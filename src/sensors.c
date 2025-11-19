/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "sensors.h"
#include <assert.h>
#include <zephyr/sys/__assert.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys_clock.h>
#include "tlog.h"
#include "sen66_wrap.h"
#include "mic_pdm.h"
#include "opt_rgb_ctrl.h"
#include "app_settings.h"
#include "zephyr_api.h"

LOG_MODULE_REGISTER(sensors, LOG_LEVEL_INF);

#define USE_SENSOR_SEN66  (1 && IS_ENABLED(CONFIG_RUUVI_AIR_USE_SENSOR_SEN66))
#define USE_SENSOR_DPS310 (1 && IS_ENABLED(CONFIG_RUUVI_AIR_USE_SENSOR_DPS310))

#define SENSORS_POLL_RETRY_ON_NOT_READY_CNT_MAX (3U)

#if !defined(RUUVI_MOCK_MEASUREMENTS)
#define RUUVI_MOCK_MEASUREMENTS (0)
#endif

#if RUUVI_MOCK_MEASUREMENTS
#define RUUVI_MOCK_MEASUREMENT_BATTERY_VOLTAGE_MV (1314)
#define RUUVI_MOCK_MEASUREMENT_SEN66 \
    (sen66_wrap_measurement_t) \
    { \
        .mass_concentration_pm1p0 = 110, .mass_concentration_pm2p5 = 114, .mass_concentration_pm4p0 = 115, \
        .mass_concentration_pm10p0 = 116, .ambient_humidity = 5275, .ambient_temperature = 5662, .voc_index = 160, \
        .nox_index = 10, .co2 = 886, \
    }
#define RUUVI_MOCK_MEASUREMENT_DPS310_TEMPERATURE (27.521055f)
#define RUUVI_MOCK_MEASUREMENT_DPS310_PRESSURE    (100.827178f)
#define RUUVI_MOCK_MEASUREMENT_COLOR_R            (4531.0f)
#define RUUVI_MOCK_MEASUREMENT_COLOR_G            (3680.0f)
#define RUUVI_MOCK_MEASUREMENT_COLOR_B            (811.0f)
#define RUUVI_MOCK_MEASUREMENT_LUMINOSITY         (79.4f)
#define RUUVI_MOCK_MEASUREMENT_SOUND_DBA_INST     (30.0f)
#define RUUVI_MOCK_MEASUREMENT_SOUND_DBA_AVG      (55.0f)
#define RUUVI_MOCK_MEASUREMENT_SOUND_DBA_PEAK     (91.0f)
#endif /* RUUVI_MOCK_MEASUREMENTS */

#define SENSOR_VALUE_FRACTIONAL_PART_MULTIPLIER (1000000.0f)

typedef struct sensor_opt4060_measurement_t
{
    float32_t luminosity;
    uint8_t   luminosity_cnt;
} sensor_opt4060_measurement_t;

static K_MUTEX_DEFINE(sensors_poll_mutex);

static sensors_measurement_t g_measurements = {
    .sen66 = {
        .mass_concentration_pm1p0  = SEN66_INVALID_RAW_VALUE_PM,
        .mass_concentration_pm2p5  = SEN66_INVALID_RAW_VALUE_PM,
        .mass_concentration_pm4p0  = SEN66_INVALID_RAW_VALUE_PM,
        .mass_concentration_pm10p0 = SEN66_INVALID_RAW_VALUE_PM,
        .ambient_humidity          = SEN66_INVALID_RAW_VALUE_HUMIDITY,
        .ambient_temperature       = SEN66_INVALID_RAW_VALUE_TEMPERATURE,
        .voc_index                 = SEN66_INVALID_RAW_VALUE_VOC,
        .nox_index                 = SEN66_INVALID_RAW_VALUE_NOX,
        .co2                       = SEN66_INVALID_RAW_VALUE_CO2,
    },
    .dps310_temperature = NAN,
    .dps310_pressure = NAN,
    .shtc3_temperature = NAN,
    .shtc3_humidity = NAN,
    .luminosity = NAN,
    .sound_inst_dba = NAN,
    .sound_avg_dba = NAN,
    .sound_peak_spl_db = NAN,
};
static time_t g_nox_valid_timestamp;

