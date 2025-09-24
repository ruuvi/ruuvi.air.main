/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "opt_rgb_ctrl.h"
#include <math.h>
#include <assert.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <hal/nrf_twim.h>
#include <opt4060.h>
#include <lp5810_api.h>
#include "rgb_led.h"
#include "app_settings.h"
#include "tlog.h"

LOG_MODULE_REGISTER(opt_rgb_ctrl, LOG_LEVEL_INF);

#define OPT_RGB_CTRL_DBG_LOG_ENABLED (0)

#define OPT_RGB_CTRL_CYCLE_MS (20)

#define OPT_RGB_CTRL_LP5810_CHECK_PERIOD_MS (500)

#define OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BRIGHTNESS (75)
#define OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MIN   (10)
#define OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MAX   (255)

#define OPT_RGB_CTRL_MEASURE_SET_COLOR_NUM_CYCLES (50U)
#define OPT_RGB_CTRL_MEASURE_SET_COLOR_MAX_CYCLES (100U)

#define OPT_RGB_CTRL_MEASURE_GET_LUMINOSITY_NUM_CYCLES (50U)
#define OPT_RGB_CTRL_MEASURE_GET_LUMINOSITY_MAX_CYCLES (100U)

#define USE_SENSOR_OPT4060 (1 && IS_ENABLED(CONFIG_RUUVI_AIR_USE_SENSOR_OPT4060))

#define LUMINOSITY_ARRAY_SIZE \
    (CONFIG_RUUVI_AIR_OPT4060_NUM_MEASUREMENTS_PER_SECOND * CONFIG_RUUVI_AIR_OPT4060_LUMINOSITY_AVG_PERIOD)

typedef enum opt_rgb_ctrl_error
{
    OPT_RGB_CTRL_ERROR_NONE = 0,
    OPT_RGB_CTRL_ERROR_TIMEOUT_READING_GREEN_CHANNEL_MEASUREMENT,
    OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_GREEN_CHANNEL_MEASUREMENT,
    OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_LUMINOSITY_CHANNEL_MEASUREMENT,
    OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_CNT_CHANGED_UNEXPECTEDLY,
    OPT_RGB_CTRL_ERROR_FAILED_TO_TURN_OFF_LED,
    OPT_RGB_CTRL_ERROR_CHECK_BLUE_CHANNEL_FAILED,
    OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_LATE,
    OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL,
    OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_CNT_CHANGED,
    OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_VAL_CHANGED,
    OPT_RGB_CTRL_ERROR_FAILED_TO_READ_LED,
    OPT_RGB_CTRL_ERROR_FAILED_TO_RESTORE_LED,
} opt_rgb_ctrl_error_e;

typedef union opt_rgb_ctrl_led_coord_t
{
    rgb_led_color_with_brightness_t color_with_brightness;
    rgb_led_currents_and_pwms_t     currents_and_pwms;
} opt_rgb_ctrl_led_coord_t;

typedef struct opt_rgb_ctrl_led_stage_t
{
    int32_t                  stage_duration_ticks;
    opt_rgb_ctrl_led_coord_t coord;
} opt_rgb_ctrl_led_stage_t;

typedef struct opt_rgb_ctrl_led_state_t
{
    bool                     use_raw_currents_and_pwm;
    opt_rgb_ctrl_led_coord_t coords;
} opt_rgb_ctrl_led_state_t;

typedef enum opt_rgb_ctrl_event_type
{
    OPT_RGB_CTRL_EVENT_TYPE_NONE                       = 0,
    OPT_RGB_CTRL_EVENT_TYPE_LED_CTRL_CYCLE             = 1 << 1,
    OPT_RGB_CTRL_EVENT_TYPE_MEASURE_LUMINOSITY         = 1 << 2,
    OPT_RGB_CTRL_EVENT_TYPE_LP5810_CHECK               = 1 << 3,
    OPT_RGB_CTRL_EVENT_TYPE_TURN_OFF_LED_BEFORE_REBOOT = 1 << 4,
    OPT_RGB_CTRL_EVENT_TYPE_STOP_BOOTUP_LED_FADING     = 1 << 5,
} opt_rgb_ctrl_event_type;

#define OPT_RGB_CTRL_DIMMING_RULE_MAX_NUM_STAGES    (4U)
#define OPT_RGB_CTRL_DIMMING_RULE_BOOTUP_NUM_STAGES (4U)

typedef struct opt_rgb_ctrl_dimming_rule_t
{
    bool                     is_ready;
    bool                     flag_auto_repeat;
    bool                     use_raw_currents_and_pwm;
    bool                     is_started;
    int64_t                  stage_start_time;
    int                      stage_idx;
    uint32_t                 num_stages;
    opt_rgb_ctrl_led_stage_t stages[OPT_RGB_CTRL_DIMMING_RULE_MAX_NUM_STAGES];
} opt_rgb_ctrl_dimming_rule_t;

static opt_rgb_ctrl_dimming_rule_t g_opt_rgb_ctrl_dimming_rule = {
    .is_ready = IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_AQI),
    .flag_auto_repeat         = true,
    .use_raw_currents_and_pwm = false,
    .is_started               = false,
    .stage_start_time         = 0,
    .stage_idx                = 0,
    .num_stages               = OPT_RGB_CTRL_DIMMING_RULE_BOOTUP_NUM_STAGES,
    .stages                   = {
        {
            .stage_duration_ticks = k_ms_to_ticks_ceil32(500),
            .coord = {
                .color_with_brightness = {
                    .rgb = { .red = 0, .green = 0, .blue = OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MIN, },
                    .brightness = (rgb_led_brightness_t)OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BRIGHTNESS,
                },
            },
        },
        {
            .stage_duration_ticks = k_ms_to_ticks_ceil32(100),
            .coord = {
                .color_with_brightness = {
                    .rgb = { .red = 0, .green = 0, .blue = OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MAX, },
                    .brightness = (rgb_led_brightness_t)OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BRIGHTNESS,
                },
            },
        },
        {
            .stage_duration_ticks = k_ms_to_ticks_ceil32(500),
            .coord = {
                .color_with_brightness = {
                    .rgb = { .red = 0, .green = 0, .blue = OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MAX, },
                    .brightness = (rgb_led_brightness_t)OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BRIGHTNESS,
                },
            },
        },
        {
            .stage_duration_ticks = 0,
            .coord = {
                .color_with_brightness = {
                    .rgb = { .red = 0, .green = 0, .blue = OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MIN, },
                    .brightness = (rgb_led_brightness_t)OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BRIGHTNESS,
                },
            },
        },
    },
};

