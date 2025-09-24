/* Auto-generated from CSV; do not edit by hand. */
#ifndef LED_CALIBRATION_H
#define LED_CALIBRATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LED_CALIBRATION_BRIGHTNESS_STEPS (101u)

/* Brightness → current (LP5810 0..255) */
extern const uint8_t g_led_calibration_brightness_to_current_red[LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_current_green[LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_current_blue[LED_CALIBRATION_BRIGHTNESS_STEPS];

/* Brightness → PWM dim (0..255) */
extern const uint8_t g_led_calibration_brightness_to_pwm_red[LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_pwm_green[LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_pwm_blue[LED_CALIBRATION_BRIGHTNESS_STEPS];

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LED_CALIBRATION_H */
