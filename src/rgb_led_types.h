/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_RGB_LED_TYPES_H
#define RUUVI_RGB_LED_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t rgb_led_color_val_t;  //<! Color value 0..255
typedef uint8_t rgb_led_brightness_t; //<! Brightness value 0..255
typedef uint8_t rgb_led_current_t;    //<! Current value 0..255

typedef struct rgb_led_color_t
{
    rgb_led_color_val_t red;   //<! Color value for red LED 0..255
    rgb_led_color_val_t green; //<! Color value for green LED 0..255
    rgb_led_color_val_t blue;  //<! Color value for blue LED 0..255
} rgb_led_color_t;

typedef uint8_t rgb_led_pwm_t; //<! PWM value 0..255

typedef struct rgb_led_pwms_t
{
    rgb_led_pwm_t pwm_red;   //<! PWM value for red LED 0..255
    rgb_led_pwm_t pwm_green; //<! PWM value for green LED 0..255
    rgb_led_pwm_t pwm_blue;  //<! PWM value for blue LED 0..255
} rgb_led_pwms_t;

typedef struct rgb_led_currents_t
{
    rgb_led_current_t current_red;   //<! Current value for red LED 0..255
    rgb_led_current_t current_green; //<! Current value for green LED 0..255
    rgb_led_current_t current_blue;  //<! Current value for blue LED 0..255
} rgb_led_currents_t;

typedef struct rgb_led_color_with_brightness_t
{
    rgb_led_color_t      rgb;
    rgb_led_brightness_t brightness; //<! Brightness value 0..255
} rgb_led_color_with_brightness_t;

typedef struct rgb_led_currents_and_pwms_t
{
    rgb_led_currents_t led_currents;
    rgb_led_pwms_t     led_pwms;
} rgb_led_currents_and_pwms_t;

typedef struct rgb_led_exp_current_coef_t
{
    uint8_t  current_min;
    uint8_t  current_max;
    uint16_t duration_ms;
    float    alpha;
} rgb_led_exp_current_coef_t;

typedef struct rgb_led_exp_current_coefs_t
{
    rgb_led_exp_current_coef_t coef_red;
    rgb_led_exp_current_coef_t coef_green;
    rgb_led_exp_current_coef_t coef_blue;
} rgb_led_exp_current_coefs_t;

#ifdef __cplusplus
}
#endif

#endif // RUUVI_RGB_LED_TYPES_H
