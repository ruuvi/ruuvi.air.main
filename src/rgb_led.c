/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "rgb_led.h"
#include <stdint.h>
#include <zephyr/drivers/led.h>
#include <lp5810_api.h>
#include "led_calibration.h"
#include "tlog.h"

LOG_MODULE_REGISTER(rgb_led, LOG_LEVEL_INF);

#define LED_RGB_MAX_BRIGHTNESS (255U)
#define LED_RGB_MAX_PWM        (255U)

#define LED_RGB_CHANNEL_CURRENT_START 0
#define LED_RGB_CHANNEL_PWM_START     3

typedef uint32_t rgb_led_brightness_idx_t; //<! Brightness index 0..LED_CALIBRATION_BRIGHTNESS_STEPS-1

static rgb_led_color_with_brightness_t g_rgb_led_color = {
    .rgb = {
        .red = 0,
        .green = 0,
        .blue = 0,
    },
    .brightness = 0,
};
static rgb_led_currents_t g_rgb_led_currents = {
    .current_red   = 0,
    .current_green = 0,
    .current_blue  = 0,
};
static rgb_led_pwms_t g_rgb_led_pwms = {
    .pwm_red   = 0,
    .pwm_green = 0,
    .pwm_blue  = 0,
};

static inline rgb_led_pwm_t
led_rgb_calc_pwm(
    const rgb_led_color_val_t      color,
    const rgb_led_brightness_idx_t brightness_idx,
    const uint8_t* const           p_brightness_to_pwm_table)
{
    const uint8_t dimming_coeff = p_brightness_to_pwm_table[brightness_idx];
    return (rgb_led_pwm_t)(((uint32_t)color * dimming_coeff + (LED_RGB_MAX_PWM / 2)) / LED_RGB_MAX_PWM);
}

static void
rgb_led_conv_rgb_with_brightness_to_currents_and_pwms(
    const rgb_led_color_with_brightness_t* const p_color,
    rgb_led_currents_t* const                    p_currents,
    rgb_led_pwms_t* const                        p_pwms)
{
    const rgb_led_brightness_idx_t brightness_idx = ((uint32_t)p_color->brightness * LED_CALIBRATION_BRIGHTNESS_STEPS
                                                     + (LED_CALIBRATION_BRIGHTNESS_STEPS / 2))
                                                    / LED_RGB_MAX_BRIGHTNESS;
    p_currents->current_red   = g_led_calibration_brightness_to_current_red[brightness_idx];
    p_currents->current_green = g_led_calibration_brightness_to_current_green[brightness_idx];
    p_currents->current_blue  = g_led_calibration_brightness_to_current_blue[brightness_idx];

    p_pwms->pwm_red   = led_rgb_calc_pwm(p_color->rgb.red, brightness_idx, g_led_calibration_brightness_to_pwm_red);
    p_pwms->pwm_green = led_rgb_calc_pwm(p_color->rgb.green, brightness_idx, g_led_calibration_brightness_to_pwm_green);
    p_pwms->pwm_blue  = led_rgb_calc_pwm(p_color->rgb.blue, brightness_idx, g_led_calibration_brightness_to_pwm_blue);
}

#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
const struct device* const dev_lp5810 = DEVICE_DT_GET_ONE(ti_lp5810);
#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)

bool
rgb_led_set_raw_currents_and_pwms(
    const rgb_led_currents_t* const p_rgb_led_currents,
    const rgb_led_pwms_t* const     p_rgb_led_pwms)
{
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    uint8_t buf[6] = {
        p_rgb_led_currents->current_red, p_rgb_led_currents->current_green, p_rgb_led_currents->current_blue,
        p_rgb_led_pwms->pwm_red,         p_rgb_led_pwms->pwm_green,         p_rgb_led_pwms->pwm_blue,
    };
    LOG_HEXDUMP_DBG(buf, sizeof(buf), "RGB LED update: ");
    int res = led_write_channels(dev_lp5810, LED_RGB_CHANNEL_CURRENT_START, sizeof(buf), buf);
    if (0 != res)
    {
        LOG_ERR("LP5810: led_write_channels failed, res=%d", res);
        return false;
    }
    return true;
#else
    return true;
#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
}

static bool
rgb_led_update_pwms(const rgb_led_pwms_t* const p_rgb_led_pwms)
{
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    uint8_t buf[3] = {
        p_rgb_led_pwms->pwm_red,
        p_rgb_led_pwms->pwm_green,
        p_rgb_led_pwms->pwm_blue,
    };
    LOG_HEXDUMP_DBG(buf, sizeof(buf), "RGB LED update: ");
    int res = led_write_channels(dev_lp5810, LED_RGB_CHANNEL_PWM_START, sizeof(buf), buf);
    if (0 != res)
    {
        LOG_ERR("LP5810: led_write_channels failed, res=%d", res);
        return false;
    }
    return true;
#else
    return true;
#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
}

