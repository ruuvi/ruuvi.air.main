/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_ext_flash_and_sensors_power.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include "tlog.h"

LOG_MODULE_DECLARE(GPIO, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_NODELABEL(gpio_enable_sensors))
#error "gpio_enable_sensors devicetree alias is not defined"
#else
#define GPIO_ENABLE_SENSORS_NODE DT_NODELABEL(gpio_enable_sensors)
#endif

#if DT_NODE_HAS_STATUS(GPIO_ENABLE_SENSORS_NODE, okay) && DT_NODE_HAS_PROP(GPIO_ENABLE_SENSORS_NODE, gpios)
static const struct gpio_dt_spec gpio_enable_sensors = GPIO_DT_SPEC_GET(GPIO_ENABLE_SENSORS_NODE, gpios);
#else
#error "Overlay for gpio_enable_sensors node not properly defined."
#endif

void
app_ext_flash_and_sensors_power_on(void)
{
    TLOG_INF("Power on external flash memory and sensors");
    if (!gpio_is_ready_dt(&gpio_enable_sensors))
    {
        TLOG_ERR("GPIO ENABLE_SENSORS is not ready");
        return;
    }
#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)
    int32_t ret = gpio_pin_configure_dt(&gpio_enable_sensors, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0)
    {
        TLOG_ERR("gpio_pin_configure_dt failed for GPIO_ENABLE_SENSORS, ret=%d", ret);
    }
#elif defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_2)
    int32_t ret = gpio_pin_configure_dt(&gpio_enable_sensors, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
    if (ret < 0)
    {
        TLOG_ERR("gpio_pin_configure_dt failed for GPIO_ENABLE_SENSORS, ret=%d", ret);
    }
#else
#error "Unsupported board configuration. CONFIG_BOARD_RUUVI_RUUVIAIR_REV_<X> must be defined."
#endif
}

void
app_ext_flash_and_sensors_power_off(void)
{
    TLOG_WRN("Power off external flash memory and sensors");
    if (!gpio_is_ready_dt(&gpio_enable_sensors))
    {
        TLOG_ERR("GPIO ENABLE_SENSORS is not ready");
        return;
    }
#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)
    int32_t ret = gpio_pin_configure_dt(&gpio_enable_sensors, GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);
    if (ret < 0)
    {
        TLOG_ERR("gpio_pin_configure_dt failed for GPIO_ENABLE_SENSORS, ret=%d", ret);
    }
#elif defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_2)
    int32_t ret = gpio_pin_configure_dt(&gpio_enable_sensors, GPIO_DISCONNECTED);
    if (ret < 0)
    {
        TLOG_ERR("gpio_pin_configure_dt failed for GPIO_ENABLE_SENSORS, ret=%d", ret);
    }
#else
#error "Unsupported board configuration. CONFIG_BOARD_RUUVI_RUUVIAIR_REV_<X> must be defined."
#endif
}