static opt_rgb_ctrl_dimming_rule_t g_opt_rgb_ctrl_next_dimming_rule;
static K_MUTEX_DEFINE(g_opt_rgb_ctrl_mutex_next_dimming_rule);

static void
on_timer_led_ctrl_cycle(struct k_timer* timer_id);

static void
on_timer_lp5810_check(struct k_timer* timer_id);

static K_TIMER_DEFINE(opt_rgb_led_ctrl_cycle, &on_timer_led_ctrl_cycle, NULL);
static K_TIMER_DEFINE(opt_rgb_led_lp5810_check, &on_timer_lp5810_check, NULL);

static K_EVENT_DEFINE(opt_rgb_ctrl_event);
static K_SEM_DEFINE(opt_rgb_ctrl_sem_thread_start, 0, 1);
static K_SEM_DEFINE(opt_rgb_ctrl_sem_thread_started, 0, 1);
static K_SEM_DEFINE(opt_rgb_ctrl_sem_led_turned_off, 0, 1);
static const rgb_led_exp_current_coefs_t* g_p_led_currents_alpha;

static int32_t  g_rgb_led_set_color_delay_ticks      = 0;
static int32_t  g_rgb_led_get_luminosity_delay_ticks = 0;
static uint32_t g_opt4060_one_chan_conv_time_ticks;
static int32_t  g_opt4060_one_measurement_duration_ticks;
static float    g_opt4060_luminosity_in_manual_mode;
static float    g_opt4060_luminosity[LUMINOSITY_ARRAY_SIZE];
static float    g_opt4060_luminosity_tmp[LUMINOSITY_ARRAY_SIZE];
static int      g_opt4060_luminosity_idx;
static bool     g_led_turned_off           = false;
static bool     g_opt_rgb_ctrl_led_enabled = true;

static int64_t g_dbg_time_green_channel_measured;
static int64_t g_dbg_cur_timestamp;
static int64_t g_dbg_next_timestamp;

#if USE_SENSOR_OPT4060
static const struct device* const dev_opt4060 = DEVICE_DT_GET_ONE(ti_opt4060);
#endif // USE_SENSOR_OPT4060

void
opt_rgb_ctrl_enable_led(const bool enable)
{
    g_opt_rgb_ctrl_led_enabled = enable;
}

static void
on_timer_led_ctrl_cycle(struct k_timer* timer_id)
{
    k_event_post(&opt_rgb_ctrl_event, OPT_RGB_CTRL_EVENT_TYPE_LED_CTRL_CYCLE);
}

static void
on_timer_lp5810_check(struct k_timer* timer_id)
{
    k_event_post(&opt_rgb_ctrl_event, OPT_RGB_CTRL_EVENT_TYPE_LP5810_CHECK);
}

static void
opt_rgb_ctrl_use_fast_speed_i2c(const bool use_fast_speed)
{
    static NRF_TWIM_Type* const g_p_twim = (NRF_TWIM_Type*)0x40003000;
    if (use_fast_speed)
    {
        /*
            https://docs.nordicsemi.com/bundle/errata_nRF52840_Rev3/page/ERR/nRF52840/Rev3/latest/anomaly_840_219.html#anomaly_840_219

            Symptom:
            The low period of the SCL clock is too short to meet the I2C specification at 400 kHz.
            The actual low period of the SCL clock is 1.25 µs while the I2C specification requires the SCL clock
            to have a minimum low period of 1.3 µs.

            Workaround:
            If communication does not work at 400 kHz with an I2C compatible device that requires the SCL clock
            to have a minimum low period of 1.3 µs, use 390 kHz instead of 400kHz by writing 0x06200000
            to the FREQUENCY register. With this setting, the SCL low period is greater than 1.3 µs.
        */
        // nrf_twim_frequency_set(g_p_twim, NRF_TWIM_FREQ_400K); // Set TWI frequency to 400 kHz (FREQ=0x06400000UL)
        nrf_twim_frequency_set(g_p_twim, 0x06200000); // Set TWI frequency to 390 kHz
    }
    else
    {
        nrf_twim_frequency_set(g_p_twim, NRF_TWIM_FREQ_100K); // Set TWI frequency to 100 kHz
    }
}

static bool
measure_set_color_delay(int32_t* const p_max_time_per_call_ticks)
{
    int64_t time_accum = 0;
    int32_t cnt        = 0;

    rgb_led_lock();

    rgb_led_pwms_t saved_pwms = { 0 };
    rgb_led_read_raw_pwms(&saved_pwms);
    const rgb_led_pwms_t raw_pwms = { 0 };

    for (int i = 0; i < OPT_RGB_CTRL_MEASURE_SET_COLOR_MAX_CYCLES; ++i)
    {
        const int64_t time_start = k_uptime_ticks();

        const bool is_success = rgb_led_write_raw_pwms(&raw_pwms);

        const int64_t time_end = k_uptime_ticks();
        if (is_success)
        {
            time_accum += (time_end - time_start);
            cnt += 1;
            if (cnt >= OPT_RGB_CTRL_MEASURE_SET_COLOR_NUM_CYCLES)
            {
                break;
            }
        }
    }
    rgb_led_write_raw_pwms(&saved_pwms);
    rgb_led_unlock();
    if (0 == cnt)
    {
        return false;
    }
    const int32_t max_time_per_call_ticks = (int32_t)((time_accum + cnt - 1) / cnt);
    *p_max_time_per_call_ticks            = max_time_per_call_ticks;

    return true;
}

static bool
measure_opt4060_channel_get(int32_t* const p_max_time_per_call_ticks)
{
    struct sensor_value luminosity = { 0 };
    int64_t             time_accum = 0;
    int32_t             cnt        = 0;

    for (int i = 0; i < OPT_RGB_CTRL_MEASURE_GET_LUMINOSITY_MAX_CYCLES; ++i)
    {
        const int64_t time_start = k_uptime_ticks();
        int           res        = sensor_channel_get(dev_opt4060, SENSOR_CHAN_LIGHT, &luminosity);
        const int64_t time_end   = k_uptime_ticks();
        if (0 == res)
        {
            time_accum += (time_end - time_start);
            cnt += 1;
            if (cnt >= OPT_RGB_CTRL_MEASURE_GET_LUMINOSITY_NUM_CYCLES)
            {
                break;
            }
        }
    }
    if (0 == cnt)
    {
        return false;
    }
    const int32_t max_time_per_call_ticks = (int32_t)((time_accum + cnt - 1) / cnt);
    *p_max_time_per_call_ticks            = max_time_per_call_ticks;
    return true;
}

