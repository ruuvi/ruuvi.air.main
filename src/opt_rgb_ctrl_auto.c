/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "opt_rgb_ctrl_auto.h"
#include "opt_rgb_ctrl.h"
#include <stdint.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <hal/nrf_twim.h>
#include "zephyr_api.h"
#include "rgb_led.h"
#include "tlog.h"

#define OPT_RGB_CTRL_DBG_LOG_ENABLED (0)

LOG_MODULE_DECLARE(opt_rgb_ctrl, LOG_LEVEL_INF);

#define NRF_TWIM_FREQ_390K 0x06200000UL

#define OPT_RGB_CTRL_MEASURE_SET_COLOR_NUM_CYCLES (50U)
#define OPT_RGB_CTRL_MEASURE_SET_COLOR_MAX_CYCLES (100U)

#define OPT_RGB_CTRL_MEASURE_GET_LUMINOSITY_NUM_CYCLES (50U)
#define OPT_RGB_CTRL_MEASURE_GET_LUMINOSITY_MAX_CYCLES (100U)

#define EVEN_DIVISOR     2U
#define MEDIAN_DIVISOR   2U
#define MEDIAN_DIVISOR_F 2.0f

#define MIN_VALID_DATA_POINTS_FOR_IQR 4U

#define USE_SENSOR_OPT4060 \
    (1 && IS_ENABLED(CONFIG_OPT4060) && IS_ENABLED(CONFIG_RUUVI_AIR_USE_SENSOR_OPT4060) \
     && DT_HAS_COMPAT_STATUS_OKAY(ti_opt4060))

#define LUMINOSITY_ARRAY_SIZE \
    (CONFIG_RUUVI_AIR_OPT4060_NUM_MEASUREMENTS_PER_SECOND * CONFIG_RUUVI_AIR_OPT4060_LUMINOSITY_AVG_PERIOD)

static float32_t g_opt4060_luminosity[LUMINOSITY_ARRAY_SIZE];
static float32_t g_opt4060_luminosity_tmp[LUMINOSITY_ARRAY_SIZE];
static int32_t   g_opt4060_luminosity_idx;

static int32_t g_rgb_led_set_color_delay_ticks      = 0;
static int32_t g_rgb_led_get_luminosity_delay_ticks = 0;

static int64_t g_dbg_time_green_channel_measured;
static int64_t g_dbg_cur_timestamp;
static int64_t g_dbg_next_timestamp;

#if USE_SENSOR_OPT4060
static const struct device* const dev_opt4060 = DEVICE_DT_GET_ONE(ti_opt4060);
#endif // USE_SENSOR_OPT4060

void
opt_rgb_ctrl_auto_init(void)
{
    for (int32_t i = 0; i < ARRAY_SIZE(g_opt4060_luminosity); ++i)
    {
        g_opt4060_luminosity[i] = NAN;
    }
    g_opt4060_luminosity_idx = 0;
}

/**
 * @brief Comparison function for qsort to sort floats in ascending order.
 * @param a A pointer to the first float32_t.
 * @param b A pointer to the second float32_t.
 * @return An integer less than, equal to, or greater than zero if the first argument is considered to be respectively
 * less than, equal to, or greater than the second.
 */
static int // NOSONAR: ANSI C99 compliant signature for qsort comparison function
compare_floats(const void* a, const void* b)
{
    const float32_t fa         = *(const float32_t*)a;
    const float32_t fb         = *(const float32_t*)b;
    const int32_t   is_greater = (fa > fb) ? 1 : 0;
    const int32_t   is_less    = (fa < fb) ? 1 : 0;
    return is_greater - is_less;
}

/**
 * @brief Calculates the median of a given array of floats.
 * @param p_arr The array of floats.
 * @param arr_size The number of elements in the array.
 * @return The median value.
 */
