/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef APP_BUTTON_H
#define APP_BUTTON_H

#include <stdbool.h>
#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

void
app_button_init(
    struct gpio_callback* const   p_gpio_callback,
    const gpio_callback_handler_t cb_handler,
    const gpio_flags_t            int_flags);

void
app_button_deinit(struct gpio_callback* const p_gpio_callback);

void
app_button_int_disable(void);

void
app_button_remove_cb(struct gpio_callback* const p_gpio_callback);

bool
app_button_get(void);

void
app_button_set_pressed(void);

void
app_button_clr_pressed(void);

bool
app_button_is_pressed(void);

#ifdef __cplusplus
}
#endif

#endif // APP_BUTTON_H