static void
opt_rgb_measure_i2c_delays(void)
{
    opt_rgb_ctrl_use_fast_speed_i2c(true);
    if (!measure_set_color_delay(&g_rgb_led_set_color_delay_ticks))
    {
        TLOG_ERR("Failed to measure rgb_led_set_color delay");
    }
    else
    {
        TLOG_INF("LP5810 set_color delay: %d ticks", g_rgb_led_set_color_delay_ticks);
    }
    if (!measure_opt4060_channel_get(&g_rgb_led_get_luminosity_delay_ticks))
    {
        TLOG_ERR("Failed to measure rgb_led_get_luminosity delay");
    }
    else
    {
        TLOG_INF("OPT4060 get_luminosity delay: %d ticks", g_rgb_led_get_luminosity_delay_ticks);
    }
    opt_rgb_ctrl_use_fast_speed_i2c(false);
}

static bool
get_opt4060_measurement(const enum sensor_channel chan, float* const p_val, int* const p_cnt)
{
    struct sensor_value val = { 0 };
    int                 res = sensor_channel_get(dev_opt4060, chan, &val);
    if (0 != res)
    {
        TLOG_DBG("sensor_channel_get failed: %d", res);
        return false;
    }

    *p_cnt = val.val2 & OPT4060_CHAN_CNT_MASK;
    val.val2 &= ~OPT4060_CHAN_CNT_MASK; // Clear counter bits
    *p_val = sensor_value_to_float(&val);

    return true;
}

static opt_rgb_ctrl_error_e
wait_opt4060_green_channel_measured(int* const p_cnt, int64_t* const p_time_green_channel_measured)
{
    float          val                 = 0.0f;
    int            initial_cnt         = 0;
    const uint32_t max_wait_time_ticks = g_opt4060_one_chan_conv_time_ticks * (OPT4060_CHANNEL_NUM + 2)
                                         + g_rgb_led_get_luminosity_delay_ticks * 2;

    int64_t time_start = k_uptime_ticks();
    while (1)
    {
        if (get_opt4060_measurement(SENSOR_CHAN_GREEN, &val, &initial_cnt))
        {
            break;
        }
        if ((k_uptime_ticks() - time_start) > max_wait_time_ticks)
        {
            return OPT_RGB_CTRL_ERROR_TIMEOUT_READING_GREEN_CHANNEL_MEASUREMENT;
        }
    }
    time_start = k_uptime_ticks();
    while (1)
    {
        int cnt = 0;
        if (get_opt4060_measurement(SENSOR_CHAN_GREEN, &val, &cnt))
        {
            if (cnt != initial_cnt)
            {
                *p_time_green_channel_measured = k_uptime_ticks();
                *p_cnt                         = cnt;
                break;
            }
        }
        if ((k_uptime_ticks() - time_start) > max_wait_time_ticks)
        {
            return OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_GREEN_CHANNEL_MEASUREMENT;
        }
    }
    return OPT_RGB_CTRL_ERROR_NONE;
}

static opt_rgb_ctrl_error_e
check_opt4060_blue_channel_not_measured(const int expected_cnt)
{
    const int prev_cnt = (expected_cnt - 1) & 0x0F;
    int       cnt      = 0;

    float val = 0.0f;
    if (!get_opt4060_measurement(SENSOR_CHAN_BLUE, &val, &cnt))
    {
        return OPT_RGB_CTRL_ERROR_CHECK_BLUE_CHANNEL_FAILED;
    }
    if (cnt != prev_cnt)
    {
        return OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_LATE;
    }
    return OPT_RGB_CTRL_ERROR_NONE;
}

static opt_rgb_ctrl_error_e
wait_opt4060_luminosity_channel_measured(const int expected_cnt, float* const p_val)
{
    const int     prev_cnt            = (expected_cnt - 1) & 0x0F;
    int           cnt                 = 0;
    const int64_t max_wait_time_ticks = g_opt4060_one_chan_conv_time_ticks * 2 + g_rgb_led_get_luminosity_delay_ticks;

    int64_t time_start = k_uptime_ticks();
    while (1)
    {
        if (get_opt4060_measurement(SENSOR_CHAN_LIGHT, p_val, &cnt))
        {
            if (cnt == expected_cnt)
            {
                break;
            }
            if (cnt != prev_cnt)
            {
                return OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_CNT_CHANGED_UNEXPECTEDLY;
            }
        }
        if ((k_uptime_ticks() - time_start) > max_wait_time_ticks)
        {
            return OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_LUMINOSITY_CHANNEL_MEASUREMENT;
        }
    }
    float val = 0.0f;
    if (!get_opt4060_measurement(SENSOR_CHAN_LIGHT, &val, &cnt))
    {
        return OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL;
    }
    if (cnt != expected_cnt)
    {
        return OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_CNT_CHANGED;
    }
    if (val != *p_val)
    {
        return OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_VAL_CHANGED;
    }
    return OPT_RGB_CTRL_ERROR_NONE;
}

static opt_rgb_ctrl_error_e
turn_off_led_and_measure_luminosity(float* const p_luminosity, int64_t* const p_timestamp_led_turned_off)
{
    *p_luminosity = NAN;

    int                        cnt                         = 0;
    int64_t                    time_green_channel_measured = 0;
    const opt_rgb_ctrl_error_e err1 = wait_opt4060_green_channel_measured(&cnt, &time_green_channel_measured);
    if (OPT_RGB_CTRL_ERROR_NONE != err1)
    {
        return err1;
    }
    const int64_t next_timestamp = time_green_channel_measured + g_opt4060_one_measurement_duration_ticks
                                   - g_rgb_led_set_color_delay_ticks - 2 * g_rgb_led_get_luminosity_delay_ticks - 15;

    g_dbg_cur_timestamp               = k_uptime_ticks();
    g_dbg_time_green_channel_measured = time_green_channel_measured;
    g_dbg_next_timestamp              = next_timestamp;

    while ((next_timestamp - k_uptime_ticks()) > 0)
    {
        // Wait until it is time to turn off LED
    }

    *p_timestamp_led_turned_off   = k_uptime_ticks();
    const rgb_led_pwms_t raw_pwms = { 0 };
    if (!rgb_led_write_raw_pwms(&raw_pwms))
    {
        return OPT_RGB_CTRL_ERROR_FAILED_TO_TURN_OFF_LED;
    }
    const opt_rgb_ctrl_error_e err2 = check_opt4060_blue_channel_not_measured(cnt);
    if (OPT_RGB_CTRL_ERROR_NONE != err2)
    {
        return err2;
    }
    return wait_opt4060_luminosity_channel_measured(cnt, p_luminosity);
}

