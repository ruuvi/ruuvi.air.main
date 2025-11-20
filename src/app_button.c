/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_button.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "app_gpio_input.h"
#include "zephyr_api.h"

LOG_MODULE_DECLARE(GPIO, LOG_LEVEL_INF);

#define BUTTON_0_NODE DT_ALIAS(button_pinhole)
#if DT_NODE_EXISTS(BUTTON_0_NODE) && DT_NODE_HAS_PROP(BUTTON_0_NODE, gpios)
static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(BUTTON_0_NODE, gpios);
#else
#error "Unsupported board: button0 devicetree node label is not defined"
#endif

static atomic_t g_flag_button_pressed = ATOMIC_INIT(0);

void
app_button_init(
    struct gpio_callback* const   p_gpio_callback,
    const gpio_callback_handler_t cb_handler,
    const gpio_flags_t            int_flags)
{
    const struct gpio_dt_spec* const p_button = &button0;
    app_gpio_input_init(p_button, GPIO_PULL_UP, p_gpio_callback, cb_handler, int_flags);
}

void
app_button_int_disable(void)
{
    const struct gpio_dt_spec* const p_button = &button0;

    const zephyr_api_ret_t ret = gpio_pin_interrupt_configure_dt(p_button, GPIO_INT_DISABLE);
    if (0 != ret)
    {
        LOG_ERR("Failed to disable interrupt on %s pin %d, res=%d", p_button->port->name, p_button->pin, ret);
        return;
    }
}

void
app_button_remove_cb(struct gpio_callback* const p_gpio_callback)
{
    const struct gpio_dt_spec* const p_button = &button0;

    gpio_remove_callback(p_button->port, p_gpio_callback);
}

void
app_button_deinit(struct gpio_callback* const p_gpio_callback)
{
    const struct gpio_dt_spec* const p_button = &button0;

    if (!device_is_ready(p_button->port))
    {
        LOG_ERR("BUTTON0 is not ready");
        return;
    }

    app_button_int_disable();

    if (NULL != p_gpio_callback)
    {
        app_button_remove_cb(p_gpio_callback);
    }

    const int32_t ret = gpio_pin_configure_dt(p_button, GPIO_DISCONNECTED);
    if (0 != ret)
    {
        LOG_ERR("Failed to disconnect GPIO on %s pin %d, res=%d", p_button->port->name, p_button->pin, ret);
        return;
    }
}

bool
app_button_get(void)
{
    zephyr_api_ret_t rc = gpio_pin_get_dt(&button0);
    if (rc < 0)
    {
        LOG_ERR("Failed to get BUTTON0 (rc: %d)", rc);
        return false;
    }
    return !!rc;
}

void
app_button_set_pressed(void)
{
    atomic_set(&g_flag_button_pressed, 1);
}

void
app_button_clr_pressed(void)
{
    atomic_clear(&g_flag_button_pressed);
}

bool
app_button_is_pressed(void)
{
    if (0 != atomic_get(&g_flag_button_pressed))
    {
        return true;
    }
    return false;
}