bool
rgb_led_init(const rgb_led_brightness_t brightness)
{
    g_rgb_led_color.rgb.red    = 0;
    g_rgb_led_color.rgb.green  = 0;
    g_rgb_led_color.rgb.blue   = 0;
    g_rgb_led_color.brightness = 0;

    g_rgb_led_currents.current_red   = 0;
    g_rgb_led_currents.current_green = 0;
    g_rgb_led_currents.current_blue  = 0;

    g_rgb_led_pwms.pwm_red   = 0;
    g_rgb_led_pwms.pwm_green = 0;
    g_rgb_led_pwms.pwm_blue  = 0;

#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    if (!device_is_ready(dev_lp5810))
    {
        LOG_ERR("Device %s is not ready", dev_lp5810->name);
        return false;
    }
    rgb_led_set_brightness(brightness);
    return true;
#else
    LOG_INF("LP5810 not available, skipping initialization");
    return false;
#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
}

void
rgb_led_deinit(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)

    rgb_led_set_raw_currents_and_pwms(
        &(rgb_led_currents_t) {
            .current_red   = 0,
            .current_green = 0,
            .current_blue  = 0,
        },
        &(rgb_led_pwms_t) {
            .pwm_red   = 0,
            .pwm_green = 0,
            .pwm_blue  = 0,
        });

    lp5810_deinit(dev_lp5810);

#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
}

bool
rgb_led_set_color(const rgb_led_color_t* const p_color)
{
    g_rgb_led_color.rgb.red   = p_color->red;
    g_rgb_led_color.rgb.green = p_color->green;
    g_rgb_led_color.rgb.blue  = p_color->blue;

    rgb_led_conv_rgb_with_brightness_to_currents_and_pwms(&g_rgb_led_color, &g_rgb_led_currents, &g_rgb_led_pwms);

    return rgb_led_update_pwms(&g_rgb_led_pwms);
}

bool
rgb_led_set_color_black(void)
{
    return rgb_led_set_color(&(const rgb_led_color_t) { .red = 0, .green = 0, .blue = 0 });
}

bool
rgb_led_set_brightness(const rgb_led_brightness_t brightness)
{
    g_rgb_led_color.brightness = brightness;
    rgb_led_conv_rgb_with_brightness_to_currents_and_pwms(&g_rgb_led_color, &g_rgb_led_currents, &g_rgb_led_pwms);

    return rgb_led_set_raw_currents_and_pwms(&g_rgb_led_currents, &g_rgb_led_pwms);
}

bool
rgb_led_set_brightness_and_color(const rgb_led_brightness_t brightness, const rgb_led_color_t* const p_color)
{
    g_rgb_led_color.brightness = brightness;
    g_rgb_led_color.rgb.red    = p_color->red;
    g_rgb_led_color.rgb.green  = p_color->green;
    g_rgb_led_color.rgb.blue   = p_color->blue;
    rgb_led_conv_rgb_with_brightness_to_currents_and_pwms(&g_rgb_led_color, &g_rgb_led_currents, &g_rgb_led_pwms);

    return rgb_led_set_raw_currents_and_pwms(&g_rgb_led_currents, &g_rgb_led_pwms);
}

void
rgb_led_lock(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    lp5810_lock(dev_lp5810);
#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
}

void
rgb_led_unlock(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    lp5810_unlock(dev_lp5810);
#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
}

bool
rgb_led_read_raw_pwms(rgb_led_pwms_t* const p_pwms)
{
    if (NULL == p_pwms)
    {
        return false;
    }
    uint8_t buf[3] = { 0 };
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    const int res = lp5810_read_pwms(dev_lp5810, buf, sizeof(buf));
    if (0 != res)
    {
        LOG_ERR("LP5810: led_read_channels failed, res=%d", res);
        return false;
    }
#endif
    p_pwms->pwm_red   = buf[0];
    p_pwms->pwm_green = buf[1];
    p_pwms->pwm_blue  = buf[2];
    return true;
}

bool
rgb_led_write_raw_pwms(const rgb_led_pwms_t* const p_pwms)
{
    if (NULL == p_pwms)
    {
        return false;
    }
    uint8_t buf[3] = {
        p_pwms->pwm_red,
        p_pwms->pwm_green,
        p_pwms->pwm_blue,
    };
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    const int res = lp5810_write_pwms(dev_lp5810, buf, sizeof(buf));
    if (0 != res)
    {
        LOG_ERR("LP5810: led_write_channels failed, res=%d", res);
        return false;
    }
#endif
    return true;
}

bool
rgb_led_check_and_reinit_if_needed(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
    if (!device_is_ready(dev_lp5810))
    {
        LOG_ERR("Device %s is not ready", dev_lp5810->name);
        return false;
    }
    return lp5810_check_and_reinit_if_needed(dev_lp5810);
#else
    return true;
#endif // DT_HAS_COMPAT_STATUS_OKAY(ti_lp5810)
}