static void
opt_rgb_ctrl_print_error_log(const opt_rgb_ctrl_error_e err)
{
    switch (err)
    {
        case OPT_RGB_CTRL_ERROR_NONE:
            // All good
            break;
        case OPT_RGB_CTRL_ERROR_TIMEOUT_READING_GREEN_CHANNEL_MEASUREMENT:
            TLOG_ERR("Timeout reading green channel measurement");
            break;
        case OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_GREEN_CHANNEL_MEASUREMENT:
            TLOG_ERR("Timeout waiting green channel measurement");
            break;
        case OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_LUMINOSITY_CHANNEL_MEASUREMENT:
            TLOG_ERR("Timeout waiting luminosity channel measurement");
            break;
        case OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_CNT_CHANGED_UNEXPECTEDLY:
            TLOG_ERR("OPT4060 luminosity channel cnt changed unexpectedly");
            break;
        case OPT_RGB_CTRL_ERROR_FAILED_TO_TURN_OFF_LED:
            TLOG_ERR("Failed to turn off LED");
            break;
        case OPT_RGB_CTRL_ERROR_CHECK_BLUE_CHANNEL_FAILED:
            TLOG_ERR("Failed to check blue channel not measured");
            break;
        case OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_LATE:
            TLOG_ERR("Luminosity channel was measured before LED was turned off");
            break;
        case OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL:
            TLOG_ERR("Failed to reread luminosity channel");
            break;
        case OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_CNT_CHANGED:
            TLOG_ERR("Luminosity channel count changed on reread");
            break;
        case OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_VAL_CHANGED:
            TLOG_ERR("Luminosity channel value changed on reread");
            break;
        default:
            TLOG_ERR("Unknown error: %d", err);
            break;
    }
}

static opt_rgb_ctrl_error_e
measure_luminosity_with_led_locked(
    float* const   p_luminosity,
    int64_t* const p_timestamp_led_turned_off,
    int64_t* const p_timestamp_led_turned_on)
{
    rgb_led_pwms_t saved_pwms = { 0 };

    *p_luminosity = NAN;

    if (!rgb_led_read_raw_pwms(&saved_pwms))
    {
        return OPT_RGB_CTRL_ERROR_FAILED_TO_READ_LED;
    }

    const opt_rgb_ctrl_error_e err = turn_off_led_and_measure_luminosity(p_luminosity, p_timestamp_led_turned_off);

    if (!rgb_led_write_raw_pwms(&saved_pwms))
    {
        return OPT_RGB_CTRL_ERROR_FAILED_TO_RESTORE_LED;
    }
    *p_timestamp_led_turned_on = k_uptime_ticks();

    return err;
}

static opt_rgb_ctrl_error_e
lock_led_and_measure_luminosity(
    float* const   p_luminosity,
    int64_t* const p_timestamp_led_turned_off,
    int64_t* const p_timestamp_led_turned_on)
{
    rgb_led_lock();
    opt_rgb_ctrl_use_fast_speed_i2c(true);

    const opt_rgb_ctrl_error_e err = measure_luminosity_with_led_locked(
        p_luminosity,
        p_timestamp_led_turned_off,
        p_timestamp_led_turned_on);

    opt_rgb_ctrl_use_fast_speed_i2c(false);
    rgb_led_unlock();

    return err;
}

static void
opt_rgb_ctrl_do_measure_luminosity_in_auto_mode(void)
{
    float   luminosity               = NAN;
    int64_t timestamp_led_turned_off = 0;
    int64_t timestamp_led_turned_on  = 0;

    const int64_t time_start = k_uptime_ticks();

    const opt_rgb_ctrl_error_e err = lock_led_and_measure_luminosity(
        &luminosity,
        &timestamp_led_turned_off,
        &timestamp_led_turned_on);

    const int64_t time_finish = k_uptime_ticks();

    const int32_t duration_led_off_ticks = (int32_t)(timestamp_led_turned_on - timestamp_led_turned_off);
    const int32_t duration_ticks         = (int32_t)(time_finish - time_start);

    g_opt4060_luminosity[g_opt4060_luminosity_idx] = luminosity; // NAN will be saved if measure failed

    const int prev_luminosity_idx = g_opt4060_luminosity_idx;
    g_opt4060_luminosity_idx      = (g_opt4060_luminosity_idx + 1) % ARRAY_SIZE(g_opt4060_luminosity);

    if (OPT_RGB_CTRL_DBG_LOG_ENABLED)
    {
        TLOG_INF(
            "Luminosity[%d]: %.3f lx (%" PRIu32 " ticks, %" PRIu32 " us), LED off duration: %" PRIu32 " ticks, %" PRIu32
            " us",
            prev_luminosity_idx,
            (double)luminosity,
            duration_ticks,
            k_ticks_to_us_ceil32(duration_ticks),
            duration_led_off_ticks,
            k_ticks_to_us_ceil32(duration_led_off_ticks));
        TLOG_INF("Cur timestamp: %lld", g_dbg_cur_timestamp);
        TLOG_INF(
            "%lld + %d - %d - 3 * %d - 15 = %lld",
            g_dbg_time_green_channel_measured,
            g_opt4060_one_measurement_duration_ticks,
            g_rgb_led_set_color_delay_ticks,
            g_rgb_led_get_luminosity_delay_ticks,
            g_dbg_next_timestamp);
    }

    if (OPT_RGB_CTRL_DBG_LOG_ENABLED)
    {
        opt_rgb_ctrl_print_error_log(err);
    }
}

static float
opt_rgb_ctrl_do_measure_luminosity_in_manual_mode(void)
{
    for (int i = 0; i < 3; ++i)
    {
        float value = NAN;
        int   cnt   = 0;
        if (get_opt4060_measurement(SENSOR_CHAN_LIGHT, &value, &cnt))
        {
            return value;
        }
    }
    return NAN;
}

static int32_t
calc_intermediate_current_value(
    const int32_t                           val_begin,
    const int32_t                           val_end,
    const int32_t                           delta_time,
    const int32_t                           duration_ticks,
    const rgb_led_exp_current_coef_t* const p_coef)
{
    if (0 == duration_ticks)
    {
        return val_begin;
    }
    if (val_end == val_begin)
    {
        return val_begin;
    }
    if (val_end > val_begin)
    {
        const float x1 = logf(val_begin - p_coef->current_min + 1) / p_coef->alpha;
        const float x2 = logf(val_end - p_coef->current_min + 1) / p_coef->alpha;
        const float x  = x1 + (((x2 - x1) * delta_time) / duration_ticks);
        const float y  = expf(p_coef->alpha * x) + p_coef->current_min - 1;
        return (int32_t)lrintf(y);
    }
    else
    {
        const float x1 = logf(val_end - p_coef->current_min + 1) / p_coef->alpha;
        const float x2 = logf(val_begin - p_coef->current_min + 1) / p_coef->alpha;
        const float x  = x2 - (((x2 - x1) * delta_time) / duration_ticks);
        const float y  = expf(p_coef->alpha * x) + p_coef->current_min - 1;
        return (int32_t)lrintf(y);
    }
}