#if USE_SENSOR_DPS310
const struct device* const dev_dps310 = DEVICE_DT_GET_ONE(infineon_dps310);
#endif // USE_SENSOR_DPS310

#if USE_SENSOR_SEN66

static uint32_t                                 g_sensors_poll_not_ready_cnt;
static app_settings_sen66_voc_algorithm_state_t g_voc_alg_state;
static K_MUTEX_DEFINE(g_voc_alg_state_mutex);

static void
sensors_save_to_cache_sen66_voc_algorithm_state(
    const uint32_t                           cur_unix_time32,
    const sen66_voc_algorithm_state_t* const p_voc_alg_state)
{
    k_mutex_lock(&g_voc_alg_state_mutex, K_FOREVER);
    g_voc_alg_state.unix_timestamp = cur_unix_time32;
    g_voc_alg_state.state          = *p_voc_alg_state;
    k_mutex_unlock(&g_voc_alg_state_mutex);
}

void
sensors_get_from_cache_sen66_voc_algorithm_state(
    uint32_t* const                    p_cur_unix_time32,
    sen66_voc_algorithm_state_t* const p_voc_alg_state)
{
    k_mutex_lock(&g_voc_alg_state_mutex, K_FOREVER);
    *p_cur_unix_time32 = g_voc_alg_state.unix_timestamp;
    *p_voc_alg_state   = g_voc_alg_state.state;
    k_mutex_unlock(&g_voc_alg_state_mutex);
}

static sen66_wrap_measurement_t
init_sen66_invalid_measurement(void)
{
    const sen66_wrap_measurement_t measurement = {
        .mass_concentration_pm1p0  = SEN66_INVALID_RAW_VALUE_PM,
        .mass_concentration_pm2p5  = SEN66_INVALID_RAW_VALUE_PM,
        .mass_concentration_pm4p0  = SEN66_INVALID_RAW_VALUE_PM,
        .mass_concentration_pm10p0 = SEN66_INVALID_RAW_VALUE_PM,
        .ambient_humidity          = SEN66_INVALID_RAW_VALUE_HUMIDITY,
        .ambient_temperature       = SEN66_INVALID_RAW_VALUE_TEMPERATURE,
        .voc_index                 = SEN66_INVALID_RAW_VALUE_VOC,
        .nox_index                 = SEN66_INVALID_RAW_VALUE_NOX,
        .co2                       = SEN66_INVALID_RAW_VALUE_CO2,
    };
    return measurement;
}

static void
sensors_reinit_sen66_temperature_offset(void)
{
    const int16_t  offset        = -600; // -1.5C (-600 / 400 = -1.5, it seems that the value 200 in docs is incorrect)
    const int16_t  slope         = 0;    // 0 / 10000 = 0.0
    const uint16_t time_constant = 0;
    const uint16_t slot          = 0;
    LOG_INF("SEN66: Set temperature offset: %d (%.1f)", (int)offset, (double)(offset / 400.0f));
    if (!sen66_wrap_set_temperature_offset(offset, slope, time_constant, slot))
    {
        LOG_ERR("%s failed", "sen66_wrap_set_temperature_offset");
    }
}

static void
sensors_reinit_sen66_voc_alg_tuning_params(void)
{
    LOG_INF("SEN66: Read VOC algorithm tuning parameters:");
    voc_algorithm_tuning_parameters_t voc_alg_tuning_params = { 0 };
    if (!sen66_wrap_get_voc_algorithm_tuning_parameters(&voc_alg_tuning_params))
    {
        LOG_ERR("%s failed", "sen66_wrap_get_voc_algorithm_tuning_parameters");
        return;
    }
    LOG_INF("- index_offset                : %d", (int)voc_alg_tuning_params.index_offset);
    LOG_INF("- learning_time_offset_hours  : %d", (int)voc_alg_tuning_params.learning_time_offset_hours);
    LOG_INF("- learning_time_gain_hours    : %d", (int)voc_alg_tuning_params.learning_time_gain_hours);
    LOG_INF("- gating_max_duration_minutes : %d", (int)voc_alg_tuning_params.gating_max_duration_minutes);
    LOG_INF("- std_initial                 : %d", (int)voc_alg_tuning_params.std_initial);
    LOG_INF("- gain_factor                 : %d", (int)voc_alg_tuning_params.gain_factor);

    voc_alg_tuning_params.learning_time_offset_hours
        = CONFIG_RUUVI_AIR_SEN66_VOC_ALG_TUNING_PARAMS_LEARNING_TIME_OFFSET_HOURS;
    voc_alg_tuning_params.learning_time_gain_hours
        = CONFIG_RUUVI_AIR_SEN66_VOC_ALG_TUNING_PARAMS_LEARNING_TIME_GAIN_HOURS;

    LOG_INF("SEN66: Set VOC algorithm tuning parameters:");
    LOG_INF("- learning_time_offset_hours  : %d", (int)voc_alg_tuning_params.learning_time_offset_hours);
    LOG_INF("- learning_time_gain_hours    : %d", (int)voc_alg_tuning_params.learning_time_gain_hours);
    if (!sen66_wrap_set_voc_algorithm_tuning_parameters(&voc_alg_tuning_params))
    {
        LOG_ERR("%s failed", "sen66_wrap_set_voc_algorithm_tuning_parameters");
        return;
    }
}

