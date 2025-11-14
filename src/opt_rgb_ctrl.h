/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef OPT_RBG_CTRL_H
#define OPT_RBG_CTRL_H

#include <stdbool.h>
#include <stdint.h>
#include "rgb_led_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void
opt_rgb_ctrl_init(const rgb_led_exp_current_coefs_t* const p_led_currents_alpha);

void
opt_rgb_ctrl_measure_luminosity(void);

float
opt_rgb_ctrl_get_luminosity(void);

void
opt_rgb_ctrl_stop_bootup_led_fading(void);

void
opt_rgb_ctrl_set_next_brightnes_and_color(const rgb_led_brightness_t brightness, const rgb_led_color_t* const p_color);

void
opt_rgb_ctrl_set_next_color_black(void);

void
opt_rgb_ctrl_set_next_raw_currents_and_pwms(
    const rgb_led_currents_t* const p_rgb_led_currents,
    const rgb_led_pwms_t* const     p_rgb_led_pwms);

void
opt_rgb_ctrl_enable_led(const bool enable);

#ifdef __cplusplus
}
#endif

#endif // OPT_RBG_CTRL_H