static int32_t
calc_intermediate_value(
    const int32_t val_begin,
    const int32_t val_end,
    const int32_t delta_time,
    const int32_t duration_ticks)
{
    if (0 == duration_ticks)
    {
        return val_begin;
    }
    return val_begin + (((val_end - val_begin) * delta_time) / duration_ticks);
}

static bool
copy_next_dimming_rule(void)
{
    bool is_ready = false;
    k_mutex_lock(&g_opt_rgb_ctrl_mutex_next_dimming_rule, K_FOREVER);
    if (g_opt_rgb_ctrl_next_dimming_rule.is_ready)
    {
        is_ready = true;
        memcpy(&g_opt_rgb_ctrl_dimming_rule, &g_opt_rgb_ctrl_next_dimming_rule, sizeof(g_opt_rgb_ctrl_dimming_rule));
        g_opt_rgb_ctrl_next_dimming_rule.is_ready = false;
        g_opt_rgb_ctrl_dimming_rule.is_started    = false;
    }
    k_mutex_unlock(&g_opt_rgb_ctrl_mutex_next_dimming_rule);
    return is_ready;
}

static void
handle_rgb_ctrl(void)
{
    opt_rgb_ctrl_dimming_rule_t* const p_rule   = &g_opt_rgb_ctrl_dimming_rule;
    const int64_t                      cur_time = k_uptime_ticks();

    TLOG_DBG("Handling RGB control at time: %" PRId64, cur_time);

    if (!p_rule->is_ready)
    {
        if (!copy_next_dimming_rule())
        {
            TLOG_DBG("No next dimming rule available");
            return;
        }
    }
    if (!p_rule->is_started)
    {
        TLOG_DBG("Starting new dimming rule");
        p_rule->is_started       = true;
        p_rule->stage_start_time = cur_time;
        p_rule->stage_idx        = 0;
    }

    const opt_rgb_ctrl_led_stage_t* p_stage = &p_rule->stages[p_rule->stage_idx];

    int32_t       delta_time           = (int32_t)(cur_time - p_rule->stage_start_time);
    const int32_t stage_duration_ticks = p_rule->stages[p_rule->stage_idx].stage_duration_ticks;
    if (delta_time > stage_duration_ticks)
    {
        delta_time = stage_duration_ticks;
    }
    TLOG_DBG(
        "Stage %d/%d, delta_time: %d/%d ticks",
        p_rule->stage_idx,
        p_rule->num_stages,
        delta_time,
        stage_duration_ticks);

    if (p_rule->use_raw_currents_and_pwm)
    {
        const rgb_led_currents_and_pwms_t* const p_begin = &p_rule->stages[p_rule->stage_idx].coord.currents_and_pwms;
        const rgb_led_currents_and_pwms_t* const p_end   = &p_rule->stages[(p_rule->stage_idx + 1) % p_rule->num_stages]
                                                              .coord.currents_and_pwms;
        const rgb_led_currents_and_pwms_t currents_and_pwms = {
            .led_currents = {
                .current_red = (rgb_led_current_t)calc_intermediate_current_value(
                    p_begin->led_currents.current_red,
                    p_end->led_currents.current_red,
                    delta_time,
                    stage_duration_ticks,
                    &g_p_led_currents_alpha->coef_red),
                .current_green = (rgb_led_current_t)calc_intermediate_current_value(
                    p_begin->led_currents.current_green,
                    p_end->led_currents.current_green,
                    delta_time,
                    stage_duration_ticks,
                    &g_p_led_currents_alpha->coef_green),
                .current_blue = (rgb_led_current_t)calc_intermediate_current_value(
                    p_begin->led_currents.current_blue,
                    p_end->led_currents.current_blue,
                    delta_time,
                    stage_duration_ticks,
                    &g_p_led_currents_alpha->coef_blue),
            },
            .led_pwms = {
                .pwm_red = (rgb_led_pwm_t)calc_intermediate_value(
                    p_begin->led_pwms.pwm_red,
                    p_end->led_pwms.pwm_red,
                    delta_time,
                    stage_duration_ticks),
                .pwm_green = (rgb_led_pwm_t)calc_intermediate_value(
                    p_begin->led_pwms.pwm_green,
                    p_end->led_pwms.pwm_green,
                    delta_time,
                    stage_duration_ticks),
                .pwm_blue = (rgb_led_pwm_t)calc_intermediate_value(
                    p_begin->led_pwms.pwm_blue,
                    p_end->led_pwms.pwm_blue,
                    delta_time,
                    stage_duration_ticks),
            },
        };
        TLOG_DBG(
            "Begin: Currents: <%d, %d, %d>, PWMs: <%d, %d, %d>",
            p_begin->led_currents.current_red,
            p_begin->led_currents.current_green,
            p_begin->led_currents.current_blue,
            p_begin->led_pwms.pwm_red,
            p_begin->led_pwms.pwm_green,
            p_begin->led_pwms.pwm_blue);
        TLOG_DBG(
            "End: Currents: <%d, %d, %d>, PWMs: <%d, %d, %d>",
            p_end->led_currents.current_red,
            p_end->led_currents.current_green,
            p_end->led_currents.current_blue,
            p_end->led_pwms.pwm_red,
            p_end->led_pwms.pwm_green,
            p_end->led_pwms.pwm_blue);
        TLOG_DBG(
            "Set raw currents and PWMs: Currents: <%d, %d, %d>, PWMs: <%d, %d, %d>",
            currents_and_pwms.led_currents.current_red,
            currents_and_pwms.led_currents.current_green,
            currents_and_pwms.led_currents.current_blue,
            currents_and_pwms.led_pwms.pwm_red,
            currents_and_pwms.led_pwms.pwm_green,
            currents_and_pwms.led_pwms.pwm_blue);

        if (g_opt_rgb_ctrl_led_enabled)
        {
            rgb_led_set_raw_currents_and_pwms(&currents_and_pwms.led_currents, &currents_and_pwms.led_pwms);
        }
    }
    else
    {
        const rgb_led_color_with_brightness_t* const p_begin = &p_rule->stages[p_rule->stage_idx]
                                                                    .coord.color_with_brightness;
        const rgb_led_color_with_brightness_t* const p_end
            = &p_rule->stages[(p_rule->stage_idx + 1) % p_rule->num_stages].coord.color_with_brightness;

        const rgb_led_brightness_t brightness = (rgb_led_brightness_t)
            calc_intermediate_value(p_begin->brightness, p_end->brightness, delta_time, stage_duration_ticks);

        const rgb_led_color_t color = {
            .red = (rgb_led_color_val_t)
                calc_intermediate_value(p_begin->rgb.red, p_end->rgb.red, delta_time, stage_duration_ticks),
            .green = (rgb_led_color_val_t)
                calc_intermediate_value(p_begin->rgb.green, p_end->rgb.green, delta_time, stage_duration_ticks),
            .blue = (rgb_led_color_val_t)
                calc_intermediate_value(p_begin->rgb.blue, p_end->rgb.blue, delta_time, stage_duration_ticks),
        };
        TLOG_DBG(
            "Begin: Color: <%d, %d, %d>, Brightness: %d",
            p_begin->rgb.red,
            p_begin->rgb.green,
            p_begin->rgb.blue,
            p_begin->brightness);
        TLOG_DBG(
            "End: Color: <%d, %d, %d>, Brightness: %d",
            p_end->rgb.red,
            p_end->rgb.green,
            p_end->rgb.blue,
            p_end->brightness);
        TLOG_DBG("Set Color: <%d, %d, %d>, Brightness: %d", color.red, color.green, color.blue, brightness);

        if (g_opt_rgb_ctrl_led_enabled)
        {
            rgb_led_set_brightness_and_color(brightness, &color);
        }
    }

    if (delta_time == p_stage->stage_duration_ticks)
    {
        p_rule->stage_idx        = (p_rule->stage_idx + 1) % p_rule->num_stages;
        p_rule->stage_start_time = cur_time;
        if (0 == p_rule->stage_idx)
        {
            if (!p_rule->flag_auto_repeat)
            {
                p_rule->is_ready = false;
                copy_next_dimming_rule();
            }
        }
    }
}

