/**
 * @file sen66_wrap.c
 * @author TheSomeMan
 * @date 2024-07-17
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "sen66_wrap.h"
#include <string.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sen66_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"
#include "ruuvi_endpoint_6.h"

LOG_MODULE_REGISTER(SEN66, LOG_LEVEL_DBG);

#define SEN66_WRAP_NUM_RETRIES (3U)

#define SEN66_SCALE_FACTOR_PM          (10)
#define SEN66_SCALE_FACTOR_HUMIDITY    (100)
#define SEN66_SCALE_FACTOR_TEMPERATURE (200)
#define SEN66_SCALE_FACTOR_VOC_INDEX   (10)
#define SEN66_SCALE_FACTOR_NOX_INDEX   (10)
#define SEN66_SCALE_FACTOR_CO2         (1)

bool
sen66_wrap_init_i2c(void)
{
    sensirion_i2c_hal_init();
    const int16_t err = sensirion_i2c_hal_select_bus(0);
    if (err != NO_ERROR)
    {
        LOG_ERR("sensirion_i2c_hal_select_bus failed, err=%d", err);
        return false;
    }
    sen66_init(SEN66_I2C_ADDR_6B);
    return true;
}

bool
sen66_wrap_device_reset(void)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_device_reset();
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_device_reset", i, error);
    }
    return flag_success;
}

static bool
sen66_wrap_get_serial_number(sen66_wrap_serial_number_t* const p_serial_num)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_get_serial_number(p_serial_num->serial_number, sizeof(p_serial_num->serial_number));
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_get_serial_number", i, error);
    }
    return flag_success;
}

static bool
sen66_wrap_get_product_name(sen66_wrap_product_name_t* const p_product_name)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_get_product_name(
            p_product_name->product_name,
            sizeof(p_product_name->product_name));
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_get_product_name", i, error);
    }
    p_product_name->product_name[sizeof(p_product_name->product_name) - 1] = '\0';
    return flag_success;
}

static bool
sen66_wrap_get_version(sen66_wrap_version_t* const p_ver)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        uint8_t       padding = 0;
        const int16_t error   = sen66_get_version(
            &p_ver->firmware_major,
            &p_ver->firmware_minor,
            &p_ver->firmware_debug,
            &p_ver->hardware_major,
            &p_ver->hardware_minor,
            &p_ver->protocol_major,
            &p_ver->protocol_minor,
            &padding);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_get_version", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_start_continuous_measurement(void)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_start_continuous_measurement();
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_start_continuous_measurement", i, error);
    }
    return flag_success;
}

static bool
sen66_wrap_read_data_ready(bool* const p_flag_data_ready)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        uint8_t       padding = 0;
        const int16_t error   = sen66_get_data_ready(&padding, p_flag_data_ready);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_read_data_ready", i, error);
    }
    return flag_success;
}

sen66_wrap_read_measurement_status_t
sen66_wrap_read_measured_values(sen66_wrap_measurement_t* const p_measurement)
{
    bool flag_data_ready = false;
    if (!sen66_wrap_read_data_ready(&flag_data_ready))
    {
        return SEN66_WRAP_READ_MEASUREMENT_STATUS_ERR;
    }
    if (!flag_data_ready)
    {
        return SEN66_WRAP_READ_MEASUREMENT_STATUS_DATA_NOT_READY;
    }

    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_read_measured_values_as_integers(
            &p_measurement->mass_concentration_pm1p0,
            &p_measurement->mass_concentration_pm2p5,
            &p_measurement->mass_concentration_pm4p0,
            &p_measurement->mass_concentration_pm10p0,
            &p_measurement->ambient_humidity,
            &p_measurement->ambient_temperature,
            &p_measurement->voc_index,
            &p_measurement->nox_index,
            &p_measurement->co2);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_read_measured_values_as_integers", i, error);
    }
    if (flag_success)
    {
        LOG_DBG(
            "PM=%u,%u,%u,%u/%u µg/m³, H=%d/%u %%RH, T=%d/%u °C, VOC=%d/%u, NOX=%d/%u, CO2=%u/%u ppm",
            p_measurement->mass_concentration_pm1p0,
            p_measurement->mass_concentration_pm2p5,
            p_measurement->mass_concentration_pm4p0,
            p_measurement->mass_concentration_pm10p0,
            SEN66_SCALE_FACTOR_PM,
            p_measurement->ambient_humidity,
            SEN66_SCALE_FACTOR_HUMIDITY,
            p_measurement->ambient_temperature,
            SEN66_SCALE_FACTOR_TEMPERATURE,
            p_measurement->voc_index,
            SEN66_SCALE_FACTOR_VOC_INDEX,
            p_measurement->nox_index,
            SEN66_SCALE_FACTOR_NOX_INDEX,
            p_measurement->co2,
            SEN66_SCALE_FACTOR_CO2);
    }
    return flag_success ? SEN66_WRAP_READ_MEASUREMENT_STATUS_OK : SEN66_WRAP_READ_MEASUREMENT_STATUS_ERR;
}

bool
sen66_wrap_set_temperature_offset(int16_t offset, int16_t slope, uint16_t time_constant, uint16_t slot)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_set_temperature_offset(offset, slope, time_constant, slot);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_set_temperature_offset", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_get_voc_algorithm_tuning_parameters(voc_algorithm_tuning_parameters_t* const p_tuning_params)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_get_voc_algorithm_tuning_parameters(p_tuning_params);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_get_voc_algorithm_tuning_parameters", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_set_voc_algorithm_tuning_parameters(const voc_algorithm_tuning_parameters_t* const p_tuning_params)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_set_voc_algorithm_tuning_parameters(p_tuning_params);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_set_voc_algorithm_tuning_parameters", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_get_ambient_pressure(uint16_t* const p_pressure_hpa)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_get_ambient_pressure(p_pressure_hpa);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_get_ambient_pressure", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_set_ambient_pressure(const uint16_t pressure_hpa)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_set_ambient_pressure(pressure_hpa);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_set_ambient_pressure", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_get_voc_algorithm_state(sen66_voc_algorithm_state_t* const p_state)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_get_voc_algorithm_state(p_state);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_get_ambient_pressure", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_set_voc_algorithm_state(const sen66_voc_algorithm_state_t* const p_state)
{
    bool flag_success = false;
    for (uint32_t i = 0; i < SEN66_WRAP_NUM_RETRIES; ++i)
    {
        const int16_t error = sen66_set_voc_algorithm_state(p_state);
        if (NO_ERROR == error)
        {
            flag_success = true;
            break;
        }
        LOG_ERR("%s[retry=%d]: err=%d", "sen66_set_ambient_pressure", i, error);
    }
    return flag_success;
}

bool
sen66_wrap_check(void)
{
    if (!sen66_wrap_device_reset())
    {
        LOG_ERR("%s failed", "sen66_wrap_device_reset");
        return false;
    }

    k_msleep(1200);

    sen66_wrap_serial_number_t serial_num = { 0 };
    if (!sen66_wrap_get_serial_number(&serial_num))
    {
        LOG_ERR("%s failed", "sen66_wrap_get_serial_number");
        return false;
    }
    LOG_INF("SEN66: Serial number: %s", serial_num.serial_number);

    sen66_wrap_product_name_t product_name = { 0 };
    if (!sen66_wrap_get_product_name(&product_name))
    {
        LOG_ERR("%s failed", "sen66_wrap_get_product_name");
        return false;
    }
    LOG_INF("SEN66: Product name: %.*s", (int)sizeof(product_name.product_name), product_name.product_name);

    sen66_wrap_version_t version = { 0 };
    if (!sen66_wrap_get_version(&version))
    {
        LOG_ERR("%s failed", "sen66_wrap_get_version");
        return false;
    }

    LOG_INF(
        "SEN66: Firmware: %u.%u, Hardware: %u.%u, Protocol: %u.%u",
        version.firmware_major,
        version.firmware_minor,
        version.hardware_major,
        version.hardware_minor,
        version.protocol_major,
        version.protocol_minor);

    if (0 != strcmp("SEN66", (const char*)product_name.product_name))
    {
        if ('\0' == product_name.product_name[0])
        {
            LOG_WRN("The sensor is not SEN66, product_name is empty");
        }
        else
        {
            LOG_ERR("The sensor is not SEN66, product_name: %s", product_name.product_name);
            return false;
        }
    }
    return true;
}

re_float
sen66_wrap_conv_raw_to_float_pm(const uint16_t raw_pm)
{
    if (SEN66_INVALID_RAW_VALUE_PM == raw_pm)
    {
        return NAN;
    }
    const re_float result = (float)raw_pm / SEN66_SCALE_FACTOR_PM;
    if (result < RE_6_PM_MIN)
    {
        return RE_6_PM_MIN;
    }
    if (result > RE_6_PM_MAX)
    {
        return RE_6_PM_MAX;
    }
    return result;
}

re_float
sen66_wrap_conv_raw_to_float_humidity(const int16_t raw_humidity)
{
    if (SEN66_INVALID_RAW_VALUE_HUMIDITY == raw_humidity)
    {
        return NAN;
    }
    const re_float result = (float)raw_humidity / SEN66_SCALE_FACTOR_HUMIDITY;
    if (result < RE_6_HUMIDITY_MIN)
    {
        return RE_6_HUMIDITY_MIN;
    }
    if (result > RE_6_HUMIDITY_MAX)
    {
        return RE_6_HUMIDITY_MAX;
    }
    return result;
}

re_float
sen66_wrap_conv_raw_to_float_temperature(const int16_t temperature)
{
    if (SEN66_INVALID_RAW_VALUE_TEMPERATURE == temperature)
    {
        return NAN;
    }
    const re_float result = (float)temperature / SEN66_SCALE_FACTOR_TEMPERATURE;
    if (result < -RE_6_TEMPERATURE_MAX)
    {
        return -RE_6_TEMPERATURE_MAX;
    }
    if (result > RE_6_TEMPERATURE_MAX)
    {
        return RE_6_TEMPERATURE_MAX;
    }
    return result;
}

re_float
sen66_wrap_conv_raw_to_float_voc_index(const int16_t raw_voc_index)
{
    if (SEN66_INVALID_RAW_VALUE_VOC == raw_voc_index)
    {
        return NAN;
    }
    const re_float result = (float)raw_voc_index / SEN66_SCALE_FACTOR_VOC_INDEX;
    if (result < RE_6_VOC_MIN)
    {
        return RE_6_VOC_MIN;
    }
    if (result > RE_6_VOC_MAX)
    {
        return RE_6_VOC_MAX;
    }
    return result;
}

re_float
sen66_wrap_conv_raw_to_float_nox_index(const int16_t raw_nox_index)
{
    if (SEN66_INVALID_RAW_VALUE_NOX == raw_nox_index)
    {
        return NAN;
    }
    const re_float result = (float)raw_nox_index / SEN66_SCALE_FACTOR_NOX_INDEX;
    if (result < RE_6_NOX_MIN)
    {
        return RE_6_NOX_MIN;
    }
    if (result > RE_6_NOX_MAX)
    {
        return RE_6_NOX_MAX;
    }
    return result;
}

re_float
sen66_wrap_conv_raw_to_float_co2(const uint16_t raw_co2)
{
    if (SEN66_INVALID_RAW_VALUE_CO2 == raw_co2)
    {
        return NAN;
    }
    const re_float result = (float)raw_co2 / SEN66_SCALE_FACTOR_CO2;
    if (result < RE_6_CO2_MIN)
    {
        return RE_6_CO2_MIN;
    }
    if (result > RE_6_CO2_MAX)
    {
        return RE_6_CO2_MAX;
    }
    return result;
}
