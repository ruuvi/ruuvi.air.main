/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "opt_rgb_ctrl.h"
#include "opt_rgb_ctrl_auto.h"
#include <math.h>
#include <assert.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys_clock.h>
#include <opt4060.h>
#include <lp5810_api.h>
#include "rgb_led.h"
#include "app_settings.h"
#include "zephyr_api.h"
#include "tlog.h"

#define OPT_RGB_CTRL_DBG_LOG_ENABLED (0)

LOG_MODULE_REGISTER(opt_rgb_ctrl, LOG_LEVEL_INF);

#define OPT_GRB_CTRL_MAX_RETRIES (3)

#define OPT_RGB_CTRL_DIMMING_RULE_NUM_STAGES 2

#define OPT_RGB_CTRL_CYCLE_MS (20)

#define OPT_RGB_CTRL_LP5810_CHECK_PERIOD_MS (500)

#define OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BRIGHTNESS (100)
#define OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MIN   (10)
#define OPT_RGB_CTRL_LED_BOOTUP_DIMMING_BLUE_MAX   (255)

#define USE_SENSOR_OPT4060 \
    (1 && IS_ENABLED(CONFIG_OPT4060) && IS_ENABLED(CONFIG_RUUVI_AIR_USE_SENSOR_OPT4060) \
     && DT_HAS_COMPAT_STATUS_OKAY(ti_opt4060))

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

