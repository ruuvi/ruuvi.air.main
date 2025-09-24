/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef APP_GPIO_INPUT_H
#define APP_GPIO_INPUT_H

#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

void
app_gpio_input_init(
    const struct gpio_dt_spec* const p_gpio_dt,
    gpio_flags_t                     extra_flags,
    struct gpio_callback* const      p_gpio_callback,
    const gpio_callback_handler_t    cb_handler,
    const gpio_flags_t               int_flags);

#ifdef __cplusplus
}
#endif

#endif // APP_GPIO_INPUT_H