static void
opt_rgb_ctrl_thread(void* p1, void* p2, void* p3)
{
    k_sem_take(&opt_rgb_ctrl_sem_thread_start, K_FOREVER);
    TLOG_INF("Start opt_rgb_ctrl thread");

    g_led_turned_off = false;

    if (!device_is_ready(dev_opt4060))
    {
        TLOG_ERR("Device %s is not ready", dev_opt4060->name);
    }
    else
    {
        TLOG_INF("Device %p: name %s", dev_opt4060, dev_opt4060->name);
    }

    for (int i = 0; i < ARRAY_SIZE(g_opt4060_luminosity); ++i)
    {
        g_opt4060_luminosity[i] = NAN;
    }
    g_opt4060_luminosity_idx = 0;

    opt_rgb_measure_i2c_delays();
    g_opt4060_one_chan_conv_time_ticks       = k_us_to_ticks_ceil32(opt4060_get_conv_time_us());
    g_opt4060_one_measurement_duration_ticks = opt4060_get_one_measurement_duration_ticks(dev_opt4060);

    if (APP_SETTINGS_LED_MODE_AUTO != app_settings_get_led_mode())
    {
        const uint32_t conv_time                 = 800;
        g_opt4060_one_measurement_duration_ticks = k_ms_to_ticks_ceil32(conv_time);
        TLOG_INF("Setting OPT4060 conversion time to %u ms", conv_time);
        if (opt4060_configure_conv_time(dev_opt4060, OPT4060_REG_CONFIG_VAL_CONV_TIME_800_MS) < 0)
        {
            TLOG_ERR("Failed to configure conversion time");
        }
    }

    k_timer_start(&opt_rgb_led_ctrl_cycle, K_MSEC(0), K_MSEC(OPT_RGB_CTRL_CYCLE_MS));
    k_timer_start(&opt_rgb_led_lp5810_check, K_MSEC(0), K_MSEC(OPT_RGB_CTRL_LP5810_CHECK_PERIOD_MS));

    k_sem_give(&opt_rgb_ctrl_sem_thread_started);

    while (1)
    {
        const uint32_t events = k_event_wait(
            &opt_rgb_ctrl_event,
            OPT_RGB_CTRL_EVENT_TYPE_LED_CTRL_CYCLE | OPT_RGB_CTRL_EVENT_TYPE_MEASURE_LUMINOSITY
                | OPT_RGB_CTRL_EVENT_TYPE_LP5810_CHECK | OPT_RGB_CTRL_EVENT_TYPE_TURN_OFF_LED_BEFORE_REBOOT
                | OPT_RGB_CTRL_EVENT_TYPE_STOP_BOOTUP_LED_FADING,
            false,
            K_FOREVER);
        k_event_clear(&opt_rgb_ctrl_event, events);
        if (g_led_turned_off)
        {
            continue;
        }
        if (0 != (events & OPT_RGB_CTRL_EVENT_TYPE_LED_CTRL_CYCLE))
        {
            handle_rgb_ctrl();
        }
        if (0 != (events & OPT_RGB_CTRL_EVENT_TYPE_MEASURE_LUMINOSITY))
        {
            if (APP_SETTINGS_LED_MODE_AUTO == app_settings_get_led_mode())
            {
                TLOG_DBG("Measuring luminosity in auto mode");
                opt_rgb_ctrl_do_measure_luminosity_in_auto_mode();
            }
            else
            {
                TLOG_DBG("Measuring luminosity in manual mode");
                g_opt4060_luminosity_in_manual_mode = opt_rgb_ctrl_do_measure_luminosity_in_manual_mode();
            }
        }
        if (0 != (events & OPT_RGB_CTRL_EVENT_TYPE_LP5810_CHECK))
        {
            rgb_led_check_and_reinit_if_needed();
        }
        if (0 != (events & OPT_RGB_CTRL_EVENT_TYPE_TURN_OFF_LED_BEFORE_REBOOT))
        {
            TLOG_WRN("Turning off LED before reboot");
            rgb_led_deinit();
            g_led_turned_off = true;
            k_sem_give(&opt_rgb_ctrl_sem_led_turned_off);
        }
        if (0 != (events & OPT_RGB_CTRL_EVENT_TYPE_STOP_BOOTUP_LED_FADING))
        {
            TLOG_INF("Stopping bootup LED fading");
            g_opt_rgb_ctrl_dimming_rule.flag_auto_repeat = false;
        }
    }
}

K_THREAD_DEFINE(
    opt_rgb_ctrl,
    CONFIG_RUUVI_AIR_OPT_RGB_CTRL_THREAD_STACK_SIZE,
    &opt_rgb_ctrl_thread,
    NULL,
    NULL,
    NULL,
    CONFIG_RUUVI_AIR_OPT_RGB_CTRL_THREAD_PRIORITY,
    0,
    0 /* Scheduling delay (in milliseconds) */);

void
opt_rgb_ctrl_init(const rgb_led_exp_current_coefs_t* const p_led_currents_alpha)
{
    g_p_led_currents_alpha = p_led_currents_alpha;
    k_sem_give(&opt_rgb_ctrl_sem_thread_start);
    k_sem_take(&opt_rgb_ctrl_sem_thread_started, K_FOREVER);
}