static void
sensors_reinit_sen66_voc_alg_state(void)
{
    sen66_voc_algorithm_state_t voc_alg_state = (sen66_voc_algorithm_state_t) {
        .voc_state = APP_SETTINGS_SEN66_VOC_ALGORITHM_STATE_DEFAULT,
    };
    if (!sen66_wrap_get_voc_algorithm_state(&voc_alg_state))
    {
        LOG_ERR("%s failed", "sen66_wrap_get_voc_algorithm_state");
    }
    else
    {
        LOG_INF(
            "SEN66: Initial VOC algorithm state: %u, %u, %u, %u",
            voc_alg_state.voc_state[0],
            voc_alg_state.voc_state[1],
            voc_alg_state.voc_state[2],
            voc_alg_state.voc_state[3]);
    }
    const app_settings_sen66_voc_algorithm_state_t settings_voc_alg_state
        = app_settings_get_sen66_voc_algorithm_state();
    LOG_INF(
        "SEN66: Loaded VOC algorithm state from settings: timestamp=%u, state: %u, %u, %u, %u",
        settings_voc_alg_state.unix_timestamp,
        settings_voc_alg_state.state.voc_state[0],
        settings_voc_alg_state.state.voc_state[1],
        settings_voc_alg_state.state.voc_state[2],
        settings_voc_alg_state.state.voc_state[3]);
    const time_t  cur_unix_time = time(NULL);
    const int32_t delta_time    = (int32_t)(cur_unix_time - settings_voc_alg_state.unix_timestamp);
    LOG_INF(
        "SEN66: Current unix time: %u, last saved VOC algorithm state timestamp: %u, delta: %d sec",
        (unsigned)cur_unix_time,
        (unsigned)settings_voc_alg_state.unix_timestamp,
        (int)delta_time);
    // Restore last saved VOC algorithm state if it is not too old
    // After factory reset the time will be reset, so delta_time will be negative
    if ((delta_time > 0) && (delta_time < CONFIG_RUUVI_AIR_SEN66_VOC_ALG_STATE_RECOVERY_TIMEOUT))
    {
        sensors_save_to_cache_sen66_voc_algorithm_state(
            settings_voc_alg_state.unix_timestamp,
            &settings_voc_alg_state.state);
        if (0 != memcmp(&voc_alg_state, &settings_voc_alg_state.state, sizeof(voc_alg_state)))
        {
            LOG_INF(
                "SEN66: Restore last saved VOC algorithm state: %u, %u, %u, %u",
                settings_voc_alg_state.state.voc_state[0],
                settings_voc_alg_state.state.voc_state[1],
                settings_voc_alg_state.state.voc_state[2],
                settings_voc_alg_state.state.voc_state[3]);
            if (!sen66_wrap_set_voc_algorithm_state(&settings_voc_alg_state.state))
            {
                LOG_ERR("%s failed", "sen66_wrap_set_voc_algorithm_state");
            }
        }
        else
        {
            LOG_INF("SEN66: Current VOC algorithm state matches the saved one, no need to restore");
        }
    }
    else
    {
        LOG_INF(
            "SEN66: Not restoring VOC algorithm state, saved timestamp=%u, current time=%u, delta=%d sec is out of "
            "range (0..%d)",
            (unsigned)settings_voc_alg_state.unix_timestamp,
            (unsigned)cur_unix_time,
            (int)delta_time,
            CONFIG_RUUVI_AIR_SEN66_VOC_ALG_STATE_RECOVERY_TIMEOUT);
        sensors_save_to_cache_sen66_voc_algorithm_state((uint32_t)cur_unix_time, &voc_alg_state);
    }
}

