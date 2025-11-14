/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "app_settings.h"
#include "rgb_led.h"
#include "opt_rgb_ctrl.h"
#include "utils.h"
#include "aqi.h"
#include "fw_img_hw_rev.h"
#include "app_fw_ver.h"

LOG_MODULE_REGISTER(shell_cmd_ruuvi, LOG_LEVEL_INF);

static void
log_args(size_t argc, char** argv)
{
    LOG_DBG("%s: argc=%zu", __func__, argc);
    for (size_t i = 0; i < argc; i++)
    {
        LOG_DBG("%s: argv[%zu]=%s", __func__, i, argv[i]);
    }
}

static int
cmd_ruuvi_echo(const struct shell* sh, size_t argc, char** argv)
{
    log_args(argc, argv);

    const char* const p_message = argv[1];
    LOG_DBG("Echo: %s", p_message);

    shell_print(sh, "%s", p_message);

    return 0;
}

static int
cmd_ruuvi_led_brightness(const struct shell* sh, size_t argc, char** argv)
{
    log_args(argc, argv);

    const char* const p_brightness = argv[1];
    LOG_DBG("LED Brightness: %s", p_brightness);

    if (0 == strcmp(p_brightness, APP_SETTINGS_VAL_LED_BRIGHTNESS_OFF))
    {
        app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_OFF);
    }
    else if (0 == strcmp(p_brightness, APP_SETTINGS_VAL_LED_BRIGHTNESS_NIGHT))
    {
        app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_NIGHT);
    }
    else if (0 == strcmp(p_brightness, APP_SETTINGS_VAL_LED_BRIGHTNESS_DAY))
    {
        app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_DAY);
    }
    else if (0 == strcmp(p_brightness, APP_SETTINGS_VAL_LED_BRIGHTNESS_BRIGHT_DAY))
    {
        app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY);
    }
    else
    {
        if (!app_settings_set_led_mode_manual_percentage(p_brightness))
        {
            shell_error(sh, "Invalid brightness value: %s", p_brightness);
            return -EINVAL;
        }
    }
    opt_rgb_ctrl_enable_led(true);
    app_post_event_refresh_led();

    return 0;
}

static bool
parse_uint8(const char* const p_str, uint8_t* const p_value)
{
    char*      p_end = NULL;
    const long val   = strtol(p_str, &p_end, 10);
    if ((NULL == p_end) || (p_end == p_str) || ('\0' != *p_end) || (val < 0) || (val > 255))
    {
        return false;
    }
    *p_value = (uint8_t)val;
    return true;
}

static bool
parse_uint8_print_err(const struct shell* sh, const char* const p_str, uint8_t* const p_value)
{
    if (!parse_uint8(p_str, p_value))
    {
        shell_error(sh, "Invalid uint8 value: %s", p_str);
        return false;
    }
    return true;
}

