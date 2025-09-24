/**
 * @file sen66_wrap.h
 * @author TheSomeMan
 * @date 2024-07-17
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_SEN66_WRAP_H
#define RUUVI_SEN66_WRAP_H

#include <stdbool.h>
#include <stdint.h>
#include "ruuvi_endpoint_6.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEN66_WRAP_SERIAL_NUMBER_SIZE (32U)
#define SEN66_WRAP_PRODUCT_NAME_SIZE  (32U)

#define SEN66_INVALID_RAW_VALUE_PM          (0xFFFFU)
#define SEN66_INVALID_RAW_VALUE_HUMIDITY    (0x7FFFU)
#define SEN66_INVALID_RAW_VALUE_TEMPERATURE (0x7FFFU)
#define SEN66_INVALID_RAW_VALUE_VOC         (0x7FFFU)
#define SEN66_INVALID_RAW_VALUE_NOX         (0x7FFFU)
#define SEN66_INVALID_RAW_VALUE_CO2         (0xFFFFU)

typedef struct sen5x_wrap_serial_number_t
{
    uint8_t serial_number[SEN66_WRAP_SERIAL_NUMBER_SIZE];
} sen66_wrap_serial_number_t;

typedef struct sen5x_wrap_product_name_t
{
    uint8_t product_name[SEN66_WRAP_PRODUCT_NAME_SIZE];
} sen66_wrap_product_name_t;

typedef struct sen5x_wrap_version_t
{
    uint8_t firmware_major;
    uint8_t firmware_minor;
    bool    firmware_debug;
    uint8_t hardware_major;
    uint8_t hardware_minor;
    uint8_t protocol_major;
    uint8_t protocol_minor;
} sen66_wrap_version_t;

typedef struct sen5x_wrap_measurement_t
{
    uint16_t mass_concentration_pm1p0;
    uint16_t mass_concentration_pm2p5;
    uint16_t mass_concentration_pm4p0;
    uint16_t mass_concentration_pm10p0;
    int16_t  ambient_humidity;
    int16_t  ambient_temperature;
    int16_t  voc_index;
    int16_t  nox_index;
    uint16_t co2;
} sen66_wrap_measurement_t;

typedef enum sen66_wrap_read_measurement_status_e
{
    SEN66_WRAP_READ_MEASUREMENT_STATUS_OK,
    SEN66_WRAP_READ_MEASUREMENT_STATUS_ERR,
    SEN66_WRAP_READ_MEASUREMENT_STATUS_DATA_NOT_READY,
} sen66_wrap_read_measurement_status_t;

bool
sen66_wrap_init_i2c(void);

bool
sen66_wrap_check(void);

bool
sen66_wrap_start_continuous_measurement(void);

sen66_wrap_read_measurement_status_t
sen66_wrap_read_measured_values(sen66_wrap_measurement_t* const p_measurement);

bool
sen66_wrap_set_temperature_offset(int16_t offset, int16_t slope, uint16_t time_constant, uint16_t slot);

bool
sen66_wrap_device_reset(void);

re_float
sen66_wrap_conv_raw_to_float_pm(const uint16_t raw_pm);

re_float
sen66_wrap_conv_raw_to_float_humidity(const int16_t raw_humidity);

re_float
sen66_wrap_conv_raw_to_float_temperature(const int16_t temperature);

re_float
sen66_wrap_conv_raw_to_float_voc_index(const int16_t raw_voc_index);

re_float
sen66_wrap_conv_raw_to_float_nox_index(const int16_t raw_nox_index);

re_float
sen66_wrap_conv_raw_to_float_co2(const uint16_t raw_co2);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_SEN66_WRAP_H