static float32_t
get_median(const float32_t* const p_arr, const size_t arr_size)
{
    if (0 == (arr_size % EVEN_DIVISOR))
    {
        return (p_arr[(arr_size / MEDIAN_DIVISOR) - 1] + p_arr[arr_size / MEDIAN_DIVISOR]) / MEDIAN_DIVISOR_F;
    }
    else
    {
        return p_arr[arr_size / MEDIAN_DIVISOR];
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
static float32_t
average_without_outliers(
    const float32_t* const p_arr,
    float32_t* const       p_arr_tmp,
    const size_t           arr_size,
    bool* const            p_flag_discarded)
{
    *p_flag_discarded = false;

    // 1. Count valid (non-NaN) values and copy them into the temporary array
    size_t valid_count = 0;
    for (size_t i = 0; i < arr_size; ++i)
    {
        if (!(bool)isnan(p_arr[i]))
        {
            p_arr_tmp[valid_count] = p_arr[i];
            valid_count += 1;
        }
    }

    if (valid_count < MIN_VALID_DATA_POINTS_FOR_IQR)
    {
        // Not enough data points to reliably calculate quartiles.
        return NAN;
    }

    // 2. Sort the valid data array
    qsort(p_arr_tmp, valid_count, sizeof(float32_t), &compare_floats);

    // 3. Calculate Q1, Q3, and IQR
    float32_t q1        = 0.0f;
    float32_t q3        = 0.0f;
    size_t    half_size = valid_count / EVEN_DIVISOR;
    if (0 == (valid_count % EVEN_DIVISOR))
    {
        // Even number of elements
        q1 = get_median(p_arr_tmp, half_size);
        q3 = get_median(p_arr_tmp + half_size, half_size);
    }
    else
    {
        // Odd number of elements
        q1 = get_median(p_arr_tmp, half_size);
        q3 = get_median((p_arr_tmp + half_size) + 1, half_size);
    }

    const float32_t iqr = q3 - q1;

    // 4. Define outlier bounds
    const float32_t upper_bound = q3 + (1.5f * iqr);

    // 5. Filter outliers and calculate sum
    float32_t sum         = 0.0f;
    size_t    clean_count = 0;
    for (size_t i = 0; i < valid_count; ++i)
    {
        if (p_arr_tmp[i] <= upper_bound)
        {
            sum += p_arr_tmp[i];
            clean_count += 1;
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
    return sum / (float32_t)clean_count;
}

float32_t
opt_rgb_ctrl_auto_get_luminosity(void)
{
    if (OPT_RGB_CTRL_DBG_LOG_ENABLED)
    {
        for (int i = 0; i < ARRAY_SIZE(g_opt4060_luminosity); ++i) // NOSONAR intentional debug stub
        {
            TLOG_INF("luminosity[%d] = %.3f", i, (double)g_opt4060_luminosity[i]);
        }
    }
    bool      flag_is_discarded = false;
    float32_t luminosity        = average_without_outliers(
        g_opt4060_luminosity,
        g_opt4060_luminosity_tmp,
        ARRAY_SIZE(g_opt4060_luminosity),
        &flag_is_discarded);
    if (OPT_RGB_CTRL_DBG_LOG_ENABLED)
    {
        if (flag_is_discarded) // NOSONAR intentional debug stub
        {
            TLOG_ERR("Some outliers were discarded when calculating average luminosity");
        }
        TLOG_WRN("Average luminosity = %.3f", (double)luminosity);
    }
    return luminosity;
}

static void
opt_rgb_ctrl_use_fast_speed_i2c(const bool use_fast_speed)
{
    static NRF_TWIM_Type* const g_p_twim = (NRF_TWIM_Type*)0x40003000;
    if (use_fast_speed)
    {
        /*
            https://docs.nordicsemi.com/bundle/errata_nRF52840_Rev3/page/ERR/
            nRF52840/Rev3/latest/anomaly_840_219.html#anomaly_840_219

            Symptom:
            The low period of the SCL clock is too short to meet the I2C specification at 400 kHz.
            The actual low period of the SCL clock is 1.25 µs while the I2C specification requires the SCL clock
            to have a minimum low period of 1.3 µs.

            Workaround:
            If communication does not work at 400 kHz with an I2C compatible device that requires the SCL clock
            to have a minimum low period of 1.3 µs, use 390 kHz instead of 400kHz by writing 0x06200000
            to the FREQUENCY register. With this setting, the SCL low period is greater than 1.3 µs.

            To set TWI frequency to 400 kHz, use constant NRF_TWIM_FREQ_400K defined in
            nrf_twim.h. Its value is 0x06400000UL.
        */
        nrf_twim_frequency_set(g_p_twim, NRF_TWIM_FREQ_390K); // Set TWI frequency to 390 kHz
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

    for (int32_t i = 0; i < OPT_RGB_CTRL_MEASURE_SET_COLOR_MAX_CYCLES; ++i)
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
    const int32_t max_time_per_call_ticks = (int32_t)(((time_accum + cnt) - 1) / cnt);
    *p_max_time_per_call_ticks            = max_time_per_call_ticks;

    return true;
}

#if USE_SENSOR_OPT4060
static bool
measure_opt4060_channel_get(int32_t* const p_max_time_per_call_ticks)
{
    struct sensor_value luminosity = { 0 };
    int64_t             time_accum = 0;
    int32_t             cnt        = 0;

    for (int32_t i = 0; i < OPT_RGB_CTRL_MEASURE_GET_LUMINOSITY_MAX_CYCLES; ++i)
    {
        const int64_t    time_start = k_uptime_ticks();
        zephyr_api_ret_t res        = sensor_channel_get(dev_opt4060, SENSOR_CHAN_LIGHT, &luminosity);
        const int64_t    time_end   = k_uptime_ticks();
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
    const int32_t max_time_per_call_ticks = (int32_t)(((time_accum + cnt) - 1) / cnt);
    *p_max_time_per_call_ticks            = max_time_per_call_ticks;
    return true;
}
#endif // USE_SENSOR_OPT4060

void
opt_rgb_ctrl_auto_measure_i2c_delays(void)
{
    opt_rgb_ctrl_use_fast_speed_i2c(true);
    if (rgb_led_is_lp5810_ready())
    {
        if (!measure_set_color_delay(&g_rgb_led_set_color_delay_ticks))
        {
            TLOG_ERR("Failed to measure rgb_led_set_color delay");
        }
        else
        {
            TLOG_INF("LP5810 set_color delay: %d ticks", g_rgb_led_set_color_delay_ticks);
        }
    }
#if USE_SENSOR_OPT4060
    if (opt_rgb_ctrl_is_opt4060_ready())
    {
        if (!measure_opt4060_channel_get(&g_rgb_led_get_luminosity_delay_ticks))
        {
            TLOG_ERR("Failed to measure rgb_led_get_luminosity delay");
        }
        else
        {
            TLOG_INF("OPT4060 get_luminosity delay: %d ticks", g_rgb_led_get_luminosity_delay_ticks);
        }
    }
#endif // USE_SENSOR_OPT4060
    opt_rgb_ctrl_use_fast_speed_i2c(false);
}

static opt_rgb_ctrl_error_e
wait_opt4060_green_channel_measured(
    opt4060_measurement_cnt_t* const p_cnt,
    int64_t* const                   p_time_green_channel_measured)
{
    float32_t                 val         = 0.0f;
    opt4060_measurement_cnt_t initial_cnt = 0;
    const uint32_t max_wait_time_ticks    = (k_us_to_ticks_ceil32(OPT4060_CONV_TIME_US) * (OPT4060_CHANNEL_NUM + 2))
                                         + (g_rgb_led_get_luminosity_delay_ticks * 2);

    int64_t time_start = k_uptime_ticks();
    while (1)
    {
        if (opt_rgb_ctrl_get_opt4060_measurement(SENSOR_CHAN_GREEN, &val, &initial_cnt))
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
        opt4060_measurement_cnt_t cnt   = 0;
        const bool                is_ok = opt_rgb_ctrl_get_opt4060_measurement(SENSOR_CHAN_GREEN, &val, &cnt);
        if (is_ok && (cnt != initial_cnt))
        {
            *p_time_green_channel_measured = k_uptime_ticks();
            *p_cnt                         = cnt;
            break;
        }
        if ((k_uptime_ticks() - time_start) > max_wait_time_ticks)
        {
            return OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_GREEN_CHANNEL_MEASUREMENT;
        }
    }
    return OPT_RGB_CTRL_ERROR_NONE;
}

static opt_rgb_ctrl_error_e
wait_opt4060_luminosity_channel_measured(const opt4060_measurement_cnt_t expected_cnt, float32_t* const p_val)
{
    const opt4060_measurement_cnt_t prev_cnt = (opt4060_measurement_cnt_t)(expected_cnt - 1)
                                               & OPT4060_MEASUREMENT_CNT_MASK;
    opt4060_measurement_cnt_t cnt = 0;

    const int64_t max_wait_time_ticks = (k_us_to_ticks_ceil32(OPT4060_CONV_TIME_US) * 2)
                                        + g_rgb_led_get_luminosity_delay_ticks;

    int64_t time_start = k_uptime_ticks();
    while (1)
    {
        if (opt_rgb_ctrl_get_opt4060_measurement(SENSOR_CHAN_LIGHT, p_val, &cnt))
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
    float32_t val = 0.0f;
    if (!opt_rgb_ctrl_get_opt4060_measurement(SENSOR_CHAN_LIGHT, &val, &cnt))
    {
        return OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL;
    }
    if (cnt != expected_cnt)
    {
        return OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_CNT_CHANGED;
    }
    if (0 != memcmp(&val, p_val, sizeof(val)))
    {
        return OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_VAL_CHANGED;
    }
    return OPT_RGB_CTRL_ERROR_NONE;
}

static opt_rgb_ctrl_error_e
check_opt4060_blue_channel_not_measured(const opt4060_measurement_cnt_t expected_cnt)
{
    const opt4060_measurement_cnt_t prev_cnt = (opt4060_measurement_cnt_t)(expected_cnt - 1)
                                               & OPT4060_MEASUREMENT_CNT_MASK;
    opt4060_measurement_cnt_t cnt = 0;

    float32_t val = 0.0f;
    if (!opt_rgb_ctrl_get_opt4060_measurement(SENSOR_CHAN_BLUE, &val, &cnt))
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
turn_off_led_and_measure_luminosity(float32_t* const p_luminosity, int64_t* const p_timestamp_led_turned_off)
{
    *p_luminosity = NAN;

    opt4060_measurement_cnt_t  cnt                         = 0;
    int64_t                    time_green_channel_measured = 0;
    const opt_rgb_ctrl_error_e err1 = wait_opt4060_green_channel_measured(&cnt, &time_green_channel_measured);
    if (OPT_RGB_CTRL_ERROR_NONE != err1)
    {
        return err1;
    }
    const int64_t next_timestamp = time_green_channel_measured + opt4060_get_one_measurement_duration_ticks(dev_opt4060)
                                   - g_rgb_led_set_color_delay_ticks            // NOSONAR
                                   - (2 * g_rgb_led_get_luminosity_delay_ticks) // NOSONAR
                                   - 15;                                        // NOSONAR

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

static opt_rgb_ctrl_error_e
measure_luminosity_with_led_locked(
    float32_t* const p_luminosity,
    int64_t* const   p_timestamp_led_turned_off,
    int64_t* const   p_timestamp_led_turned_on)
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
    float32_t* const p_luminosity,
    int64_t* const   p_timestamp_led_turned_off,
    int64_t* const   p_timestamp_led_turned_on)
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

void
opt_rgb_ctrl_auto_do_measure_luminosity(void)
{
    float32_t luminosity               = NAN;
    int64_t   timestamp_led_turned_off = 0;
    int64_t   timestamp_led_turned_on  = 0;

    const int64_t time_start = k_uptime_ticks();

    opt_rgb_ctrl_error_e err = OPT_RGB_CTRL_ERROR_NONE;
    if (opt_rgb_ctrl_is_opt4060_ready())
    {
        err = lock_led_and_measure_luminosity(&luminosity, &timestamp_led_turned_off, &timestamp_led_turned_on);
    }

    const int64_t time_finish = k_uptime_ticks();

    const int32_t duration_led_off_ticks = (int32_t)(timestamp_led_turned_on - timestamp_led_turned_off); // NOSONAR
    const int32_t duration_ticks         = (int32_t)(time_finish - time_start);                           // NOSONAR

    g_opt4060_luminosity[g_opt4060_luminosity_idx] = luminosity; // NAN will be saved if measure failed

    const int prev_luminosity_idx = g_opt4060_luminosity_idx; // NOSONAR
    g_opt4060_luminosity_idx      = (g_opt4060_luminosity_idx + 1) % ARRAY_SIZE(g_opt4060_luminosity);

    if (OPT_RGB_CTRL_DBG_LOG_ENABLED) // NOSONAR intentional debug stub
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
            opt4060_get_one_measurement_duration_ticks(dev_opt4060),
            g_rgb_led_set_color_delay_ticks,
            g_rgb_led_get_luminosity_delay_ticks,
            g_dbg_next_timestamp);
    }

    opt_rgb_ctrl_print_error_log(err);
}
