/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(APP_LED_H)
#define APP_LED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void
app_led_early_init(void);

void
app_led_late_init_pwm(void);

void
app_led_deinit(void);

void
app_led_red_set(const bool is_on);

static inline void
app_led_red_on(void)
{
    app_led_red_set(true);
}

static inline void
app_led_red_off(void)
{
    app_led_red_set(false);
}

void
app_led_green_set(const bool is_on);

static inline void
app_led_green_on(void)
{
    app_led_green_set(true);
}

static inline void
app_led_green_off(void)
{
    app_led_green_set(false);
}

void
app_led_mutex_lock(void);

void
app_led_mutex_unlock(void);

void
app_led_green_set_if_button_is_not_pressed(const bool is_on);

#ifdef __cplusplus
}
#endif

#endif // APP_LED_H
