/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "aqi.h"
#include <math.h>
#include <zephyr/kernel.h>
#include "rgb_led_types.h"
#include "opt_rgb_ctrl.h"
#include "app_settings.h"
#include "tlog.h"

LOG_MODULE_REGISTER(AQI, LOG_LEVEL_INF);

#define AQI_MAX_LUMINOSITY          (2000.0f)
#define AQI_LED_MAX_AUTO_BRIGHTNESS (CONFIG_RUUVI_AIR_LED_BRIGHTNESS)

#define AIR_QUALITY_INDEX_EXCELLENT_THRESHOLD (89.5f)
#define AIR_QUALITY_INDEX_GOOD_THRESHOLD      (79.5f)
#define AIR_QUALITY_INDEX_MODERATE_THRESHOLD  (49.5f)
#define AIR_QUALITY_INDEX_POOR_THRESHOLD      (9.5f)

#define AQI_EMA_ALPHA (0.1f) /* 1.0f - expf(-1.0f / 10) ≈ 0.1 */

#define AQI_LED_EXP_CURRENTS_DURATION_MS (1000)

static float g_aqi_luminosity_ema = 200.0f;

static float               g_air_quality_index   = NAN;
static air_quality_index_e g_aqi_led             = AIR_QUALITY_INDEX_EXCELLENT;
static int64_t             g_aqi_led_last_update = 0;
static bool                g_aqi_become_valid    = false;
static bool                g_aqi_is_started      = false;
static int64_t             g_aqi_started_timestamp;

static rgb_led_brightness_t g_aqi_led_auto_brightness_level   = AQI_LED_MAX_AUTO_BRIGHTNESS / 2;
static uint8_t              g_aqi_led_auto_brightness_dim_pwm = 128;

static const rgb_led_color_t g_aqi_auto_led_colors_table[AIR_QUALITY_NUM_INDEXES] = {
    [AIR_QUALITY_INDEX_NONE]      = { .red = 0, .green = 0, .blue = 0, },
    [AIR_QUALITY_INDEX_EXCELLENT] = { .red = 0, .green = 255, .blue = 90, },
    [AIR_QUALITY_INDEX_GOOD]      = { .red = 30, .green = 255, .blue = 0, },
    [AIR_QUALITY_INDEX_FAIR]      = { .red = 240, .green = 255, .blue = 0, },
    [AIR_QUALITY_INDEX_POOR]      = { .red = 255, .green = 80, .blue = 0, },
    [AIR_QUALITY_INDEX_VERY_POOR] = { .red = 255, .green = 0, .blue = 0, },
};

static const manual_brightness_color_t g_aqi_manual_brightness_colors_table_init[MANUAL_BRIGHTNESS_LEVELS] = {
    [MANUAL_BRIGHTNESS_LEVEL_OFF] = {
        .currents = { .current_red = 12, .current_green = 2, .current_blue = 10 },
        .colors   = {
            [AIR_QUALITY_INDEX_NONE]      = { .red = 0, .green = 0, .blue = 0, },
            [AIR_QUALITY_INDEX_EXCELLENT] = { .red = 0, .green = 0, .blue = 0, },
            [AIR_QUALITY_INDEX_GOOD]      = { .red = 0, .green = 0, .blue = 0, },
            [AIR_QUALITY_INDEX_FAIR]      = { .red = 0, .green = 0, .blue = 0, },
            [AIR_QUALITY_INDEX_POOR]      = { .red = 0, .green = 0, .blue = 0, },
            [AIR_QUALITY_INDEX_VERY_POOR] = { .red = 0, .green = 0, .blue = 0, },
        },
    },
    [MANUAL_BRIGHTNESS_LEVEL_NIGHT] = {
        .currents = { .current_red = 12, .current_green = 2, .current_blue = 10 },
        .colors   = {
            [AIR_QUALITY_INDEX_NONE]      = { .red = 0, .green = 0, .blue = 0 },
            [AIR_QUALITY_INDEX_EXCELLENT] = { .red = 0, .green = 255, .blue = 90 },
            [AIR_QUALITY_INDEX_GOOD]      = { .red = 30, .green = 255, .blue = 0 },
            [AIR_QUALITY_INDEX_FAIR]      = { .red = 240, .green = 255, .blue = 0 },
            [AIR_QUALITY_INDEX_POOR]      = { .red = 255, .green = 80, .blue = 0 },
            [AIR_QUALITY_INDEX_VERY_POOR] = { .red = 255, .green = 0, .blue = 0 },
        },
    },
    [MANUAL_BRIGHTNESS_LEVEL_DAY] = {
        .currents = { .current_red = 35, .current_green = 6, .current_blue = 20 },
        .colors   = {
            [AIR_QUALITY_INDEX_NONE]      = { .red = 0, .green = 0, .blue = 0 },
            [AIR_QUALITY_INDEX_EXCELLENT] = { .red = 0, .green = 255, .blue = 90 },
            [AIR_QUALITY_INDEX_GOOD]      = { .red = 30, .green = 255, .blue = 0 },
            [AIR_QUALITY_INDEX_FAIR]      = { .red = 240, .green = 255, .blue = 0 },
            [AIR_QUALITY_INDEX_POOR]      = { .red = 255, .green = 80, .blue = 0 },
            [AIR_QUALITY_INDEX_VERY_POOR] = { .red = 255, .green = 0, .blue = 0 },
        },
    },
    [MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY] = {
        .currents = { .current_red = 150, .current_green = 70, .current_blue = 255 },
        .colors   = {
            [AIR_QUALITY_INDEX_NONE]      = { .red = 0, .green = 0, .blue = 0 },
            [AIR_QUALITY_INDEX_EXCELLENT] = { .red = 0, .green = 255, .blue = 90 },
            [AIR_QUALITY_INDEX_GOOD]      = { .red = 30, .green = 255, .blue = 0 },
            [AIR_QUALITY_INDEX_FAIR]      = { .red = 255, .green = 160, .blue = 0 },
            [AIR_QUALITY_INDEX_POOR]      = { .red = 255, .green = 80, .blue = 0 },
            [AIR_QUALITY_INDEX_VERY_POOR] = { .red = 255, .green = 0, .blue = 0 },
        },
    },
};

