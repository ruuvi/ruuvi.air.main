/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "lp5810_test.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/led.h>
#include "led_calibration.h"
#include "tlog.h"

#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)

#if IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_CALIBRATE) || IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_TEST_RGBW)

LOG_MODULE_REGISTER(lp5810_test, LOG_LEVEL_INF);

typedef enum lp5810_channel_e
{
    LP5810_CHANNEL_RED = 0,
    LP5810_CHANNEL_GREEN,
    LP5810_CHANNEL_BLUE,
} lp5810_channel_e;

const struct device* const dev_lp5810 = DEVICE_DT_GET_ONE(ti_lp5810);

#endif // IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_CALIBRATE) || IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_TEST_RGBW)

void
lp5810_test_calibrate(void)
{
#if IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_CALIBRATE)
    if (!device_is_ready(dev_lp5810))
    {
        LOG_ERR("Device %s is not ready", dev_lp5810->name);
    }

    static int g_test_counter = 0;
    g_test_counter += 1;

    if (g_test_counter >= 256 * 4)
    {
        g_test_counter = 0;
    }
    const int current = g_test_counter / 4;
    const int color   = g_test_counter % 4;
    LOG_INF("LP5810: Set current %d for color %d", current, color);
    int           res = 0;
    const uint8_t pwm = 255;
    switch (color)
    {
        case 0:
            res = led_write_channels(dev_lp5810, 0, 6, (uint8_t[]) { 0, 0, 0, pwm, pwm, pwm });
            break;
        case 1:
            // res = led_write_channels(dev_lp5810, 0, 6, (uint8_t[]) { 255 - current, 0, 0, pwm, pwm, pwm });
            res = led_write_channels(dev_lp5810, 0, 6, (uint8_t[]) { current, 0, 0, pwm, pwm, pwm });
            break;
        case 2:
            // res = led_write_channels(dev_lp5810, 0, 6, (uint8_t[]) { 0, 255 - current, 0, pwm, pwm, pwm });
            res = led_write_channels(dev_lp5810, 0, 6, (uint8_t[]) { 0, current, 0, pwm, pwm, pwm });
            break;
        case 3:
            // res = led_write_channels(dev_lp5810, 0, 6, (uint8_t[]) { 0, 0, 255 - current, pwm, pwm, pwm });
            res = led_write_channels(dev_lp5810, 0, 6, (uint8_t[]) { 0, 0, current, pwm, pwm, pwm });
            break;
    }
    if (0 != res)
    {
        LOG_ERR("LP5810: Failed to set color for channel %d", color);
    }
#endif // IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_CALIBRATE)
}

void
lp5810_test_rgbw(void)
{
#if IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_TEST_RGBW)
    enum lp5810_test_rgbw_channel_e
    {
        LP5810_TEST_RGBW_CHANNEL_BLACK = 0,
        LP5810_TEST_RGBW_CHANNEL_RED,
        LP5810_TEST_RGBW_CHANNEL_GREEN,
        LP5810_TEST_RGBW_CHANNEL_BLUE,
        LP5810_TEST_RGBW_CHANNEL_WHITE,
    } lp5810_test_rgbw_channel_e;

    if (!device_is_ready(dev_lp5810))
    {
        LOG_ERR("Device %s is not ready", dev_lp5810->name);
    }

    static int g_test_rgbw_stage = LP5810_TEST_RGBW_CHANNEL_BLACK;
    g_test_rgbw_stage += 1;

    if (g_test_rgbw_stage > LP5810_TEST_RGBW_CHANNEL_WHITE)
    {
        g_test_rgbw_stage = LP5810_TEST_RGBW_CHANNEL_BLACK;
    }

    const uint8_t brightness    = 20;
    const uint8_t pwm           = 255;
    const uint8_t current_red   = g_led_calibration_brightness_to_current_red[brightness];
    const uint8_t current_green = g_led_calibration_brightness_to_current_green[brightness];
    const uint8_t current_blue  = g_led_calibration_brightness_to_current_blue[brightness];
    const uint8_t vals_black[6] = { 0, 0, 0, pwm, pwm, pwm };
    const uint8_t vals_red[6]   = { current_red, 0, 0, pwm, pwm, pwm };
    const uint8_t vals_green[6] = { 0, current_green, 0, pwm, pwm, pwm };
    const uint8_t vals_blue[6]  = { 0, 0, current_blue, pwm, pwm, pwm };
    const uint8_t vals_white[6] = { current_red, current_green, current_blue, pwm, pwm, pwm };

    uint8_t* p_vals = vals_black;
    switch (g_test_rgbw_stage)
    {
        case LP5810_TEST_RGBW_CHANNEL_BLACK:
            p_vals = vals_black;
            break;
        case LP5810_TEST_RGBW_CHANNEL_RED:
            p_vals = vals_red;
            break;
        case LP5810_TEST_RGBW_CHANNEL_GREEN:
            p_vals = vals_green;
            break;
        case LP5810_TEST_RGBW_CHANNEL_BLUE:
            p_vals = vals_blue;
            break;
        case LP5810_TEST_RGBW_CHANNEL_WHITE:
            p_vals = vals_white;
            break;
        default:
            break;
    }
    LOG_HEXDUMP_INF(p_vals, 6, "LP5810: Set Currents/PWM");
    const int res = led_write_channels(dev_lp5810, 0, 6, p_vals);
    if (0 != res)
    {
        LOG_ERR("LP5810: led_write_channels failed, res=%d", res);
    }
#endif // IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_TEST_RGBW)
}

#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
