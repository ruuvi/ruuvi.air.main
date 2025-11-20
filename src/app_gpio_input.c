/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_gpio_input.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(GPIO, LOG_LEVEL_INF);

void
app_gpio_input_init(
    const struct gpio_dt_spec* const p_gpio_dt,
    gpio_flags_t                     extra_flags,
    struct gpio_callback* const      p_gpio_callback,
    const gpio_callback_handler_t    cb_handler,
    const gpio_flags_t               int_flags)
{
    LOG_INF("Configure GPIO: %s pin %d", p_gpio_dt->port->name, p_gpio_dt->pin);
    if (!gpio_is_ready_dt(p_gpio_dt))
    {
        LOG_ERR("GPIO %s is not ready", p_gpio_dt->port->name);
        return;
    }

    int32_t ret = gpio_pin_configure_dt(p_gpio_dt, GPIO_INPUT | extra_flags);
    if (ret != 0)
    {
        LOG_ERR("Failed to configure %s pin %d, res=%d", p_gpio_dt->port->name, p_gpio_dt->pin, ret);
        return;
    }

    if (0 != (int_flags & GPIO_INT_ENABLE))
    {
        ret = gpio_pin_interrupt_configure_dt(p_gpio_dt, int_flags);
        if (ret != 0)
        {
            LOG_ERR("Failed to configure interrupt on %s pin %d, res=%d", p_gpio_dt->port->name, p_gpio_dt->pin, ret);
            return;
        }

        if ((0 != (int_flags & GPIO_INT_ENABLE)) && (NULL != cb_handler) && (NULL != p_gpio_callback))
        {
            LOG_INF("Set up GPIO callback at %s pin %d", p_gpio_dt->port->name, p_gpio_dt->pin);
            gpio_init_callback(p_gpio_callback, cb_handler, BIT(p_gpio_dt->pin));
            gpio_add_callback(p_gpio_dt->port, p_gpio_callback);
        }
    }
    else
    {
        ret = gpio_pin_interrupt_configure_dt(p_gpio_dt, GPIO_INT_DISABLE);
        if (ret != 0)
        {
            LOG_ERR("Failed to disable interrupt on %s pin %d, res=%d", p_gpio_dt->port->name, p_gpio_dt->pin, ret);
            return;
        }
    }
}
