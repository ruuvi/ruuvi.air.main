/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_AIR_APP_SETTINGS_H
#define RUUVI_AIR_APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sen66_i2c.h"
#include "aqi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SETTINGS_VAL_LED_BRIGHTNESS_OFF        "off"
#define APP_SETTINGS_VAL_LED_BRIGHTNESS_NIGHT      "night"
#define APP_SETTINGS_VAL_LED_BRIGHTNESS_DAY        "day"
#define APP_SETTINGS_VAL_LED_BRIGHTNESS_BRIGHT_DAY "bright_day"
#define APP_SETTINGS_VAL_LED_BRIGHTNESS_DISABLED   "disabled"
#define APP_SETTINGS_VAL_LED_BRIGHTNESS_AUTO       "auto"

#define APP_SETTINGS_SEN66_VOC_ALGORITHM_STATE_DEFAULT \
    { \
        0, 0, 50, 0 \
    }

#define APP_SETTINGS_LED_BRIGHTNESS_NIGHT_VALUE      (5)
#define APP_SETTINGS_LED_BRIGHTNESS_DAY_VALUE        (15)
#define APP_SETTINGS_LED_BRIGHTNESS_BRIGHT_DAY_VALUE (64)

enum app_settings_led_mode
{
    APP_SETTINGS_LED_MODE_DISABLED,
    APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY,
    APP_SETTINGS_LED_MODE_MANUAL_DAY,
    APP_SETTINGS_LED_MODE_MANUAL_NIGHT,
    APP_SETTINGS_LED_MODE_MANUAL_OFF,
    APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE,
    APP_SETTINGS_LED_MODE_AUTO,
};

typedef uint16_t app_settings_led_brightness_deci_percent_t;

typedef struct app_settings_sen66_voc_algorithm_state_t
{
    uint32_t                    unix_timestamp;
    sen66_voc_algorithm_state_t state;
} app_settings_sen66_voc_algorithm_state_t;

bool
app_settings_init(void);

enum app_settings_led_mode
app_settings_get_led_mode(void);

app_settings_led_brightness_deci_percent_t
app_settings_get_led_brightness_deci_percent(void);

bool
app_settings_is_led_mode_auto(void);

void
app_settings_set_led_mode(const enum app_settings_led_mode mode);

bool
app_settings_set_led_mode_manual_percentage(const char* const p_str_brightness_deci_percent);

void
app_settings_set_next_led_mode(void);

void
app_settings_save_sen66_voc_algorithm_state(uint32_t unix_timestamp, const sen66_voc_algorithm_state_t* const p_state);

app_settings_sen66_voc_algorithm_state_t
app_settings_get_sen66_voc_algorithm_state(void);

uint32_t
app_settings_get_sen66_voc_algorithm_state_timestamp(void);

void
app_settings_set_led_color_table(
    manual_brightness_level_e              brightness_level,
    const manual_brightness_color_t* const p_table);

void
app_settings_reset_led_color_table(manual_brightness_level_e brightness_level);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_AIR_APP_SETTINGS_H