static manual_brightness_color_t g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVELS] = {
    [MANUAL_BRIGHTNESS_LEVEL_OFF]   = g_aqi_manual_brightness_colors_table_init[MANUAL_BRIGHTNESS_LEVEL_OFF],
    [MANUAL_BRIGHTNESS_LEVEL_NIGHT] = g_aqi_manual_brightness_colors_table_init[MANUAL_BRIGHTNESS_LEVEL_NIGHT],
    [MANUAL_BRIGHTNESS_LEVEL_DAY]   = g_aqi_manual_brightness_colors_table_init[MANUAL_BRIGHTNESS_LEVEL_DAY],
    [MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY]
    = g_aqi_manual_brightness_colors_table_init[MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY],
};

static rgb_led_exp_current_coefs_t g_aqi_led_currents_alpha;

const manual_brightness_color_t*
aqi_get_colors_table(const manual_brightness_level_e level)
{
    if (level < MANUAL_BRIGHTNESS_LEVELS)
    {
        return &g_aqi_manual_brightness_colors_table[level];
    }
    return &g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_OFF];
}

void
aqi_set_colors_table(const manual_brightness_level_e level, const manual_brightness_color_t* const p_table)
{
    if (level < MANUAL_BRIGHTNESS_LEVELS)
    {
        g_aqi_manual_brightness_colors_table[level] = *p_table;
        if (MANUAL_BRIGHTNESS_LEVEL_NIGHT == level)
        {
            g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_OFF].currents = p_table->currents;
        }
    }
}

void
aqi_reset_colors_table(const manual_brightness_level_e level)
{
    if (level < MANUAL_BRIGHTNESS_LEVELS)
    {
        g_aqi_manual_brightness_colors_table[level] = g_aqi_manual_brightness_colors_table_init[level];
        if (MANUAL_BRIGHTNESS_LEVEL_NIGHT == level)
        {
            g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_OFF].currents
                = g_aqi_manual_brightness_colors_table_init[MANUAL_BRIGHTNESS_LEVEL_NIGHT].currents;
        }
    }
}

static void
aqi_init_exp_current_coef(
    rgb_led_exp_current_coef_t* const p_coef,
    const uint8_t                     current_min,
    const uint8_t                     current_max,
    const uint16_t                    duration_ms)
{
    p_coef->current_min = current_min;
    p_coef->current_max = current_max;
    p_coef->duration_ms = duration_ms;
    p_coef->alpha       = logf(current_max - current_min + 1.0f) / k_ms_to_ticks_ceil32(duration_ms);
}

