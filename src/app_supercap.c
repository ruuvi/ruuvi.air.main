/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_supercap.h"
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/logging/log.h>
#include "app_gpio_input.h"
#include "app_led.h"
#include "app_button_cb.h"

LOG_MODULE_DECLARE(GPIO, LOG_LEVEL_INF);

#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)

#define SUPERCAP_ACTIVE_NODE DT_ALIAS(gpio_supercap_active)
#if !DT_NODE_HAS_STATUS_OKAY(SUPERCAP_ACTIVE_NODE)
#error "'gpio-supercap-active' devicetree alias is not defined"
#endif

static const struct gpio_dt_spec g_supercap_active = GPIO_DT_SPEC_GET(SUPERCAP_ACTIVE_NODE, gpios);
static struct gpio_callback      g_supercap_active_isr_gpio_cb_data;

static __NO_RETURN void
app_on_supercap_active(void)
{
    // Do not print logs here, as this can be called from ISR
    app_button_cb_deinit();
    app_led_deinit();
    (void)gpio_pin_interrupt_configure_dt(&g_supercap_active, GPIO_INT_LEVEL_HIGH);
    sys_poweroff();
}

static void
app_isr_cb_supercap_active(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
    (void)dev;
    (void)cb;
    (void)pins;

    app_on_supercap_active();
}

void
app_supercap_init(void)
{
    LOG_INF("Set up GPIO SUPERCAP_ACTIVE");
    const gpio_flags_t extra_flags = 0;
    app_gpio_input_init(
        &g_supercap_active,
        extra_flags,
        &g_supercap_active_isr_gpio_cb_data,
        &app_isr_cb_supercap_active,
        GPIO_INT_EDGE_FALLING);
    int rc = gpio_pin_get_dt(&g_supercap_active);
    if (rc < 0)
    {
        LOG_ERR("%s: Failed to get GPIO_SUPERCAP_ACTIVE (rc: %d)", __func__, rc);
        return;
    }
    const bool is_supercap_active = !!rc;
    if (is_supercap_active)
    {
        app_on_supercap_active();
    }
}

#endif // CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1
