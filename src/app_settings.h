/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_AIR_APP_SETTINGS_H
#define RUUVI_AIR_APP_SETTINGS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum app_settings_led_mode
{
    APP_SETTINGS_LED_MODE_DISABLED,
    APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY,
    APP_SETTINGS_LED_MODE_MANUAL_DAY,
    APP_SETTINGS_LED_MODE_MANUAL_NIGHT,
    APP_SETTINGS_LED_MODE_MANUAL_OFF,
    APP_SETTINGS_LED_MODE_AUTO,
};

bool
app_settings_init(void);

enum app_settings_led_mode
app_settings_get_led_mode(void);

bool
app_settings_is_led_mode_auto(void);

void
app_settings_set_led_mode(const enum app_settings_led_mode mode);

void
app_settings_set_next_led_mode(void);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_AIR_APP_SETTINGS_H