void
opt_rgb_ctrl_measure_luminosity(void)
{
    TLOG_DBG("Measure luminosity");
    k_event_post(&opt_rgb_ctrl_event, OPT_RGB_CTRL_EVENT_TYPE_MEASURE_LUMINOSITY);
}

void
opt_rgb_ctrl_turn_off_led_before_reboot(void)
{
    TLOG_INF("Turn off LED before reboot");
    k_event_post(&opt_rgb_ctrl_event, OPT_RGB_CTRL_EVENT_TYPE_TURN_OFF_LED_BEFORE_REBOOT);
    k_sem_take(&opt_rgb_ctrl_sem_led_turned_off, K_FOREVER);
}

void
opt_rgb_ctrl_stop_bootup_led_fading(void)
{
    TLOG_INF("Stop bootup LED fading");
    k_event_post(&opt_rgb_ctrl_event, OPT_RGB_CTRL_EVENT_TYPE_STOP_BOOTUP_LED_FADING);
}

void
opt_rgb_ctrl_set_next_brightnes_and_color(const rgb_led_brightness_t brightness, const rgb_led_color_t* const p_color)
{
    TLOG_INF(
        "Set next brightness and color: brightness=%d, color=<%d, %d, %d>",
        brightness,
        p_color->red,
        p_color->green,
        p_color->blue);

    k_mutex_lock(&g_opt_rgb_ctrl_mutex_next_dimming_rule, K_FOREVER);

    const opt_rgb_ctrl_dimming_rule_t* const     p_prev_rule                   = &g_opt_rgb_ctrl_dimming_rule;
    const bool                                   prev_use_brightness_and_color = !p_prev_rule->use_raw_currents_and_pwm;
    const opt_rgb_ctrl_led_stage_t* const        p_prev_stage = &p_prev_rule->stages[p_prev_rule->num_stages - 1];
    const rgb_led_color_with_brightness_t* const p_prev_color = &p_prev_stage->coord.color_with_brightness;
    opt_rgb_ctrl_dimming_rule_t* const           p_rule       = &g_opt_rgb_ctrl_next_dimming_rule;
    p_rule->flag_auto_repeat                                  = false;
    p_rule->use_raw_currents_and_pwm                          = false;
    p_rule->is_started                                        = false;
    p_rule->stage_start_time                                  = 0;
    p_rule->stage_idx                                         = 0;
    p_rule->num_stages                                        = 2;
    p_rule->stages[0] = (opt_rgb_ctrl_led_stage_t){
        .stage_duration_ticks = k_ms_to_ticks_ceil32(1000),
        .coord = {
            .color_with_brightness = {
                .rgb = {
                    .red = prev_use_brightness_and_color ? p_prev_color->rgb.red : 0,
                    .green = prev_use_brightness_and_color ? p_prev_color->rgb.green : 0,
                    .blue = prev_use_brightness_and_color ? p_prev_color->rgb.blue : 0,
                },
                .brightness = prev_use_brightness_and_color ? p_prev_color->brightness : 0,
            },
        },
    };
    p_rule->stages[1] = (opt_rgb_ctrl_led_stage_t){
        .stage_duration_ticks = 0,
        .coord = {
            .color_with_brightness = {
                .rgb = {
                    .red = p_color->red,
                    .green = p_color->green,
                    .blue = p_color->blue,
                },
                .brightness = brightness,
            },
        },
    };
    p_rule->is_ready = true;

    k_mutex_unlock(&g_opt_rgb_ctrl_mutex_next_dimming_rule);
}

void
opt_rgb_ctrl_set_next_color_black(void)
{
    TLOG_INF("Set next color to black");
    const rgb_led_color_t black_color = { .red = 0, .green = 0, .blue = 0 };
    opt_rgb_ctrl_set_next_brightnes_and_color(0, &black_color);
}

void
opt_rgb_ctrl_set_next_raw_currents_and_pwms(
    const rgb_led_currents_t* const p_rgb_led_currents,
    const rgb_led_pwms_t* const     p_rgb_led_pwms)
{
    TLOG_INF(
        "Set next raw currents and PWMs: Currents: <%d, %d, %d>, PWMs: <%d, %d, %d>",
        p_rgb_led_currents->current_red,
        p_rgb_led_currents->current_green,
        p_rgb_led_currents->current_blue,
        p_rgb_led_pwms->pwm_red,
        p_rgb_led_pwms->pwm_green,
        p_rgb_led_pwms->pwm_blue);

    k_mutex_lock(&g_opt_rgb_ctrl_mutex_next_dimming_rule, K_FOREVER);

    const opt_rgb_ctrl_dimming_rule_t* const p_prev_rule                   = &g_opt_rgb_ctrl_dimming_rule;
    const bool                               prev_use_raw_currents_and_pwm = p_prev_rule->use_raw_currents_and_pwm;
    const opt_rgb_ctrl_led_stage_t* const    p_prev_stage     = &p_prev_rule->stages[p_prev_rule->num_stages - 1];
    const rgb_led_currents_and_pwms_t* const p_prev_curs_pwms = &p_prev_stage->coord.currents_and_pwms;
    opt_rgb_ctrl_dimming_rule_t* const       p_rule           = &g_opt_rgb_ctrl_next_dimming_rule;
    p_rule->flag_auto_repeat                                  = false;
    p_rule->use_raw_currents_and_pwm                          = true;
    p_rule->is_started                                        = false;
    p_rule->stage_start_time                                  = 0;
    p_rule->stage_idx                                         = 0;
    p_rule->num_stages                                        = 2;
    p_rule->stages[0] = (opt_rgb_ctrl_led_stage_t){
        .stage_duration_ticks = k_ms_to_ticks_ceil32(CONFIG_RUUVI_AIR_LED_DIMMING_INTERVAL_MS),
        .coord = {
            .currents_and_pwms = {
                .led_currents = {
                    .current_red = prev_use_raw_currents_and_pwm ? p_prev_curs_pwms->led_currents.current_red : 0,
                    .current_green = prev_use_raw_currents_and_pwm ? p_prev_curs_pwms->led_currents.current_green : 0,
                    .current_blue = prev_use_raw_currents_and_pwm ? p_prev_curs_pwms->led_currents.current_blue : 0,
                },
                .led_pwms = {
                    .pwm_red = prev_use_raw_currents_and_pwm ? p_prev_curs_pwms->led_pwms.pwm_red : 0,
                    .pwm_green = prev_use_raw_currents_and_pwm ? p_prev_curs_pwms->led_pwms.pwm_green : 0,
                    .pwm_blue = prev_use_raw_currents_and_pwm ? p_prev_curs_pwms->led_pwms.pwm_blue : 0,
                },
            },
        },
    };
    p_rule->stages[1] = (opt_rgb_ctrl_led_stage_t){
        .stage_duration_ticks = 0,
        .coord = {
            .currents_and_pwms = {
                .led_currents = {
                    .current_red = p_rgb_led_currents->current_red,
                    .current_green = p_rgb_led_currents->current_green,
                    .current_blue = p_rgb_led_currents->current_blue,
                },
                .led_pwms = {
                    .pwm_red = p_rgb_led_pwms->pwm_red,
                    .pwm_green = p_rgb_led_pwms->pwm_green,
                    .pwm_blue = p_rgb_led_pwms->pwm_blue,
                },
            },
        },
    };
    TLOG_DBG(
        "Set next raw currents and PWMs: Begin: Currents: <%d, %d, %d>, PWMs: <%d, %d, %d>",
        p_rule->stages[0].coord.currents_and_pwms.led_currents.current_red,
        p_rule->stages[0].coord.currents_and_pwms.led_currents.current_green,
        p_rule->stages[0].coord.currents_and_pwms.led_currents.current_blue,
        p_rule->stages[0].coord.currents_and_pwms.led_pwms.pwm_red,
        p_rule->stages[0].coord.currents_and_pwms.led_pwms.pwm_green,
        p_rule->stages[0].coord.currents_and_pwms.led_pwms.pwm_blue);
    TLOG_DBG(
        "Set next raw currents and PWMs: End: Currents: <%d, %d, %d>, PWMs: <%d, %d, %d>",
        p_rule->stages[1].coord.currents_and_pwms.led_currents.current_red,
        p_rule->stages[1].coord.currents_and_pwms.led_currents.current_green,
        p_rule->stages[1].coord.currents_and_pwms.led_currents.current_blue,
        p_rule->stages[1].coord.currents_and_pwms.led_pwms.pwm_red,
        p_rule->stages[1].coord.currents_and_pwms.led_pwms.pwm_green,
        p_rule->stages[1].coord.currents_and_pwms.led_pwms.pwm_blue);

    p_rule->is_ready = true;

    k_mutex_unlock(&g_opt_rgb_ctrl_mutex_next_dimming_rule);
}