typedef enum opt_rgb_ctrl_event_type_e
{
    OPT_RGB_CTRL_EVENT_TYPE_NONE                   = 0,
    OPT_RGB_CTRL_EVENT_TYPE_LED_CTRL_CYCLE         = 1 << 1,
    OPT_RGB_CTRL_EVENT_TYPE_MEASURE_LUMINOSITY     = 1 << 2,
    OPT_RGB_CTRL_EVENT_TYPE_LP5810_CHECK           = 1 << 3,
    OPT_RGB_CTRL_EVENT_TYPE_STOP_BOOTUP_LED_FADING = 1 << 4,
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
    int32_t                  stage_idx;
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

static float32_t g_opt4060_luminosity_in_manual_mode;
static bool      g_led_turned_off               = false;
static int32_t   g_opt_rgb_ctrl_led_disable_cnt = 0;

#if USE_SENSOR_OPT4060
static const struct device* const dev_opt4060 = DEVICE_DT_GET_ONE(ti_opt4060);
#endif // USE_SENSOR_OPT4060

bool
opt_rgb_ctrl_is_opt4060_ready(void)
{
#if USE_SENSOR_OPT4060
    return device_is_ready(dev_opt4060);
#else
    return false;
#endif // USE_SENSOR_OPT4060
}

void
opt_rgb_ctrl_enable_led(const bool enable)
{
    rgb_led_lock();
    if (enable)
    {
        if (g_opt_rgb_ctrl_led_disable_cnt > 0)
        {
            g_opt_rgb_ctrl_led_disable_cnt -= 1;
        }
    }
    else
    {
        g_opt_rgb_ctrl_led_disable_cnt += 1;
    }
    rgb_led_unlock();
}

static bool
opt_rgb_ctrl_is_led_enabled(void)
{
    rgb_led_lock();
    const bool flag_led_enabled = (0 == g_opt_rgb_ctrl_led_disable_cnt) ? true : false;
    rgb_led_unlock();
    return flag_led_enabled;
}

static void
on_timer_led_ctrl_cycle(__unused struct k_timer* timer_id)
{
    if (!rgb_led_is_lp5810_ready())
    {
        return;
    }
    k_event_post(&opt_rgb_ctrl_event, OPT_RGB_CTRL_EVENT_TYPE_LED_CTRL_CYCLE);
}

static void
on_timer_lp5810_check(__unused struct k_timer* timer_id)
{
    if (!rgb_led_is_lp5810_ready())
    {
        return;
    }
    k_event_post(&opt_rgb_ctrl_event, OPT_RGB_CTRL_EVENT_TYPE_LP5810_CHECK);
}

bool
opt_rgb_ctrl_get_opt4060_measurement(
    const enum sensor_channel        chan,
    float32_t* const                 p_val,
    opt4060_measurement_cnt_t* const p_cnt)
{
#if USE_SENSOR_OPT4060
    struct sensor_value val = { 0 };
    zephyr_api_ret_t    res = sensor_channel_get(dev_opt4060, chan, &val);
    if (0 != res)
    {
        TLOG_DBG("sensor_channel_get failed: %d", res);
        return false;
    }
    uint32_t tmp_val2 = (uint32_t)val.val2;
    *p_cnt            = tmp_val2 & OPT4060_MEASUREMENT_CNT_MASK;
    tmp_val2 &= ~OPT4060_MEASUREMENT_CNT_MASK; // Clear counter bits
    val.val2 = (int32_t)tmp_val2;
    *p_val   = sensor_value_to_float(&val);
#else
    *p_cnt = 0;
    *p_val = NAN;
#endif
    return true;
}

static float32_t
opt_rgb_ctrl_do_measure_luminosity_in_manual_mode(void)
{
    if (!opt_rgb_ctrl_is_opt4060_ready())
    {
        return NAN;
    }
    for (int32_t i = 0; i < OPT_GRB_CTRL_MAX_RETRIES; ++i)
    {
        float32_t                 value = NAN;
        opt4060_measurement_cnt_t cnt   = 0;
        if (opt_rgb_ctrl_get_opt4060_measurement(SENSOR_CHAN_LIGHT, &value, &cnt))
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
    if (val_begin < p_coef->current_min)
    {
        return p_coef->current_min;
    }
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
        const int32_t   delta_val1 = val_begin - p_coef->current_min;
        const float32_t x1         = logf((float32_t)delta_val1 + 1.0f) / p_coef->alpha;
        const int32_t   delta_val2 = val_end - p_coef->current_min;
        const float32_t x2         = logf((float32_t)delta_val2 + 1.0f) / p_coef->alpha;
        const float32_t x          = x1 + (((x2 - x1) * (float32_t)delta_time) / (float32_t)duration_ticks);
        const float32_t y          = (expf(p_coef->alpha * x) + p_coef->current_min) - 1.0f;
        return (int32_t)lrintf(y);
    }
    else
    {
        const int32_t   delta_val1 = val_end - p_coef->current_min;
        const float32_t x1         = logf((float32_t)delta_val1 + 1.0f) / p_coef->alpha;
        const int32_t   delta_val2 = val_begin - p_coef->current_min;
        const float32_t x2         = logf((float32_t)delta_val2 + 1.0f) / p_coef->alpha;
        const float32_t x          = x2 - (((x2 - x1) * (float32_t)delta_time) / (float32_t)duration_ticks);
        const float32_t y          = (expf(p_coef->alpha * x) + p_coef->current_min) - 1.0f;
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
handle_rgb_ctrl_raw_currents_and_pwm(
    const opt_rgb_ctrl_dimming_rule_t* const p_rule,
    const int32_t                            delta_time,
    const int32_t                            stage_duration_ticks)
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

    if (opt_rgb_ctrl_is_led_enabled())
    {
        rgb_led_lock();
        rgb_led_set_raw_currents_and_pwms(&currents_and_pwms.led_currents, &currents_and_pwms.led_pwms);
        rgb_led_unlock();
    }
}

static void
handle_rgb_ctrl_brightness_and_color(
    const opt_rgb_ctrl_dimming_rule_t* const p_rule,
    const int32_t                            delta_time,
    const int32_t                            stage_duration_ticks)
{
    const rgb_led_color_with_brightness_t* const p_begin = &p_rule->stages[p_rule->stage_idx]
                                                                .coord.color_with_brightness;
    const rgb_led_color_with_brightness_t* const p_end = &p_rule->stages[(p_rule->stage_idx + 1) % p_rule->num_stages]
                                                              .coord.color_with_brightness;

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

    if (opt_rgb_ctrl_is_led_enabled())
    {
        rgb_led_lock();
        rgb_led_set_brightness_and_color(brightness, &color);
        rgb_led_unlock();
    }
}

static void
handle_rgb_ctrl(void)
{
    opt_rgb_ctrl_dimming_rule_t* const p_rule   = &g_opt_rgb_ctrl_dimming_rule;
    const int64_t                      cur_time = k_uptime_ticks();

    TLOG_DBG("Handling RGB control at time: %" PRId64, cur_time);

    if (!p_rule->is_ready)
    {
        const bool is_next_rule_copied = copy_next_dimming_rule();
        if (!is_next_rule_copied)
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
        handle_rgb_ctrl_raw_currents_and_pwm(p_rule, delta_time, stage_duration_ticks);
    }
    else
    {
        handle_rgb_ctrl_brightness_and_color(p_rule, delta_time, stage_duration_ticks);
    }

    if (delta_time == p_stage->stage_duration_ticks)
    {
        p_rule->stage_idx        = (p_rule->stage_idx + 1) % p_rule->num_stages;
        p_rule->stage_start_time = cur_time;
        if ((0 == p_rule->stage_idx) && (!p_rule->flag_auto_repeat))
        {
            p_rule->is_ready = false;
            copy_next_dimming_rule();
        }
    }
}

static void
opt_rgb_ctrl_thread(__unused void* p1, __unused void* p2, __unused void* p3)
{
    k_sem_take(&opt_rgb_ctrl_sem_thread_start, K_FOREVER);
    TLOG_INF("Start opt_rgb_ctrl thread");

    g_led_turned_off = false;

#if USE_SENSOR_OPT4060
    if (!device_is_ready(dev_opt4060))
    {
        TLOG_ERR("Device %s is not ready", dev_opt4060->name);
    }
    else
    {
        TLOG_INF("Device %p: name %s", dev_opt4060, dev_opt4060->name);
    }
#endif

    opt_rgb_ctrl_auto_init();

    opt_rgb_ctrl_auto_measure_i2c_delays();

    if (!app_settings_is_led_mode_auto())
    {
#if USE_SENSOR_OPT4060
        const bool is_opt4060_ready = opt_rgb_ctrl_is_opt4060_ready();
        if (is_opt4060_ready)
        {
            const uint32_t conv_time = 800;
            TLOG_INF("Setting OPT4060 conversion time to %u ms", conv_time);
            if (opt4060_configure_conv_time(dev_opt4060, OPT4060_REG_CONFIG_VAL_CONV_TIME_800_MS) < 0)
            {
                TLOG_ERR("Failed to configure conversion time");
            }
        }
#endif // USE_SENSOR_OPT4060
    }

    k_timer_start(&opt_rgb_led_ctrl_cycle, K_MSEC(0), K_MSEC(OPT_RGB_CTRL_CYCLE_MS));
    k_timer_start(&opt_rgb_led_lp5810_check, K_MSEC(0), K_MSEC(OPT_RGB_CTRL_LP5810_CHECK_PERIOD_MS));

    k_sem_give(&opt_rgb_ctrl_sem_thread_started);

    while (1)
    {
        const uint32_t events = k_event_wait(
            &opt_rgb_ctrl_event,
            OPT_RGB_CTRL_EVENT_TYPE_LED_CTRL_CYCLE | OPT_RGB_CTRL_EVENT_TYPE_MEASURE_LUMINOSITY
                | OPT_RGB_CTRL_EVENT_TYPE_LP5810_CHECK | OPT_RGB_CTRL_EVENT_TYPE_STOP_BOOTUP_LED_FADING,
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
            if (app_settings_is_led_mode_auto())
            {
                TLOG_DBG("Measuring luminosity in auto mode");
                opt_rgb_ctrl_auto_do_measure_luminosity();
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
    const enum app_settings_led_mode_e led_mode = app_settings_get_led_mode();
    if (APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY != led_mode)
    {
        const rgb_led_brightness_t brightness = app_settings_get_led_brightness();
        uint8_t                    dim_pwm    = 255U;
        if (APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE == led_mode)
        {
            const app_settings_led_brightness_deci_percent_t deci_percent
                = app_settings_get_led_brightness_deci_percent();
            app_settings_conv_deci_percent_to_brightness(deci_percent, &dim_pwm);
        }
        for (int32_t i = 0; i < OPT_RGB_CTRL_DIMMING_RULE_BOOTUP_NUM_STAGES; ++i)
        {
            rgb_led_color_with_brightness_t* const p_color = &g_opt_rgb_ctrl_dimming_rule.stages[i]
                                                                  .coord.color_with_brightness;
            p_color->brightness = brightness;
            p_color->rgb.red    = (uint8_t)((p_color->rgb.red * dim_pwm) / RGB_LED_PWM_MAX);
            p_color->rgb.green  = (uint8_t)((p_color->rgb.green * dim_pwm) / RGB_LED_PWM_MAX);
            p_color->rgb.blue   = (uint8_t)((p_color->rgb.blue * dim_pwm) / RGB_LED_PWM_MAX);
        }
    }

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
    const rgb_led_color_with_brightness_t black_color = {
        .rgb = { .red = 0, .green = 0, .blue = 0, },
        .brightness = 0,
    };
    const rgb_led_color_with_brightness_t* const p_color_used = prev_use_brightness_and_color ? p_prev_color
                                                                                              : &black_color;
    p_rule->flag_auto_repeat                                  = false;
    p_rule->use_raw_currents_and_pwm                          = false;
    p_rule->is_started                                        = false;
    p_rule->stage_start_time                                  = 0;
    p_rule->stage_idx                                         = 0;
    p_rule->num_stages                                        = OPT_RGB_CTRL_DIMMING_RULE_NUM_STAGES;
    p_rule->stages[0] = (opt_rgb_ctrl_led_stage_t){
        .stage_duration_ticks = k_ms_to_ticks_ceil32(MSEC_PER_SEC),
        .coord = {
            .color_with_brightness = *p_color_used,
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
    p_rule->num_stages                                        = OPT_RGB_CTRL_DIMMING_RULE_NUM_STAGES;
    const rgb_led_currents_and_pwms_t     currents_and_pwms_black = {
        .led_currents = {
            .current_red = 0,
            .current_green = 0,
            .current_blue = 0,
        },
        .led_pwms = {
            .pwm_red = 0,
            .pwm_green = 0,
            .pwm_blue = 0,
        },
    };
    const rgb_led_currents_and_pwms_t* const p_curs_pwms = prev_use_raw_currents_and_pwm ? p_prev_curs_pwms
                                                                                         : &currents_and_pwms_black;
    p_rule->stages[0] = (opt_rgb_ctrl_led_stage_t){
        .stage_duration_ticks = k_ms_to_ticks_ceil32(CONFIG_RUUVI_AIR_LED_DIMMING_INTERVAL_MS),
        .coord = {
            .currents_and_pwms = *p_curs_pwms,
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
float32_t
opt_rgb_ctrl_get_luminosity(void)
{
    if (app_settings_is_led_mode_auto())
    {
        return opt_rgb_ctrl_auto_get_luminosity();
    }
    else
    {
        return g_opt4060_luminosity_in_manual_mode;
    }
}
