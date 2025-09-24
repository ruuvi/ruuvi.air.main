/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_led.h"
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include "app_button.h"

LOG_MODULE_REGISTER(LED, LOG_LEVEL_WRN);

#if (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))

#if DT_NODE_EXISTS(DT_ALIAS(led_red))
#define LED_RED_NODE DT_ALIAS(led_red)
#else
#error "'led-red' devicetree alias is not defined"
#endif

#if DT_NODE_EXISTS(DT_ALIAS(led_green))
#define LED_GREEN_NODE DT_ALIAS(led_green)
#else
#error "'led-green' devicetree alias is not defined"
#endif

#if DT_NODE_HAS_STATUS(LED_RED_NODE, okay) && DT_NODE_HAS_PROP(LED_RED_NODE, gpios)
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
#else
#error "'led-red' devicetree alias is not defined properly"
#endif

#if DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay) && DT_NODE_HAS_PROP(LED_GREEN_NODE, gpios)
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
#else
#error "'led-green' devicetree alias is not defined properly"
#endif

#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
#if (!IS_ENABLED(CONFIG_PWM))
#error "CONFIG_PWM must be enabled when CONFIG_RUUVI_AIR_PINHOLE_LED_PWM is enabled"
#endif // (!IS_ENABLED(CONFIG_PWM))

#if DT_NODE_EXISTS(DT_ALIAS(led_red_pwm))
#define PWM_LED_RED_NODE DT_ALIAS(led_red_pwm)
#else
#error "'led-red-pwm' devicetree alias is not defined"
#endif

#if DT_NODE_EXISTS(DT_ALIAS(led_green_pwm))
#define PWM_LED_GREEN_NODE DT_ALIAS(led_green_pwm)
#else
#error "'led-green-pwm' devicetree alias is not defined"
#endif

#if DT_NODE_HAS_STATUS(PWM_LED_RED_NODE, okay) && DT_NODE_HAS_PROP(PWM_LED_RED_NODE, pwms)
static const struct pwm_dt_spec pwm_led_red = PWM_DT_SPEC_GET(DT_ALIAS(led_red_pwm));
#else
#error "'led-red' devicetree alias is not defined properly"
#endif

#if DT_NODE_HAS_STATUS(PWM_LED_GREEN_NODE, okay) && DT_NODE_HAS_PROP(PWM_LED_GREEN_NODE, pwms)
static const struct pwm_dt_spec pwm_led_green = PWM_DT_SPEC_GET(DT_ALIAS(led_green_pwm));
#else
#error "'led-green-pwm' devicetree alias is not defined properly"
#endif

static bool g_app_led_in_pwm_mode;
static K_MUTEX_DEFINE(g_app_led_mutex);

#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
#endif // (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))

#if (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
static void
app_led_init_gpio(const struct gpio_dt_spec* p_led_spec)
{
    if (!device_is_ready(p_led_spec->port))
    {
        LOG_ERR("LED %s:%d is not ready", p_led_spec->port->name, p_led_spec->pin);
        return;
    }

    const int rc = gpio_pin_configure_dt(p_led_spec, GPIO_OUTPUT_INACTIVE);
    if (0 != rc)
    {
        LOG_ERR("Failed to configure LED %s:%d, rc %d", p_led_spec->port->name, p_led_spec->pin, rc);
        return;
    }
}
#endif // (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))

void
app_led_early_init(void)
{
#if (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
    app_led_init_gpio(&led_red);
    app_led_init_gpio(&led_green);
#endif // (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
}

#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
static void
app_led_init_pwm_gpio(
    const char* const          p_name,
    const struct gpio_dt_spec* p_led_spec,
    const struct pwm_dt_spec*  p_pwm_led_spec)
{
    LOG_INF(
        "Configure '%s' LED (%s:%d) PWM %s:%d",
        p_name,
        p_led_spec->port->name,
        p_led_spec->pin,
        p_pwm_led_spec->dev->name,
        p_pwm_led_spec->channel);
    const int rc = pwm_set_dt(p_pwm_led_spec, 0, 0);
    if (0 != rc)
    {
        LOG_ERR(
            "Failed to configure '%s' LED (%s:%d) PWM %s:%d, rc %d",
            p_name,
            p_pwm_led_spec->dev->name,
            p_led_spec->pin,
            p_pwm_led_spec->dev->name,
            p_pwm_led_spec->channel,
            rc);
        return;
    }
}
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))