/**
 * @brief Comparison function for qsort to sort floats in ascending order.
 * @param a A pointer to the first float.
 * @param b A pointer to the second float.
 * @return An integer less than, equal to, or greater than zero if the first argument is considered to be respectively
 * less than, equal to, or greater than the second.
 */
static int
compare_floats(const void* a, const void* b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

/**
 * @brief Calculates the median of a given array of floats.
 * @param p_arr The array of floats.
 * @param arr_size The number of elements in the array.
 * @return The median value.
 */
static float
get_median(float* const p_arr, size_t arr_size)
{
    if (0 == (arr_size % 2))
    {
        return (p_arr[arr_size / 2 - 1] + p_arr[arr_size / 2]) / 2.0f;
    }
    else
    {
        return p_arr[arr_size / 2];
    }
}

/**
 * @brief Calculates the average of an array of floats after removing outliers using the IQR method.
 * @param p_arr The input array of floats (may contain NaNs).
 * @param p_arr_tmp A temporary array to hold valid data points.
 * @param arr_size The total number of elements in the input array.
 * @param p_flag_discarded Pointer to a boolean flag that will be set to true if any outliers were discarded.
 * @return The average of the cleaned data, or 0.0f if no valid data points remain.
 */
static float
average_without_outliers(
    const float* const p_arr,
    float* const       p_arr_tmp,
    const size_t       arr_size,
    bool* const        p_flag_discarded)
{
    *p_flag_discarded = false;

    // 1. Count valid (non-NaN) values and copy them into the temporary array
    size_t valid_count = 0;
    for (size_t i = 0; i < arr_size; ++i)
    {
        if (!isnan(p_arr[i]))
        {
            p_arr_tmp[valid_count] = p_arr[i];
            valid_count += 1;
        }
    }

    if (valid_count < 4)
    {
        // Not enough data points to reliably calculate quartiles.
        return NAN;
    }

    // 2. Sort the valid data array
    qsort(p_arr_tmp, valid_count, sizeof(float), &compare_floats);

    // 3. Calculate Q1, Q3, and IQR
    float  q1, q3;
    size_t half_size = valid_count / 2;
    if (0 == (valid_count % 2))
    {
        // Even number of elements
        q1 = get_median(p_arr_tmp, half_size);
        q3 = get_median(p_arr_tmp + half_size, half_size);
    }
    else
    {
        // Odd number of elements
        q1 = get_median(p_arr_tmp, half_size);
        q3 = get_median(p_arr_tmp + half_size + 1, half_size);
    }

    const float iqr = q3 - q1;

    // 4. Define outlier bounds
    // const float lower_bound = q1 - 1.5f * iqr;
    const float upper_bound = q3 + 1.5f * iqr;

    // 5. Filter outliers and calculate sum
    float  sum         = 0.0f;
    size_t clean_count = 0;
    for (size_t i = 0; i < valid_count; i++)
    {
        if (/* p_arr_tmp[i] >= lower_bound && */ p_arr_tmp[i] <= upper_bound)
        {
            sum += p_arr_tmp[i];
            clean_count++;
        }
    }
    if (clean_count < valid_count)
    {
        *p_flag_discarded = true;
    }

    if (0 == clean_count)
    {
        return NAN;
    }
    return sum / clean_count;
}

float
opt_rgb_ctrl_get_luminosity(void)
{
    if (APP_SETTINGS_LED_MODE_AUTO == app_settings_get_led_mode())
    {
        if (OPT_RGB_CTRL_DBG_LOG_ENABLED)
        {
            for (int i = 0; i < ARRAY_SIZE(g_opt4060_luminosity); ++i)
            {
                TLOG_INF("luminosity[%d] = %.3f", i, (double)g_opt4060_luminosity[i]);
            }
        }
        bool  flag_is_discarded = false;
        float luminosity        = average_without_outliers(
            g_opt4060_luminosity,
            g_opt4060_luminosity_tmp,
            ARRAY_SIZE(g_opt4060_luminosity),
            &flag_is_discarded);
        if (OPT_RGB_CTRL_DBG_LOG_ENABLED)
        {
            if (flag_is_discarded)
            {
                TLOG_ERR("Some outliers were discarded when calculating average luminosity");
            }
            TLOG_WRN("Average luminosity = %.3f", (double)luminosity);
        }
        return luminosity;
    }
    else
    {
        return g_opt4060_luminosity_in_manual_mode;
    }
}