static void
sensors_reinit_sen66(void)
{
    LOG_INF("Reinitialize SEN66");
    if (!sen66_wrap_device_reset())
    {
        LOG_ERR("%s failed", "sen66_wrap_device_reset");
        k_msleep(MSEC_PER_SEC);
        return;
    }
    sensors_reinit_sen66_temperature_offset();
    sensors_reinit_sen66_voc_alg_tuning_params();
    sensors_reinit_sen66_voc_alg_state();

    LOG_INF("SEN66: Start continuous measurement");
    if (!sen66_wrap_start_continuous_measurement())
    {
        LOG_ERR("%s failed", "sen66_wrap_start_continuous_measurement");
        return;
    }
    LOG_INF("SEN66 reinitialized");
}
#endif // USE_SENSOR_SEN66

bool
sensors_init(void)
{
#if USE_SENSOR_DPS310
    LOG_INF("Check DPS310");
    if (!device_is_ready(dev_dps310))
    {
        LOG_ERR("Device %s is not ready", dev_dps310->name);
    }
    else
    {
        LOG_INF("Device %p: name %s", dev_dps310, dev_dps310->name);
    }
#endif

#if USE_SENSOR_SEN66
    if (!sen66_wrap_init_i2c())
    {
        LOG_ERR("sen66_wrap_init_i2c failed");
        return false;
    }
    LOG_INF("sen66_wrap_init_i2c ok");

    if (!sen66_wrap_check())
    {
        LOG_ERR("sen66_wrap_check failed");
    }
    else
    {
        LOG_INF("sen66_wrap_check ok");
    }

    sensors_reinit_sen66();
#endif

    return true;
}

void
sensors_reinit(void)
{
#if USE_SENSOR_SEN66
    sensors_reinit_sen66();
#endif
}

#if USE_SENSOR_SEN66
static void
sensors_save_measurement_sen66(const sen66_wrap_measurement_t* const p_sen66)
{
    k_mutex_lock(&sensors_poll_mutex, K_FOREVER);
    g_measurements.sen66 = *p_sen66;
    if ((SEN66_INVALID_RAW_VALUE_NOX != p_sen66->nox_index)
        && ((SEN66_MIN_VALID_RAW_VALUE_NOX <= p_sen66->nox_index)
            && (p_sen66->nox_index <= SEN66_MAX_VALID_RAW_VALUE_NOX)))
    {
        const time_t cur_time = time(NULL);
        if (0 == g_nox_valid_timestamp)
        {
            TLOG_INF("SEN66: NOx valid timestamp updated: %u", (unsigned)cur_time);
            g_nox_valid_timestamp = cur_time;
        }
        if ((cur_time - g_nox_valid_timestamp) < CONFIG_RUUVI_AIR_SEN66_NOX_CALIBRATION_TIMEOUT)
        {
            g_measurements.flag_nox_calibration_in_progress = true;
        }
        else
        {
            g_measurements.flag_nox_calibration_in_progress = false;
        }
    }
    else
    {
        g_nox_valid_timestamp                           = 0;
        g_measurements.flag_nox_calibration_in_progress = true;
    }
    k_mutex_unlock(&sensors_poll_mutex);
}
#endif

#if USE_SENSOR_DPS310
static void
sensors_save_measurement_dps310(const float32_t temperature, const float32_t pressure)
{
    k_mutex_lock(&sensors_poll_mutex, K_FOREVER);
    g_measurements.dps310_temperature = temperature;
    g_measurements.dps310_pressure    = pressure;
    k_mutex_unlock(&sensors_poll_mutex);
}
#endif

static void
sensors_save_measurement_luminosity(const float32_t luminosity)
{
    k_mutex_lock(&sensors_poll_mutex, K_FOREVER);
    g_measurements.luminosity = luminosity;
    k_mutex_unlock(&sensors_poll_mutex);
}

