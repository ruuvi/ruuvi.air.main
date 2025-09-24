/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_RGB_LED_H
#define RUUVI_RGB_LED_H

#include <stdbool.h>
#include "rgb_led_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
rgb_led_init(const rgb_led_brightness_t brightness);

void
rgb_led_deinit(void);

bool
rgb_led_set_color(const rgb_led_color_t* const p_color);

bool
rgb_led_set_color_black(void);

bool
rgb_led_set_brightness(const rgb_led_brightness_t brightness);

bool
rgb_led_set_brightness_and_color(const rgb_led_brightness_t brightness, const rgb_led_color_t* const p_color);

bool
rgb_led_set_raw_currents_and_pwms(
    const rgb_led_currents_t* const p_rgb_led_currents,
    const rgb_led_pwms_t* const     p_rgb_led_pwms);

void
rgb_led_lock(void);

void
rgb_led_unlock(void);

bool
rgb_led_read_raw_pwms(rgb_led_pwms_t* const p_pwms);

bool
rgb_led_write_raw_pwms(const rgb_led_pwms_t* const p_pwms);

bool
rgb_led_check_and_reinit_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_RGB_LED_H