#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
static void
app_led_deinit_pwm(const struct pwm_dt_spec* p_pwm_led_spec)
{
    if (g_app_led_in_pwm_mode)
    {
        pwm_set_dt(p_pwm_led_spec, 0, 0);
    }
}
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))

#if (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
static void
app_led_deinit_gpio(const struct gpio_dt_spec* p_led_spec)
{
    gpio_pin_set_dt(p_led_spec, 0);

    const int rc = gpio_pin_configure_dt(p_led_spec, GPIO_DISCONNECTED);
    if (0 != rc)
    {
        LOG_ERR("Failed to configure LED %s:%d, rc %d", p_led_spec->port->name, p_led_spec->pin, rc);
        return;
    }
}
#endif // (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))

void
app_led_deinit(void)
{
#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
    app_led_deinit_pwm(&pwm_led_red);
    app_led_deinit_pwm(&pwm_led_green);
    g_app_led_in_pwm_mode = false;
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
#if (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
    app_led_deinit_gpio(&led_red);
    app_led_deinit_gpio(&led_green);
#endif // (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
}

void
app_led_late_init_pwm(void)
{
#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
    app_led_deinit_gpio(&led_red);
    app_led_init_pwm_gpio("Red", &led_red, &pwm_led_red);
    app_led_deinit_gpio(&led_green);
    app_led_init_pwm_gpio("Green", &led_green, &pwm_led_green);
    g_app_led_in_pwm_mode = true;
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
}

void
app_led_red_set(const bool is_on)
{
#if (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
    LOG_INF("Red LED state: %s", is_on ? "ON" : "OFF");
#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
    if (g_app_led_in_pwm_mode)
    {
        const int res = pwm_set_dt(
            &pwm_led_red,
            CONFIG_RUUVI_AIR_PINHOLE_LED_PWM_PERIOD_NS,
            is_on ? CONFIG_RUUVI_AIR_PINHOLE_LED_PWM_PULSE_WIDTH_NS : 0);
        if (0 != res)
        {
            LOG_ERR("pwm_set_dt failed");
            return;
        }
    }
    else
    {
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
        const int res = gpio_pin_set_dt(&led_red, is_on ? 1 : 0);
        if (0 != res)
        {
            LOG_ERR("gpio_pin_set_dt failed");
            return;
        }
#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
    }
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
#endif // (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
}

void
app_led_green_set(const bool is_on)
{
#if (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
    LOG_INF("Green LED state: %s", is_on ? "ON" : "OFF");
#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
    if (g_app_led_in_pwm_mode)
    {
        const int res = pwm_set_dt(
            &pwm_led_green,
            CONFIG_RUUVI_AIR_PINHOLE_LED_PWM_PERIOD_NS,
            is_on ? CONFIG_RUUVI_AIR_PINHOLE_LED_PWM_PULSE_WIDTH_NS : 0);
        if (0 != res)
        {
            LOG_ERR("pwm_set_dt failed");
            return;
        }
    }
    else
    {
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
        const int res = gpio_pin_set_dt(&led_green, is_on ? 1 : 0);
        if (0 != res)
        {
            LOG_ERR("gpio_pin_set_dt failed");
            return;
        }
#if (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
    }
#endif // (IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_PWM))
#endif // (!IS_ENABLED(CONFIG_RUUVI_AIR_PINHOLE_LED_NONE))
}

void
app_led_mutex_lock(void)
{
    k_mutex_lock(&g_app_led_mutex, K_FOREVER);
}

void
app_led_mutex_unlock(void)
{
    k_mutex_unlock(&g_app_led_mutex);
}

void
app_led_green_set_if_button_is_not_pressed(const bool is_on)
{
    app_led_mutex_lock();
    if (!app_button_is_pressed())
    {
        app_led_green_set(is_on);
    }
    app_led_mutex_unlock();
}