static void
sensors_save_measurement_sound_dba(
    const float32_t sound_inst_dba,
    const float32_t sound_avg_dba,
    const float32_t sound_peak_spl_db)
{
    k_mutex_lock(&sensors_poll_mutex, K_FOREVER);
    g_measurements.sound_inst_dba    = sound_inst_dba;
    g_measurements.sound_avg_dba     = sound_avg_dba;
    g_measurements.sound_peak_spl_db = sound_peak_spl_db;
    k_mutex_unlock(&sensors_poll_mutex);
}

sensors_measurement_t
sensors_get_measurement(void)
{
    k_mutex_lock(&sensors_poll_mutex, K_FOREVER);
    const sensors_measurement_t measurement = g_measurements;
    k_mutex_unlock(&sensors_poll_mutex);
    return measurement;
}

float32_t
sensors_get_air_quality_index(void)
{
    k_mutex_lock(&sensors_poll_mutex, K_FOREVER);
    const float32_t air_quality_index = g_measurements.air_quality_index;
    k_mutex_unlock(&sensors_poll_mutex);
    return air_quality_index;
}

static struct sensor_value
conv_float_to_sensor_value(const float32_t val)
{
    if (isnan(val))
    {
        struct sensor_value sensor_val = {
            .val1 = INT32_MAX,
            .val2 = INT32_MAX,
        };
        return sensor_val;
    }
    else
    {
        struct sensor_value sensor_val = { 0 };
        float32_t           int_part   = 0.0f;
        const float32_t     fract_part = modff(val, &int_part);
        sensor_val.val1                = (int32_t)int_part;
        sensor_val.val2                = (int32_t)lrintf(fract_part * SENSOR_VALUE_FRACTIONAL_PART_MULTIPLIER);
        return sensor_val;
    }
}

static inline float32_t
conv_sensor_value_to_float(const struct sensor_value* const p_val)
{
    if ((INT32_MAX == p_val->val1) && (INT32_MAX == p_val->val2))
    {
        return NAN;
    }
    return sensor_value_to_float(p_val);
}