void
aqi_init(void)
{
    aqi_init_exp_current_coef(
        &g_aqi_led_currents_alpha.coef_red,
        g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_NIGHT].currents.current_red,
        g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY].currents.current_red,
        AQI_LED_EXP_CURRENTS_DURATION_MS);
    aqi_init_exp_current_coef(
        &g_aqi_led_currents_alpha.coef_green,
        g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_NIGHT].currents.current_green,
        g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY].currents.current_green,
        AQI_LED_EXP_CURRENTS_DURATION_MS);
    aqi_init_exp_current_coef(
        &g_aqi_led_currents_alpha.coef_blue,
        g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_NIGHT].currents.current_blue,
        g_aqi_manual_brightness_colors_table[MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY].currents.current_blue,
        AQI_LED_EXP_CURRENTS_DURATION_MS);
}

const rgb_led_exp_current_coefs_t*
aqi_get_led_currents_alpha(void)
{
    return &g_aqi_led_currents_alpha;
}

static air_quality_index_e
aqi_calculate_index(const float air_quality_index)
{
    if (isnan(air_quality_index))
    {
        return AIR_QUALITY_INDEX_NONE;
    }
    if (air_quality_index >= AIR_QUALITY_INDEX_EXCELLENT_THRESHOLD)
    {
        return AIR_QUALITY_INDEX_EXCELLENT;
    }
    else if (air_quality_index >= AIR_QUALITY_INDEX_GOOD_THRESHOLD)
    {
        return AIR_QUALITY_INDEX_GOOD;
    }
    else if (air_quality_index >= AIR_QUALITY_INDEX_MODERATE_THRESHOLD)
    {
        return AIR_QUALITY_INDEX_FAIR;
    }
    else if (air_quality_index >= AIR_QUALITY_INDEX_POOR_THRESHOLD)
    {
        return AIR_QUALITY_INDEX_POOR;
    }
    else
    {
        return AIR_QUALITY_INDEX_VERY_POOR;
    }
}

void
aqi_recalc_auto_brightness_level(const float luminosity)
{
    float coef = NAN;
    if (!isnan(luminosity))
    {
        const float luminosity_limited = (luminosity < AQI_MAX_LUMINOSITY) ? luminosity : AQI_MAX_LUMINOSITY;
        g_aqi_luminosity_ema = AQI_EMA_ALPHA * luminosity_limited + (1.0f - AQI_EMA_ALPHA) * g_aqi_luminosity_ema;
        const float e        = expf(1.0f);
        coef                 = (logf(e + g_aqi_luminosity_ema) - 1) / logf(e + AQI_MAX_LUMINOSITY);
        if (coef > 0.2f)
        {
            g_aqi_led_auto_brightness_level   = (uint8_t)roundf(AQI_LED_MAX_AUTO_BRIGHTNESS * coef);
            g_aqi_led_auto_brightness_dim_pwm = 255;
        }
        else if (coef > 0.02f)
        {
            g_aqi_led_auto_brightness_level   = (uint8_t)roundf(AQI_LED_MAX_AUTO_BRIGHTNESS * 0.2f);
            g_aqi_led_auto_brightness_dim_pwm = (uint8_t)roundf(255 * (coef / 0.2f));
        }
        else
        {
            g_aqi_led_auto_brightness_level   = (uint8_t)roundf(AQI_LED_MAX_AUTO_BRIGHTNESS * 0.1f);
            g_aqi_led_auto_brightness_dim_pwm = (uint8_t)roundf(255 * (coef / 0.1f));
            if (g_aqi_led_auto_brightness_dim_pwm < 20)
            {
                g_aqi_led_auto_brightness_dim_pwm = 20;
            }
        }
    }
}

static void
aqi_update_led_auto(const air_quality_index_e aqi_idx)
{
    const rgb_led_color_t* const p_led_color = &g_aqi_auto_led_colors_table[aqi_idx];

    const rgb_led_color_t led_color = {
        .red   = (uint8_t)(((uint32_t)p_led_color->red * g_aqi_led_auto_brightness_dim_pwm) / 255U),
        .green = (uint8_t)(((uint32_t)p_led_color->green * g_aqi_led_auto_brightness_dim_pwm) / 255U),
        .blue  = (uint8_t)(((uint32_t)p_led_color->blue * g_aqi_led_auto_brightness_dim_pwm) / 255U),
    };

    LOG_INF(
        "AQI=%d, %.3f, brightness: %d, dim: %d, set colors: <%d, %d, %d> -> <%d, "
        "%d, %d>",
        g_aqi_led,
        (double)g_aqi_luminosity_ema,
        g_aqi_led_auto_brightness_level,
        g_aqi_led_auto_brightness_dim_pwm,
        p_led_color->red,
        p_led_color->green,
        p_led_color->blue,
        led_color.red,
        led_color.green,
        led_color.blue);

    opt_rgb_ctrl_set_next_brightnes_and_color(g_aqi_led_auto_brightness_level, &led_color);
}

