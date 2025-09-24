/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef AQI_H
#define AQI_H

#include <stdbool.h>
#include "rgb_led_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void
aqi_init(void);

const rgb_led_exp_current_coefs_t*
aqi_get_led_currents_alpha(void);

void
aqi_update_led(const float air_quality_index);

void
aqi_refresh_led(void);

void
aqi_recalc_auto_brightness_level(const float luminosity);

#ifdef __cplusplus
}
#endif

#endif // AQI_H