static sen66_wrap_read_measurement_status_e
sensor_poll_sen66(const time_t cur_unix_time)
{
#if USE_SENSOR_SEN66
#if !RUUVI_MOCK_MEASUREMENTS
    sen66_wrap_measurement_t             measurement   = { 0 };
    sen66_wrap_read_measurement_status_e status        = sen66_wrap_read_measured_values(&measurement);
    sen66_voc_algorithm_state_t          voc_alg_state = { 0 };
    if (!sen66_wrap_get_voc_algorithm_state(&voc_alg_state))
    {
        TLOG_ERR("%s failed", "sen66_wrap_get_voc_algorithm_state");
    }
    else
    {
        TLOG_INF(
            "SEN66: VOC algorithm state: %u, %u, %u, %u",
            voc_alg_state.voc_state[0],
            voc_alg_state.voc_state[1],
            voc_alg_state.voc_state[2],
            voc_alg_state.voc_state[3]);
        sensors_save_to_cache_sen66_voc_algorithm_state((uint32_t)cur_unix_time, &voc_alg_state);
        if ((cur_unix_time - app_settings_get_sen66_voc_algorithm_state_timestamp())
            >= CONFIG_RUUVI_AIR_SEN66_VOC_ALG_STATE_SAVING_INTERVAL)
        {
            app_settings_save_sen66_voc_algorithm_state((uint32_t)cur_unix_time, &voc_alg_state);
        }
    }
#else
    sen66_wrap_measurement_t             measurement = RUUVI_MOCK_MEASUREMENT_SEN66;
    sen66_wrap_read_measurement_status_e status      = SEN66_WRAP_READ_MEASUREMENT_STATUS_OK;
#endif
    switch (status)
    {
        case SEN66_WRAP_READ_MEASUREMENT_STATUS_OK:
            TLOG_INF(
                "SEN66: PM1.0: %u (%f), PM2.5: %u (%f), PM4.0: %u (%f), PM10.0: %u (%f)",
                measurement.mass_concentration_pm1p0,
                (double)sen66_wrap_conv_raw_to_float_pm(measurement.mass_concentration_pm1p0),
                measurement.mass_concentration_pm2p5,
                (double)sen66_wrap_conv_raw_to_float_pm(measurement.mass_concentration_pm2p5),
                measurement.mass_concentration_pm4p0,
                (double)sen66_wrap_conv_raw_to_float_pm(measurement.mass_concentration_pm4p0),
                measurement.mass_concentration_pm10p0,
                (double)sen66_wrap_conv_raw_to_float_pm(measurement.mass_concentration_pm10p0));
            TLOG_INF(
                "SEN66: temperature: %d (%f); humidity: %d (%f), VOC: %d (%f), NOx: %d (%f), CO2: %u",
                measurement.ambient_temperature,
                (double)sen66_wrap_conv_raw_to_float_temperature(measurement.ambient_temperature),
                measurement.ambient_humidity,
                (double)sen66_wrap_conv_raw_to_float_humidity(measurement.ambient_humidity),
                measurement.voc_index,
                (double)sen66_wrap_conv_raw_to_float_voc_index(measurement.voc_index),
                measurement.nox_index,
                (double)sen66_wrap_conv_raw_to_float_nox_index(measurement.nox_index),
                measurement.co2);
            break;
        case SEN66_WRAP_READ_MEASUREMENT_STATUS_ERR:
            LOG_ERR("sen66_wrap_read_measured_values failed");
            measurement = init_sen66_invalid_measurement();
            break;
        case SEN66_WRAP_READ_MEASUREMENT_STATUS_DATA_NOT_READY:
            LOG_ERR("sen66_wrap_read_measured_values data not ready");
            g_sensors_poll_not_ready_cnt += 1;
            if (g_sensors_poll_not_ready_cnt < SENSORS_POLL_RETRY_ON_NOT_READY_CNT_MAX)
            {
                return SEN66_WRAP_READ_MEASUREMENT_STATUS_DATA_NOT_READY;
            }
            status      = SEN66_WRAP_READ_MEASUREMENT_STATUS_ERR;
            measurement = init_sen66_invalid_measurement();
            break;
    }
    sensors_save_measurement_sen66(&measurement);
    g_sensors_poll_not_ready_cnt = 0;
#else
    const sen66_wrap_read_measurement_status_e status = SENSORS_POLL_RESULT_OK;
#endif
    return status;
}