static void
aqi_update_led_manual_percentage(
    const app_settings_led_brightness_deci_percent_t brightness_deci_percent,
    const air_quality_index_e                        aqi_idx)
{
    const rgb_led_color_t* const p_led_color = &g_aqi_auto_led_colors_table[aqi_idx];

    uint8_t              dim_pwm        = 0;
    rgb_led_brightness_t led_brightness = app_settings_conv_deci_percent_to_brightness(
        brightness_deci_percent,
        &dim_pwm);

    const rgb_led_color_t led_color = {
        .red   = (uint8_t)(((uint32_t)p_led_color->red * dim_pwm) / 255U),
        .green = (uint8_t)(((uint32_t)p_led_color->green * dim_pwm) / 255U),
        .blue  = (uint8_t)(((uint32_t)p_led_color->blue * dim_pwm) / 255U),
    };

    LOG_INF(
        "AQI=%d, brightness: %u.%01u%%, dim: %d, set colors: <%d, %d, %d> -> <%d, %d, %d>",
        g_aqi_led,
        brightness_deci_percent / 10,
        brightness_deci_percent % 10,
        dim_pwm,
        p_led_color->red,
        p_led_color->green,
        p_led_color->blue,
        led_color.red,
        led_color.green,
        led_color.blue);

    opt_rgb_ctrl_set_next_brightnes_and_color(led_brightness, &led_color);
}

static void
aqi_update_led_manual(const manual_brightness_level_e brightness_level, const air_quality_index_e aqi_idx)
{
    const manual_brightness_color_t* const p_info      = &g_aqi_manual_brightness_colors_table[brightness_level];
    const rgb_led_color_t* const           p_led_color = &p_info->colors[aqi_idx];

    const rgb_led_pwms_t led_pwm = {
        .pwm_red   = p_led_color->red,
        .pwm_green = p_led_color->green,
        .pwm_blue  = p_led_color->blue,
    };

    opt_rgb_ctrl_set_next_raw_currents_and_pwms(&p_info->currents, &led_pwm);
}

void
aqi_update_led(const float air_quality_index)
{
    g_aqi_led_last_update = k_uptime_get();
    g_aqi_led             = aqi_calculate_index(air_quality_index);
    g_air_quality_index   = air_quality_index;

    if (!g_aqi_is_started)
    {
        g_aqi_is_started        = true;
        g_aqi_started_timestamp = g_aqi_led_last_update;
    }
    aqi_refresh_led();
}

void
aqi_refresh_led(void)
{
    bool flag_stop_bootup_fading = false;
    if (!g_aqi_become_valid)
    {
        if (AIR_QUALITY_INDEX_NONE != g_aqi_led)
        {
            g_aqi_become_valid      = true;
            flag_stop_bootup_fading = true;
        }
        else
        {
            if ((g_aqi_led_last_update - g_aqi_started_timestamp) > (30 * 1000))
            {
                flag_stop_bootup_fading = true;
            }
            else
            {
                return;
            }
        }
    }

    const enum app_settings_led_mode led_mode = app_settings_get_led_mode();
    switch (led_mode)
    {
        case APP_SETTINGS_LED_MODE_DISABLED:
            opt_rgb_ctrl_set_next_color_black();
            break;

        case APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY:
            aqi_update_led_manual(MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY, g_aqi_led);
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_DAY:
            aqi_update_led_manual(MANUAL_BRIGHTNESS_LEVEL_DAY, g_aqi_led);
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_NIGHT:
            aqi_update_led_manual(MANUAL_BRIGHTNESS_LEVEL_NIGHT, g_aqi_led);
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_OFF:
            aqi_update_led_manual(MANUAL_BRIGHTNESS_LEVEL_OFF, g_aqi_led);
            break;

        case APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE:
            aqi_update_led_manual_percentage(app_settings_get_led_brightness_deci_percent(), g_aqi_led);
            break;

        case APP_SETTINGS_LED_MODE_AUTO:
            aqi_update_led_auto(g_aqi_led);
            break;
    }
    if (flag_stop_bootup_fading)
    {
        opt_rgb_ctrl_stop_bootup_led_fading();
    }
}