static int
cmd_ruuvi_led_write_channels(const struct shell* sh, size_t argc, char** argv)
{
    log_args(argc, argv);
    uint8_t currents[3] = { 0 };
    uint8_t pwms[3]     = { 0 };

    for (int i = 0; i < 3; ++i)
    {
        if (!parse_uint8(argv[1 + i], &currents[i]))
        {
            shell_error(sh, "Invalid current value: %s", argv[1 + i]);
            return -EINVAL;
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        if (!parse_uint8(argv[4 + i], &pwms[i]))
        {
            shell_error(sh, "Invalid PWM value: %s", argv[4 + i]);
            return -EINVAL;
        }
    }
    const rgb_led_currents_t led_currents = {
        .current_red   = currents[0],
        .current_green = currents[1],
        .current_blue  = currents[2],
    };
    const rgb_led_pwms_t led_pwms = {
        .pwm_red   = pwms[0],
        .pwm_green = pwms[1],
        .pwm_blue  = pwms[2],
    };

    shell_fprintf(
        sh,
        SHELL_NORMAL,
        "%s: Writing LED currents and PWMs: <%u, %u, %u> <%u, %u, %u>\n",
        rgb_led_get_dev_name(),
        led_currents.current_red,
        led_currents.current_green,
        led_currents.current_blue,
        led_pwms.pwm_red,
        led_pwms.pwm_green,
        led_pwms.pwm_blue);

    // Disable AQI LED control when a client is connected via BLE and
    // first 'led_write_channels' command is received.
    // This allows to use the LED without interference from AQI indication.
    // The LED control is re-enabled when the BLE connection is closed.
    opt_rgb_ctrl_enable_led(false);

    if (!rgb_led_set_raw_currents_and_pwms(&led_currents, &led_pwms))
    {
        shell_error(sh, "Failed to set LED currents and PWMs");
        return -EIO;
    }

    return 0;
}

static void
print_led_color_table(
    const struct shell*                    sh,
    const char* const                      p_brightness,
    const manual_brightness_color_t* const p_colors)
{
    shell_print(
        sh,
        "LED color table '%s': <%u, %u, %u> [<%u, %u, %u> <%u, %u, %u> <%u, %u, %u> <%u, %u, %u> <%u, %u, %u>]",
        p_brightness,
        p_colors->currents.current_red,
        p_colors->currents.current_green,
        p_colors->currents.current_blue,
        p_colors->colors[AIR_QUALITY_INDEX_EXCELLENT].red,
        p_colors->colors[AIR_QUALITY_INDEX_EXCELLENT].green,
        p_colors->colors[AIR_QUALITY_INDEX_EXCELLENT].blue,
        p_colors->colors[AIR_QUALITY_INDEX_GOOD].red,
        p_colors->colors[AIR_QUALITY_INDEX_GOOD].green,
        p_colors->colors[AIR_QUALITY_INDEX_GOOD].blue,
        p_colors->colors[AIR_QUALITY_INDEX_FAIR].red,
        p_colors->colors[AIR_QUALITY_INDEX_FAIR].green,
        p_colors->colors[AIR_QUALITY_INDEX_FAIR].blue,
        p_colors->colors[AIR_QUALITY_INDEX_POOR].red,
        p_colors->colors[AIR_QUALITY_INDEX_POOR].green,
        p_colors->colors[AIR_QUALITY_INDEX_POOR].blue,
        p_colors->colors[AIR_QUALITY_INDEX_VERY_POOR].red,
        p_colors->colors[AIR_QUALITY_INDEX_VERY_POOR].green,
        p_colors->colors[AIR_QUALITY_INDEX_VERY_POOR].blue);
}

static manual_brightness_level_e
get_brightness_level_from_str(const char* const p_brightness)
{
    manual_brightness_level_e brightness_level = MANUAL_BRIGHTNESS_LEVEL_OFF;

    if (0 == strcmp(p_brightness, APP_SETTINGS_VAL_LED_BRIGHTNESS_NIGHT))
    {
        brightness_level = MANUAL_BRIGHTNESS_LEVEL_NIGHT;
    }
    else if (0 == strcmp(p_brightness, APP_SETTINGS_VAL_LED_BRIGHTNESS_DAY))
    {
        brightness_level = MANUAL_BRIGHTNESS_LEVEL_DAY;
    }
    else if (0 == strcmp(p_brightness, APP_SETTINGS_VAL_LED_BRIGHTNESS_BRIGHT_DAY))
    {
        brightness_level = MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY;
    }
    return brightness_level;
}

static int
cmd_ruuvi_led_get_color_table(const struct shell* sh, size_t argc, char** argv)
{
    log_args(argc, argv);

    const char* const p_brightness = argv[1];

    manual_brightness_level_e brightness_level = get_brightness_level_from_str(p_brightness);
    if (MANUAL_BRIGHTNESS_LEVEL_OFF == brightness_level)
    {
        shell_error(sh, "Invalid brightness value: %s", p_brightness);
        return -EINVAL;
    }
    const manual_brightness_color_t* const p_colors = aqi_get_colors_table(brightness_level);
    print_led_color_table(sh, p_brightness, p_colors);
    return 0;
}

static bool
parse_rgb_values(const struct shell* sh, char** argv, int* const p_arg_idx, rgb_led_color_t* const p_colors)
{
    int arg_idx = *p_arg_idx;
    if (!parse_uint8_print_err(sh, argv[arg_idx++], &p_colors->red))
    {
        return false;
    }
    if (!parse_uint8_print_err(sh, argv[arg_idx++], &p_colors->green))
    {
        return false;
    }
    if (!parse_uint8_print_err(sh, argv[arg_idx++], &p_colors->blue))
    {
        return false;
    }
    *p_arg_idx = arg_idx;
    return true;
}

static int
cmd_ruuvi_led_set_color_table(const struct shell* sh, size_t argc, char** argv)
{
    log_args(argc, argv);

    const char* const p_brightness = argv[1];

    manual_brightness_level_e brightness_level = get_brightness_level_from_str(p_brightness);
    if (MANUAL_BRIGHTNESS_LEVEL_OFF == brightness_level)
    {
        shell_error(sh, "Invalid brightness value: %s", p_brightness);
        return -EINVAL;
    }

    manual_brightness_color_t table = {
        .currents = { 0 },
        .colors   = { { 0 } },
    };
    int arg_idx = 2;
    if (!parse_uint8_print_err(sh, argv[arg_idx++], &table.currents.current_red))
    {
        return -EINVAL;
    }
    if (!parse_uint8_print_err(sh, argv[arg_idx++], &table.currents.current_green))
    {
        return -EINVAL;
    }
    if (!parse_uint8_print_err(sh, argv[arg_idx++], &table.currents.current_blue))
    {
        return -EINVAL;
    }
    for (air_quality_index_e quality_idx = AIR_QUALITY_INDEX_EXCELLENT; quality_idx <= AIR_QUALITY_INDEX_VERY_POOR;
         ++quality_idx)
    {
        if (!parse_rgb_values(sh, argv, &arg_idx, &table.colors[quality_idx]))
        {
            return -EINVAL;
        }
    }
    aqi_set_colors_table(brightness_level, &table);
    const manual_brightness_color_t* const p_colors = aqi_get_colors_table(brightness_level);
    print_led_color_table(sh, p_brightness, p_colors);
    app_settings_set_led_color_table(brightness_level, &table);
    return 0;
}

static int
cmd_ruuvi_led_reset_color_table(const struct shell* sh, size_t argc, char** argv)
{
    log_args(argc, argv);

    const char* const p_brightness = argv[1];

    manual_brightness_level_e brightness_level = get_brightness_level_from_str(p_brightness);
    if (MANUAL_BRIGHTNESS_LEVEL_OFF == brightness_level)
    {
        shell_error(sh, "Invalid brightness value: %s", p_brightness);
        return -EINVAL;
    }
    aqi_reset_colors_table(brightness_level);
    const manual_brightness_color_t* const p_colors = aqi_get_colors_table(brightness_level);
    print_led_color_table(sh, p_brightness, p_colors);
    app_settings_reset_led_color_table(brightness_level);
    return 0;
}

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
static int
cmd_ruuvi_version_info(const struct shell* sh, size_t argc, char** argv)
{
    log_args(argc, argv);

    const char* const p_version_str   = app_fw_ver_get();
    const size_t      version_str_len = strlen(p_version_str);
    const char* const p_prod_suffix   = "-prod";
    const bool        is_prod         = (version_str_len >= strlen(p_prod_suffix))
                         && (0 == strcmp(&p_version_str[version_str_len - strlen(p_prod_suffix)], p_prod_suffix));

    struct image_version  fw_ver    = { 0 };
    const struct fw_info* p_fw_info = NULL;
    fw_image_hw_rev_t     hw_rev    = { 0 };
    if (!fw_img_get_image_info(FW_IMG_ID_APP, &fw_ver, &p_fw_info, &hw_rev))
    {
        shell_error(sh, "Failed to get firmware image info");
        return -EINVAL;
    }

    shell_print(sh, "Hardware revision: %s", hw_rev.hw_rev_name);
    shell_print(sh, "Build type: %s", is_prod ? "production" : "development");

    shell_print(
        sh,
        "App version: %u.%u.%u+%u",
        fw_ver.iv_major,
        fw_ver.iv_minor,
        fw_ver.iv_revision,
        fw_ver.iv_build_num);

    if (!fw_img_get_image_info(FW_IMG_ID_FWLOADER, &fw_ver, &p_fw_info, &hw_rev))
    {
        shell_error(sh, "Failed to get firmware image info");
        return -EINVAL;
    }
    shell_print(
        sh,
        "FwLoader version: %u.%u.%u+%u",
        fw_ver.iv_major,
        fw_ver.iv_minor,
        fw_ver.iv_revision,
        fw_ver.iv_build_num);

    if (!fw_img_get_image_info(FW_IMG_ID_MCUBOOT0, &fw_ver, &p_fw_info, &hw_rev))
    {
        shell_error(sh, "Failed to get firmware image info");
        return -EINVAL;
    }
    shell_print(
        sh,
        "MCUBoot0 version: %u.%u.%u+%u",
        fw_ver.iv_major,
        fw_ver.iv_minor,
        fw_ver.iv_revision,
        fw_ver.iv_build_num);

    if (!fw_img_get_image_info(FW_IMG_ID_MCUBOOT1, &fw_ver, &p_fw_info, &hw_rev))
    {
        shell_error(sh, "Failed to get firmware image info");
        return -EINVAL;
    }
    shell_print(
        sh,
        "MCUBoot1 version: %u.%u.%u+%u",
        fw_ver.iv_major,
        fw_ver.iv_minor,
        fw_ver.iv_revision,
        fw_ver.iv_build_num);

    return 0;
}
#endif // CONFIG_BOOTLOADER_MCUBOOT

/* Add command to the set of 'ruuvi' subcommands, see `SHELL_SUBCMD_ADD` */
#define RUUVI_CMD_ARG_ADD(_syntax, _subcmd, _help, _handler, _mand, _opt) \
    SHELL_SUBCMD_ADD((ruuvi), _syntax, _subcmd, _help, _handler, _mand, _opt);

RUUVI_CMD_ARG_ADD(echo, NULL, "message", cmd_ruuvi_echo, 2, 0);
RUUVI_CMD_ARG_ADD(
    led_brightness,
    NULL,
    "led_brightness <off|night|day|bright_day|0-100%%|0.0-100.0%%>",
    cmd_ruuvi_led_brightness,
    2,
    0);
RUUVI_CMD_ARG_ADD(
    led_write_channels,
    NULL,
    "led_write_channels <Cur_R [0-255]> <Cur_G [0-255]> <Cur_B [0-255]> <PWM_R [0-255]> <PWM_G [0-255]> <PWM_B "
    "[0-255]>",
    cmd_ruuvi_led_write_channels,
    7,
    0);
RUUVI_CMD_ARG_ADD(
    led_get_color_table,
    NULL,
    "led_get_color_table <night|day|bright_day>",
    cmd_ruuvi_led_get_color_table,
    2,
    0);
RUUVI_CMD_ARG_ADD(
    led_set_color_table,
    NULL,
    "led_set_color_table <night|day|bright_day> <C_R> <C_G> <C_B> <R1> <G1> <B1> <R2> <G2> <B2> <R3> <G3> <B3> <R4> "
    "<G4> <B4> <R5> <G5> <B5>",
    cmd_ruuvi_led_set_color_table,
    20,
    0);
RUUVI_CMD_ARG_ADD(
    led_reset_color_table,
    NULL,
    "led_reset_color_table <night|day|bright_day>",
    cmd_ruuvi_led_reset_color_table,
    2,
    0);

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
RUUVI_CMD_ARG_ADD(version_info, NULL, "version_info", cmd_ruuvi_version_info, 1, 0);
#endif // CONFIG_BOOTLOADER_MCUBOOT

SHELL_SUBCMD_SET_CREATE(ruuvi_cmds, (ruuvi));
SHELL_CMD_REGISTER(ruuvi, &ruuvi_cmds, "Ruuvi commands", NULL);