static void
sensor_poll_dps310(void)
{
#if USE_SENSOR_DPS310
    const bool mock_enabled = (RUUVI_MOCK_MEASUREMENTS != 0);
    if ((!mock_enabled) && (!device_is_ready(dev_dps310)))
    {
        return;
    }
    struct sensor_value temperature = conv_float_to_sensor_value(NAN);
    struct sensor_value pressure    = conv_float_to_sensor_value(NAN);

#if !RUUVI_MOCK_MEASUREMENTS
    zephyr_api_ret_t res = sensor_sample_fetch(dev_dps310);
    if (0 != res)
    {
        TLOG_ERR("sensor_sample_fetch failed for %s: %d", dev_dps310->name, res);
    }
    else
    {
        sensor_channel_get(dev_dps310, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
        sensor_channel_get(dev_dps310, SENSOR_CHAN_PRESS, &pressure);
    }
#else
    temperature = conv_float_to_sensor_value(RUUVI_MOCK_MEASUREMENT_DPS310_TEMPERATURE);
    pressure    = conv_float_to_sensor_value(RUUVI_MOCK_MEASUREMENT_DPS310_PRESSURE);
#endif

    const float32_t temperature_f = conv_sensor_value_to_float(&temperature);
    const float32_t pressure_f    = conv_sensor_value_to_float(&pressure) * 1000.0f;
    TLOG_INF(
        "DPS310: temperature: %d.%06d (%f); pressure: %d.%06d (%f)",
        temperature.val1,
        abs(temperature.val2),
        (double)temperature_f,
        pressure.val1,
        pressure.val2,
        (double)pressure_f);
    sensors_save_measurement_dps310(temperature_f, pressure_f);

#if USE_SENSOR_SEN66 && !RUUVI_MOCK_MEASUREMENTS
    if (!(bool)isnan(pressure_f))
    {
        const float32_t pressure_f_hpa = pressure_f / 100.0f;
        if ((pressure_f_hpa >= SEN66_AMBIENT_PRESSURE_MIN_HPA) && (pressure_f_hpa <= SEN66_AMBIENT_PRESSURE_MAX_HPA))
        {
            const uint16_t pressure_hpa = (uint16_t)lrintf(pressure_f / 100.0f);
            TLOG_INF("SEN66: Set ambient pressure: %u hPa", (unsigned)pressure_hpa);
            if (!sen66_wrap_set_ambient_pressure(pressure_hpa))
            {
                LOG_ERR("%s failed", "sen66_wrap_set_ambient_pressure");
            }
        }
        else
        {
            TLOG_WRN(
                "DPS310: pressure out of range for SEN66: %f hPa, limit to valid range [%d..%d] hPa",
                (double)pressure_f_hpa,
                SEN66_AMBIENT_PRESSURE_MIN_HPA,
                SEN66_AMBIENT_PRESSURE_MAX_HPA);
        }
    }
#endif // USE_SENSOR_SEN66 && !RUUVI_MOCK_MEASUREMENTS
#endif // USE_SENSOR_DPS310
}

static void
sensor_poll_opt4060(void)
{
    const float32_t luminosity = opt_rgb_ctrl_get_luminosity() * CONFIG_RUUVI_AIR_OPT4060_LUMINOSITY_MULTIPLIER;
    TLOG_INF("OPT4060: luminosity: %.03f", (double)luminosity);
    sensors_save_measurement_luminosity(luminosity);
}

static void
sensor_poll_sound(void)
{
#if !RUUVI_MOCK_MEASUREMENTS
    spl_db_t sound_inst_dba    = 0;
    spl_db_t sound_avg_dba     = 0;
    spl_db_t sound_peak_spl_db = 0;
    mic_pdm_get_measurements(&sound_inst_dba, &sound_avg_dba, &sound_peak_spl_db);
    const float32_t sound_inst_dba_f32    = (SPL_DB_INVALID != sound_inst_dba) ? (float32_t)sound_inst_dba : NAN;
    const float32_t sound_avg_dba_f32     = (SPL_DB_INVALID != sound_avg_dba) ? (float32_t)sound_avg_dba : NAN;
    const float32_t sound_peak_spl_db_f32 = (SPL_DB_INVALID != sound_peak_spl_db) ? (float32_t)sound_peak_spl_db : NAN;
#else
    const float32_t sound_inst_dba_f32    = RUUVI_MOCK_MEASUREMENT_SOUND_DBA_INST;
    const float32_t sound_avg_dba_f32     = RUUVI_MOCK_MEASUREMENT_SOUND_DBA_AVG;
    const float32_t sound_peak_spl_db_f32 = RUUVI_MOCK_MEASUREMENT_SOUND_DBA_PEAK;
#endif

    if (!(bool)IS_ENABLED(CONFIG_RUUVI_AIR_MIC_NONE))
    {
        LOG_INF(
            "Sound: inst=%f dBA, avg=%f dBA, peak=%f SPL dB",
            (double)sound_inst_dba_f32,
            (double)sound_avg_dba_f32,
            (double)sound_peak_spl_db_f32);
    }
    sensors_save_measurement_sound_dba(sound_inst_dba_f32, sound_avg_dba_f32, sound_peak_spl_db_f32);
}

sensors_poll_result_e
sensors_poll(const time_t cur_unix_time)
{
    const sen66_wrap_read_measurement_status_e status = sensor_poll_sen66(cur_unix_time);
    switch (status)
    {
        case SEN66_WRAP_READ_MEASUREMENT_STATUS_OK:
            sensor_poll_dps310();
            sensor_poll_opt4060();
            sensor_poll_sound();
            return SENSORS_POLL_RESULT_OK;
        case SEN66_WRAP_READ_MEASUREMENT_STATUS_ERR:
            return SENSORS_POLL_RESULT_ERR;
        case SEN66_WRAP_READ_MEASUREMENT_STATUS_DATA_NOT_READY:
            return SENSORS_POLL_RESULT_NOT_READY;
        default:
            TLOG_ERR("Unknown sensor_poll_sen66 status: %d", (int)status);
            __ASSERT(0, "Unknown sensor_poll_sen66 status: %d", (int)status);
            return SENSORS_POLL_RESULT_ERR;
    }
}
