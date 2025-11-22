/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_settings.h"
#include <time.h>
#include <ctype.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include "utils.h"
#include "sys_utils.h"
#include "aqi.h"
#include "app_fw_ver.h"
#include "app_version.h"
#include "zephyr_api.h"
#include "tlog.h"

LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_INF);

#define APP_SETTINGS_MAX_VAL_LEN (64)

#define APP_SETTINGS_KEY_PREFIX_BT_DIS "bt/dis"

#define APP_SETTINGS_KEY_BT_DIS_MODEL  "model"
#define APP_SETTINGS_KEY_BT_DIS_MANUF  "manuf"
#define APP_SETTINGS_KEY_BT_DIS_SERIAL "serial"
#define APP_SETTINGS_KEY_BT_DIS_SW     "sw"
#define APP_SETTINGS_KEY_BT_DIS_FW     "fw"
#define APP_SETTINGS_KEY_BT_DIS_HW     "hw"

#define APP_SETTINGS_FULL_KEY_BT_DIS_MODEL  APP_SETTINGS_KEY_PREFIX_BT_DIS "/" APP_SETTINGS_KEY_BT_DIS_MODEL
#define APP_SETTINGS_FULL_KEY_BT_DIS_MANUF  APP_SETTINGS_KEY_PREFIX_BT_DIS "/" APP_SETTINGS_KEY_BT_DIS_MANUF
#define APP_SETTINGS_FULL_KEY_BT_DIS_SERIAL APP_SETTINGS_KEY_PREFIX_BT_DIS "/" APP_SETTINGS_KEY_BT_DIS_SERIAL
#define APP_SETTINGS_FULL_KEY_BT_DIS_SW     APP_SETTINGS_KEY_PREFIX_BT_DIS "/" APP_SETTINGS_KEY_BT_DIS_SW
#define APP_SETTINGS_FULL_KEY_BT_DIS_FW     APP_SETTINGS_KEY_PREFIX_BT_DIS "/" APP_SETTINGS_KEY_BT_DIS_FW
#define APP_SETTINGS_FULL_KEY_BT_DIS_HW     APP_SETTINGS_KEY_PREFIX_BT_DIS "/" APP_SETTINGS_KEY_BT_DIS_HW

#define APP_SETTINGS_KEY_PREFIX_APP                 "app"
#define APP_SETTINGS_KEY_SEN66_VOC_ALGORITHM_STATE  "sen66/voc_algorithm_state"
#define APP_SETTINGS_KEY_LED_BRIGHTNESS             "led/brightness"
#define APP_SETTINGS_KEY_LED_COLOR_TABLE_NIGHT      "led/color_table_night"
#define APP_SETTINGS_KEY_LED_COLOR_TABLE_DAY        "led/color_table_day"
#define APP_SETTINGS_KEY_LED_COLOR_TABLE_BRIGHT_DAY "led/color_table_bright_day"

#define APP_SETTINGS_LED_MANUAL_PERCENTAGE_PWM_LIMIT_DECI_PERCENT (25 * 10)

typedef struct device_id_str_t
{
    char serial_number[8 * 3]; // "XX:XX:XX:XX:XX:XX:XX:XX"
} device_id_str_t;

typedef struct firmware_version_str_t
{
    char fw_version[sizeof(CONFIG_BT_DEVICE_NAME) + 2 + sizeof(APP_VERSION_EXTENDED_STRING)];
} firmware_version_str_t;

typedef void (*handle_key_cb_t)(const char* const p_key, const char* const p_buf, const size_t len);

typedef struct settings_raw_color_table_t
{
    uint8_t data[18];
} settings_raw_color_table_t;

static bool g_flag_config_mode = false;

#if defined(CONFIG_BT_DIS_SETTINGS)
bool g_flag_bt_dis_model_set  = false;
bool g_flag_bt_dis_manuf_set  = false;
bool g_flag_bt_dis_serial_set = false;
bool g_flag_bt_dis_sw_set     = false;
bool g_flag_bt_dis_fw_set     = false;
bool g_flag_bt_dis_hw_set     = false;
#endif // CONFIG_BT_DIS_SETTINGS

static app_settings_sen66_voc_algorithm_state_t g_sen66_voc_algorithm_state = {
    .unix_timestamp = 0,
    .state = {
        .voc_state = APP_SETTINGS_SEN66_VOC_ALGORITHM_STATE_DEFAULT,
    },
};
K_MUTEX_DEFINE(g_sen66_voc_algorithm_state_mutex);

enum app_settings_led_mode_e               g_led_mode                     = APP_SETTINGS_LED_MODE_MANUAL_DAY;
app_settings_led_brightness_deci_percent_t g_led_mode_manual_deci_percent = APP_SETTINGS_LED_BRIGHTNESS_DAY_VALUE * 10;

static bool
check_is_buf_printable(const uint8_t* const p_buf, const ssize_t len)
{
    for (ssize_t i = 0; i < (len - 1); ++i)
    {
        if (('\0' == p_buf[i]) || (0 == isprint((int)p_buf[i])))
        {
            return false;
        }
    }
    if ((p_buf[len - 1] != '\0') && (0 == isprint((int)p_buf[len - 1])))
    {
        return false;
    }
    return true;
}

static int // NOSONAR: Zephyr API
cb_direct_handle_keys(const char* key, size_t len, settings_read_cb read_cb, void* cb_arg, void* param)
{
    ARG_UNUSED(param);
    char            buf[APP_SETTINGS_MAX_VAL_LEN];
    handle_key_cb_t p_cb = param;

    if (len >= (sizeof(buf) - 1))
    {
        TLOG_WRN("Value for \"%s\" too long (%u bytes)", key, (unsigned)len);
        return 0;
    }

    ssize_t rlen = read_cb(cb_arg, buf, len);
    if (rlen < 0)
    {
        TLOG_ERR("read_cb failed for \"%s\": %d", key, (int)rlen);
        return 0;
    }
    if (rlen >= (sizeof(buf) - 1))
    {
        TLOG_ERR("Value for \"%s\" too long (%u bytes)", key, (unsigned)rlen);
        return 0;
    }
    buf[rlen] = '\0';

    if (rlen > 0)
    {
        const bool printable = check_is_buf_printable(buf, rlen);
        if (printable)
        {
            LOG_INF("  - key: %s: '%s'", key, buf);
        }
        else
        {
            LOG_INF("  - key: %s: (len=%d):", key, (int)rlen);
            LOG_HEXDUMP_INF(buf, rlen, "    value(hex):");
        }
    }
    else
    {
        LOG_INF("  - key: %s: <empty>", key);
    }
    if (NULL != p_cb)
    {
        p_cb(key, buf, (size_t)rlen);
    }
    return 0;
}

static device_id_str_t
get_device_id_str(void)
{
    device_id_str_t device_id_str = { 0 };
    if (g_flag_config_mode)
    {
        const uint64_t device_id = get_device_id();
        snprintf(
            device_id_str.serial_number,
            sizeof(device_id_str.serial_number),
            "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
            (uint8_t)(device_id >> BYTE_SHIFT_7) & BYTE_MASK,
            (uint8_t)(device_id >> BYTE_SHIFT_6) & BYTE_MASK,
            (uint8_t)(device_id >> BYTE_SHIFT_5) & BYTE_MASK,
            (uint8_t)(device_id >> BYTE_SHIFT_4) & BYTE_MASK,
            (uint8_t)(device_id >> BYTE_SHIFT_3) & BYTE_MASK,
            (uint8_t)(device_id >> BYTE_SHIFT_2) & BYTE_MASK,
            (uint8_t)(device_id >> BYTE_SHIFT_1) & BYTE_MASK,
            (uint8_t)(device_id >> BYTE_SHIFT_0) & BYTE_MASK);
    }
    else
    {
        snprintf(
            device_id_str.serial_number,
            sizeof(device_id_str.serial_number),
            "%s",
            CONFIG_BT_DIS_SERIAL_NUMBER_STR);
    }
    return device_id_str;
}

static firmware_version_str_t
get_firmware_version_str(void)
{
    firmware_version_str_t fw_ver_str = { 0 };
    snprintf(fw_ver_str.fw_version, sizeof(fw_ver_str.fw_version), "%s v%s", CONFIG_BT_DEVICE_NAME, app_fw_ver_get());
    return fw_ver_str;
}

#if defined(CONFIG_BT_DIS_SETTINGS)
static void
handle_bt_key(const char* const p_key, const char* const p_buf, const size_t len)
{
    if (0 == strcmp(APP_SETTINGS_KEY_BT_DIS_MODEL, p_key))
    {
        if (0 == strncmp(CONFIG_BT_DIS_MODEL, p_buf, len))
        {
            g_flag_bt_dis_model_set = true;
        }
    }
    else if (0 == strcmp(APP_SETTINGS_KEY_BT_DIS_MANUF, p_key))
    {
        if (0 == strncmp(CONFIG_BT_DIS_MANUF, p_buf, len))
        {
            g_flag_bt_dis_manuf_set = true;
        }
    }
#if defined(CONFIG_BT_DIS_SERIAL_NUMBER)
    else if (0 == strcmp(APP_SETTINGS_KEY_BT_DIS_SERIAL, p_key))
    {
        const device_id_str_t device_id_str = get_device_id_str();
        if (0 == strncmp(device_id_str.serial_number, p_buf, len))
        {
            g_flag_bt_dis_serial_set = true;
        }
    }
#endif // CONFIG_BT_DIS_SERIAL_NUMBER
#if defined(CONFIG_BT_DIS_SW_REV)
    else if (0 == strcmp(APP_SETTINGS_KEY_BT_DIS_SW, p_key))
    {
        if (0 == strncmp(CONFIG_BT_DIS_SW_REV_STR, p_buf, len))
        {
            g_flag_bt_dis_sw_set = true;
        }
    }
#endif // CONFIG_BT_DIS_SW_REV
#if defined(CONFIG_BT_DIS_FW_REV)
    else if (0 == strcmp(APP_SETTINGS_KEY_BT_DIS_FW, p_key))
    {
        const firmware_version_str_t fw_ver = get_firmware_version_str();
        if (0 == strncmp(fw_ver.fw_version, p_buf, len))
        {
            g_flag_bt_dis_fw_set = true;
        }
    }
#endif // CONFIG_BT_DIS_FW_REV
#if defined(CONFIG_BT_DIS_HW_REV)
    else if (0 == strcmp(APP_SETTINGS_KEY_BT_DIS_HW, p_key))
    {
        if (0 == strncmp(app_hw_rev_get(), p_buf, len))
        {
            g_flag_bt_dis_hw_set = true;
        }
    }
#endif // CONFIG_BT_DIS_HW_REV
    else
    {
        LOG_WRN("Unhandled key: %s", p_key);
    }
}
#endif // CONFIG_BT_DIS_SETTINGS

static void
app_settings_save_key(const char* const p_key, const char* const p_val, const size_t val_len)
{
    LOG_INF("Saving key: %s=%.*s", p_key, (int)val_len, p_val);
    const zephyr_api_ret_t err = settings_save_one(p_key, p_val, val_len);
    if (0 != err)
    {
        TLOG_ERR("settings_save_one failed for key '%s': %d", p_key, err);
    }
}

static void
app_settings_save_bin_key(const char* const p_key, const uint8_t* const p_val, const size_t val_len)
{
    LOG_INF("Saving key: %s", p_key);
    LOG_HEXDUMP_INF(p_val, val_len, "    value(hex):");
    const zephyr_api_ret_t err = settings_save_one(p_key, p_val, val_len);
    if (0 != err)
    {
        TLOG_ERR("settings_save_one failed for key '%s': %d", p_key, err);
    }
}

static void
app_settings_delete_key(const char* const p_key)
{
    LOG_INF("Deleting key: %s", p_key);
    const zephyr_api_ret_t err = settings_delete(p_key);
    if (0 != err)
    {
        TLOG_ERR("settings_delete failed for key '%s': %d", p_key, err);
    }
}

bool
app_settings_init(void)
{
    g_sen66_voc_algorithm_state = (app_settings_sen66_voc_algorithm_state_t){
        .unix_timestamp = (uint32_t)time(NULL),
        .state = {
            .voc_state = APP_SETTINGS_SEN66_VOC_ALGORITHM_STATE_DEFAULT,
        },
    };
    TLOG_INF(
        "SEN66: Init default VOC algorithm state: timestamp=%u, state: %u, %u, %u, %u",
        g_sen66_voc_algorithm_state.unix_timestamp,
        g_sen66_voc_algorithm_state.state.voc_state[0],
        g_sen66_voc_algorithm_state.state.voc_state[1],
        g_sen66_voc_algorithm_state.state.voc_state[2],
        g_sen66_voc_algorithm_state.state.voc_state[3]);

    settings_subsys_init();
    TLOG_INF("Loading settings from backend...");
    zephyr_api_ret_t err = settings_load();
    if (0 != err)
    {
        TLOG_ERR("Settings loading failed: %d", err);
    }
    else
    {
        TLOG_INF("Settings loaded successfully");
    }
    /* Handle every key directly from the backend */
#if defined(CONFIG_BT_DIS_SETTINGS)
    TLOG_INF("Checking '%s' settings:", APP_SETTINGS_KEY_PREFIX_BT_DIS);
    err = settings_load_subtree_direct("bt/dis", &cb_direct_handle_keys, (void*)&handle_bt_key);
    if (0 != err)
    {
        TLOG_WRN("settings_load_subtree_direct: %d", err);
    }
#endif // CONFIG_BT_DIS_SETTINGS

#if defined(CONFIG_BT_DIS_SETTINGS)
    if (!g_flag_bt_dis_model_set)
    {
        app_settings_save_key(APP_SETTINGS_FULL_KEY_BT_DIS_MODEL, CONFIG_BT_DIS_MODEL, sizeof(CONFIG_BT_DIS_MODEL));
        app_settings_save_key(APP_SETTINGS_FULL_KEY_BT_DIS_MANUF, CONFIG_BT_DIS_MANUF, sizeof(CONFIG_BT_DIS_MANUF));
    }
#if defined(CONFIG_BT_DIS_SERIAL_NUMBER)
    if (!g_flag_bt_dis_serial_set)
    {
        const device_id_str_t device_id_str = get_device_id_str();
        app_settings_save_key(
            APP_SETTINGS_FULL_KEY_BT_DIS_SERIAL,
            device_id_str.serial_number,
            strlen(device_id_str.serial_number) + 1);
    }
#endif
#if defined(CONFIG_BT_DIS_SW_REV)
    if (!g_flag_bt_dis_sw_set)
    {
        app_settings_save_bt_dis(
            APP_SETTINGS_FULL_KEY_BT_DIS_SW,
            CONFIG_BT_DIS_SW_REV_STR,
            sizeof(CONFIG_BT_DIS_SW_REV_STR));
    }
#endif
#if defined(CONFIG_BT_DIS_FW_REV)
    if (!g_flag_bt_dis_fw_set)
    {
        const firmware_version_str_t fw_ver = get_firmware_version_str();
        app_settings_save_key(APP_SETTINGS_FULL_KEY_BT_DIS_FW, fw_ver.fw_version, strlen(fw_ver.fw_version) + 1);
    }
#endif
#if defined(CONFIG_BT_DIS_HW_REV)
    if (!g_flag_bt_dis_hw_set)
    {
        const char* const p_hw_rev = app_hw_rev_get();
        app_settings_save_key(APP_SETTINGS_FULL_KEY_BT_DIS_HW, p_hw_rev, strlen(p_hw_rev) + 1);
    }
#endif
    bool flag_reload_settings = false;
    flag_reload_settings      = flag_reload_settings || (!g_flag_bt_dis_model_set);
    flag_reload_settings      = flag_reload_settings || (!g_flag_bt_dis_manuf_set);
    flag_reload_settings      = flag_reload_settings || (!g_flag_bt_dis_serial_set);
    flag_reload_settings      = flag_reload_settings || (!g_flag_bt_dis_sw_set);
    flag_reload_settings      = flag_reload_settings || (!g_flag_bt_dis_fw_set);
    flag_reload_settings      = flag_reload_settings || (!g_flag_bt_dis_hw_set);
    if (flag_reload_settings)
    {
        // Reload settings to update in-memory values for BLE subsystem.
        app_settings_reload();
    }
#endif
    return true;
}

enum app_settings_led_mode_e
app_settings_get_led_mode(void)
{
    return g_led_mode;
}

app_settings_led_brightness_deci_percent_t
app_settings_get_led_brightness_deci_percent(void)
{
    return g_led_mode_manual_deci_percent;
}

rgb_led_brightness_t
app_settings_conv_deci_percent_to_brightness(
    const app_settings_led_brightness_deci_percent_t brightness_deci_percent,
    uint8_t* const                                   p_dim_pwm)
{
    const uint8_t led_brightness_min   = APP_SETTINGS_LED_BRIGHTNESS_NIGHT_VALUE;
    const uint8_t led_brightness_max   = RGB_LED_BRIGHTNESS_MAX;
    const uint8_t led_brightness_range = (uint8_t)(led_brightness_max - led_brightness_min);

    rgb_led_brightness_t led_brightness = 0;
    uint8_t              dim_pwm        = 0;

    if (g_led_mode_manual_deci_percent < APP_SETTINGS_LED_MANUAL_PERCENTAGE_PWM_LIMIT_DECI_PERCENT)
    {
        led_brightness = led_brightness_min;
        dim_pwm        = (uint8_t)(((RGB_LED_PWM_MAX * brightness_deci_percent)
                             + (APP_SETTINGS_LED_MANUAL_PERCENTAGE_PWM_LIMIT_DECI_PERCENT / ROUND_HALF_DIVISOR))
                            / APP_SETTINGS_LED_MANUAL_PERCENTAGE_PWM_LIMIT_DECI_PERCENT);
    }
    else
    {
        const uint32_t brightness_min_deci_percent   = APP_SETTINGS_LED_MANUAL_PERCENTAGE_PWM_LIMIT_DECI_PERCENT;
        const uint32_t brightness_max_deci_percent   = PERCENT_100 * DECI_PERCENT_PER_PERCENT;
        const uint32_t brightness_range_deci_percent = brightness_max_deci_percent - brightness_min_deci_percent;

        led_brightness = (rgb_led_brightness_t)(((((brightness_deci_percent - brightness_min_deci_percent)
                                                   * led_brightness_range)
                                                  + (brightness_range_deci_percent / ROUND_HALF_DIVISOR))
                                                 / brightness_range_deci_percent)
                                                + led_brightness_min);
        dim_pwm        = RGB_LED_PWM_MAX;
    }
    if (NULL != p_dim_pwm)
    {
        *p_dim_pwm = dim_pwm;
    }
    return led_brightness;
}

rgb_led_brightness_t
app_settings_get_led_brightness(void)
{
    switch (g_led_mode)
    {
        case APP_SETTINGS_LED_MODE_DISABLED:
            return 0;

        case APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY:
            return APP_SETTINGS_LED_BRIGHTNESS_BRIGHT_DAY_VALUE;
        case APP_SETTINGS_LED_MODE_MANUAL_DAY:
            return APP_SETTINGS_LED_BRIGHTNESS_DAY_VALUE;
        case APP_SETTINGS_LED_MODE_MANUAL_NIGHT:
            return APP_SETTINGS_LED_BRIGHTNESS_NIGHT_VALUE;
        case APP_SETTINGS_LED_MODE_MANUAL_OFF:
            return 0;

        case APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE:
            return app_settings_conv_deci_percent_to_brightness(g_led_mode_manual_deci_percent, NULL);

        case APP_SETTINGS_LED_MODE_AUTO:
            return 0;
    }
    return 0;
}

bool
app_settings_is_led_mode_auto(void)
{
    if (APP_SETTINGS_LED_MODE_AUTO == app_settings_get_led_mode())
    {
        return true;
    }
    return false;
}

void
app_settings_set_led_mode(const enum app_settings_led_mode_e mode)
{
    g_led_mode = mode;
    TLOG_INF("LED mode set to %d", g_led_mode);

    const char* p_val = "";
    switch (g_led_mode)
    {
        case APP_SETTINGS_LED_MODE_DISABLED:
            p_val = APP_SETTINGS_VAL_LED_BRIGHTNESS_DISABLED;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY:
            p_val = APP_SETTINGS_VAL_LED_BRIGHTNESS_BRIGHT_DAY;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_DAY:
            p_val = APP_SETTINGS_VAL_LED_BRIGHTNESS_DAY;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_NIGHT:
            p_val = APP_SETTINGS_VAL_LED_BRIGHTNESS_NIGHT;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_OFF:
            p_val = APP_SETTINGS_VAL_LED_BRIGHTNESS_OFF;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE:
            TLOG_ERR("Use app_settings_set_led_brightness to set percentage brightness");
            break;
        case APP_SETTINGS_LED_MODE_AUTO:
            p_val = APP_SETTINGS_VAL_LED_BRIGHTNESS_AUTO;
            break;
        default:
            TLOG_WRN("Unknown LED mode: %d", g_led_mode);
            break;
    }
    app_settings_save_key(APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_BRIGHTNESS, p_val, strlen(p_val) + 1);
}

void
app_settings_set_led_color_table(
    manual_brightness_level_e              brightness_level,
    const manual_brightness_color_t* const p_table)
{
    const settings_raw_color_table_t raw_table = { .data = {
                                                       p_table->currents.current_red,
                                                       p_table->currents.current_green,
                                                       p_table->currents.current_blue,
                                                       p_table->colors[AIR_QUALITY_INDEX_EXCELLENT].red,
                                                       p_table->colors[AIR_QUALITY_INDEX_EXCELLENT].green,
                                                       p_table->colors[AIR_QUALITY_INDEX_EXCELLENT].blue,
                                                       p_table->colors[AIR_QUALITY_INDEX_GOOD].red,
                                                       p_table->colors[AIR_QUALITY_INDEX_GOOD].green,
                                                       p_table->colors[AIR_QUALITY_INDEX_GOOD].blue,
                                                       p_table->colors[AIR_QUALITY_INDEX_FAIR].red,
                                                       p_table->colors[AIR_QUALITY_INDEX_FAIR].green,
                                                       p_table->colors[AIR_QUALITY_INDEX_FAIR].blue,
                                                       p_table->colors[AIR_QUALITY_INDEX_POOR].red,
                                                       p_table->colors[AIR_QUALITY_INDEX_POOR].green,
                                                       p_table->colors[AIR_QUALITY_INDEX_POOR].blue,
                                                       p_table->colors[AIR_QUALITY_INDEX_VERY_POOR].red,
                                                       p_table->colors[AIR_QUALITY_INDEX_VERY_POOR].green,
                                                       p_table->colors[AIR_QUALITY_INDEX_VERY_POOR].blue,
                                                   } };
    switch (brightness_level)
    {
        case MANUAL_BRIGHTNESS_LEVEL_NIGHT:
            app_settings_save_bin_key(
                APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_COLOR_TABLE_NIGHT,
                &raw_table.data[0],
                sizeof(raw_table.data));
            break;
        case MANUAL_BRIGHTNESS_LEVEL_DAY:
            app_settings_save_bin_key(
                APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_COLOR_TABLE_DAY,
                &raw_table.data[0],
                sizeof(raw_table.data));
            break;
        case MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY:
            app_settings_save_bin_key(
                APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_COLOR_TABLE_BRIGHT_DAY,
                &raw_table.data[0],
                sizeof(raw_table.data));
            break;
        default:
            TLOG_ERR("Invalid brightness level: %d", brightness_level);
            return;
    }
}

void
app_settings_reset_led_color_table(manual_brightness_level_e brightness_level)
{
    switch (brightness_level)
    {
        case MANUAL_BRIGHTNESS_LEVEL_NIGHT:
            app_settings_delete_key(APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_COLOR_TABLE_NIGHT);
            break;
        case MANUAL_BRIGHTNESS_LEVEL_DAY:
            app_settings_delete_key(APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_COLOR_TABLE_DAY);
            break;
        case MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY:
            app_settings_delete_key(APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_COLOR_TABLE_BRIGHT_DAY);
            break;
        default:
            TLOG_ERR("Invalid brightness level: %d", brightness_level);
            return;
    }
}

static bool
parse_deci_percent(const char* const p_str, app_settings_led_brightness_deci_percent_t* const p_deci_percent)
{
    char*    p_end    = NULL;
    uint32_t int_part = strtoul(p_str, &p_end, BASE_10);
    if (NULL == p_end)
    {
        return false;
    }
    if (('%' == *p_end) && (int_part <= PERCENT_100))
    {
        *p_deci_percent = (app_settings_led_brightness_deci_percent_t)(int_part * DECI_PERCENT_PER_PERCENT);
        return true;
    }
    if ('.' == *p_end)
    {
        const char* p_frac = p_end + 1;
        p_end              = NULL;
        uint32_t frac_part = strtoul(p_frac, &p_end, BASE_10);
        if ((NULL != p_end) && ('%' == *p_end) && (int_part <= PERCENT_100) && (frac_part < BASE_10))
        {
            const uint32_t deci_percent = (int_part * DECI_PERCENT_PER_PERCENT) + frac_part;
            if (deci_percent <= (PERCENT_100 * DECI_PERCENT_PER_PERCENT))
            {
                *p_deci_percent = (app_settings_led_brightness_deci_percent_t)deci_percent;
                return true;
            }
        }
    }
    return false;
}

bool
app_settings_set_led_mode_manual_percentage(const char* const p_str_brightness_deci_percent)
{
    app_settings_led_brightness_deci_percent_t brightness_deci_percent = 0;
    if (!parse_deci_percent(p_str_brightness_deci_percent, &brightness_deci_percent))
    {
        return false;
    }
    g_led_mode                     = APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE;
    g_led_mode_manual_deci_percent = brightness_deci_percent;
    TLOG_INF(
        "LED mode set to %d (APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE), brightness=%u.%01u",
        g_led_mode,
        brightness_deci_percent / DECI_PERCENT_PER_PERCENT,
        brightness_deci_percent % DECI_PERCENT_PER_PERCENT);
    app_settings_save_key(
        APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_LED_BRIGHTNESS,
        p_str_brightness_deci_percent,
        strlen(p_str_brightness_deci_percent) + 1);
    return true;
}

void
app_settings_set_next_led_mode(void)
{
    switch (g_led_mode)
    {
        case APP_SETTINGS_LED_MODE_DISABLED:
            TLOG_INF("Do not switch LED mode in DISABLED mode");
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY:
            TLOG_INF("Switch LED mode BRIGHT_DAY -> DAY");
            app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_DAY);
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_DAY:
            TLOG_INF("Switch LED mode DAY -> NIGHT");
            app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_NIGHT);
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_NIGHT:
            TLOG_INF("Switch LED mode NIGHT -> OFF");
            app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_OFF);
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_OFF:
            TLOG_INF("Switch LED mode OFF -> BRIGHT_DAY");
            app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY);
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_PERCENTAGE:
            TLOG_INF("Switch LED mode PERCENTAGE -> DAY");
            app_settings_set_led_mode(APP_SETTINGS_LED_MODE_MANUAL_DAY);
            break;
        case APP_SETTINGS_LED_MODE_AUTO:
            TLOG_INF("Do not switch LED mode in AUTO mode");
            break;
        default:
            TLOG_INF("Unknown LED mode=%d", g_led_mode);
            break;
    }
}

static void
app_settings_log_key(const char* key, const uint8_t* buf, ssize_t len, bool flag_binary)
{
    if (0 == len)
    {
        LOG_INF("App settings: %s/%s: <empty>", APP_SETTINGS_KEY_PREFIX_APP, key);
        return;
    }

    const bool printable = check_is_buf_printable(buf, len);
    if (printable && (!flag_binary))
    {
        LOG_INF("App settings: %s/%s: '%.*s'", APP_SETTINGS_KEY_PREFIX_APP, key, (int)len, buf);
    }
    else
    {
        LOG_INF("App settings: %s/%s: (len=%d):", APP_SETTINGS_KEY_PREFIX_APP, key, (int)len);
        LOG_HEXDUMP_INF(buf, len, "App settings:     value(hex):");
    }
}

static void
app_settings_handler_set_sen66_voc_algorithm_state(const char* const key, const char* const buf, const ssize_t rlen)
{
    app_settings_log_key(key, buf, rlen, true);
    if (rlen == sizeof(g_sen66_voc_algorithm_state))
    {
        memcpy(&g_sen66_voc_algorithm_state, buf, sizeof(g_sen66_voc_algorithm_state));
        TLOG_INF(
            "SEN66: Loaded VOC algorithm state: timestamp=%u, state: %u, %u, %u, %u",
            g_sen66_voc_algorithm_state.unix_timestamp,
            g_sen66_voc_algorithm_state.state.voc_state[0],
            g_sen66_voc_algorithm_state.state.voc_state[1],
            g_sen66_voc_algorithm_state.state.voc_state[2],
            g_sen66_voc_algorithm_state.state.voc_state[3]);
    }
    else
    {
        TLOG_WRN(
            "Invalid length for key \"%s\": %d (expected %d)",
            key,
            (int)rlen,
            (int)sizeof(g_sen66_voc_algorithm_state));
    }
}

static void
app_settings_handler_set_led_brightness(const char* const p_key, const char* const p_val)
{
    app_settings_log_key(p_key, p_val, strlen(p_val), false);
    if (0 == strcmp(p_val, APP_SETTINGS_VAL_LED_BRIGHTNESS_BRIGHT_DAY))
    {
        g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY;
        TLOG_INF("LED brightness from settings: BRIGHT_DAY");
    }
    else if (0 == strcmp(p_val, APP_SETTINGS_VAL_LED_BRIGHTNESS_DAY))
    {
        g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_DAY;
        TLOG_INF("LED brightness from settings: DAY");
    }
    else if (0 == strcmp(p_val, APP_SETTINGS_VAL_LED_BRIGHTNESS_NIGHT))
    {
        g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_NIGHT;
        TLOG_INF("LED brightness from settings: NIGHT");
    }
    else if (0 == strcmp(p_val, APP_SETTINGS_VAL_LED_BRIGHTNESS_OFF))
    {
        g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_OFF;
        TLOG_INF("LED brightness from settings: OFF");
    }
    else if (0 == strcmp(p_val, APP_SETTINGS_VAL_LED_BRIGHTNESS_DISABLED))
    {
        g_led_mode = APP_SETTINGS_LED_MODE_DISABLED;
        TLOG_INF("LED brightness from settings: disabled");
    }
    else if (0 == strcmp(p_val, APP_SETTINGS_VAL_LED_BRIGHTNESS_AUTO))
    {
        g_led_mode = APP_SETTINGS_LED_MODE_AUTO;
        TLOG_INF("LED brightness from settings: auto");
    }
    else
    {
        TLOG_INF("LED brightness from settings: manual percentage: %s", p_val);
        if (!app_settings_set_led_mode_manual_percentage(p_val))
        {
            TLOG_WRN("Invalid LED brightness value in settings: '%s'", p_val);
        }
    }
}

static void
app_settings_handler_set_led_color_table(
    const char* const               p_key,
    const manual_brightness_level_e brightness_level,
    const uint8_t* const            p_buf,
    ssize_t                         buf_len)
{
    app_settings_log_key(p_key, p_buf, buf_len, true);
    const settings_raw_color_table_t* const p_raw_table = (const settings_raw_color_table_t*)p_buf;
    if (buf_len != sizeof(p_raw_table->data))
    {
        TLOG_WRN(
            "Invalid length for key \"%s\": %d (expected %d)",
            p_key,
            (int)buf_len,
            (int)sizeof(p_raw_table->data));
        return;
    }
    const manual_brightness_color_t table = {
        .currents = {
            .current_red   = p_raw_table->data[0],
            .current_green = p_raw_table->data[1],
            .current_blue  = p_raw_table->data[2],
        },
        .colors = {
            [AIR_QUALITY_INDEX_EXCELLENT] = {
                .red   = p_raw_table->data[3],
                .green = p_raw_table->data[4],
                .blue  = p_raw_table->data[5],
            },
            [AIR_QUALITY_INDEX_GOOD] = {
                .red   = p_raw_table->data[6],
                .green = p_raw_table->data[7],
                .blue  = p_raw_table->data[8],
            },
            [AIR_QUALITY_INDEX_FAIR] = {
                .red   = p_raw_table->data[9],
                .green = p_raw_table->data[10],
                .blue  = p_raw_table->data[11],
            },
            [AIR_QUALITY_INDEX_POOR] = {
                .red   = p_raw_table->data[12],
                .green = p_raw_table->data[13],
                .blue  = p_raw_table->data[14],
            },
            [AIR_QUALITY_INDEX_VERY_POOR] = {
                .red   = p_raw_table->data[15],
                .green = p_raw_table->data[16],
                .blue  = p_raw_table->data[17],
            },
        },
    };
    aqi_set_colors_table(brightness_level, &table);
    TLOG_INF(
        "LED color table for brightness level %d loaded from settings: <%u, %u, %u> [<%u, %u, %u> <%u, %u, %u> <%u, "
        "%u, %u> <%u, %u, %u> <%u, %u, %u>]",
        brightness_level,
        table.currents.current_red,
        table.currents.current_green,
        table.currents.current_blue,
        table.colors[AIR_QUALITY_INDEX_EXCELLENT].red,
        table.colors[AIR_QUALITY_INDEX_EXCELLENT].green,
        table.colors[AIR_QUALITY_INDEX_EXCELLENT].blue,
        table.colors[AIR_QUALITY_INDEX_GOOD].red,
        table.colors[AIR_QUALITY_INDEX_GOOD].green,
        table.colors[AIR_QUALITY_INDEX_GOOD].blue,
        table.colors[AIR_QUALITY_INDEX_FAIR].red,
        table.colors[AIR_QUALITY_INDEX_FAIR].green,
        table.colors[AIR_QUALITY_INDEX_FAIR].blue,
        table.colors[AIR_QUALITY_INDEX_POOR].red,
        table.colors[AIR_QUALITY_INDEX_POOR].green,
        table.colors[AIR_QUALITY_INDEX_POOR].blue,
        table.colors[AIR_QUALITY_INDEX_VERY_POOR].red,
        table.colors[AIR_QUALITY_INDEX_VERY_POOR].green,
        table.colors[AIR_QUALITY_INDEX_VERY_POOR].blue);
}

static int // NOSONAR: Zephyr API
app_settings_handler_set(const char* key, size_t len, settings_read_cb read_cb, void* cb_arg)
{
    char buf[APP_SETTINGS_MAX_VAL_LEN];

    if (len >= sizeof(buf))
    {
        TLOG_WRN("Value for \"%s\" too long (%u bytes)", key, (unsigned)len);
        return 0;
    }

    ssize_t rlen = read_cb(cb_arg, buf, len);
    if (rlen < 0)
    {
        TLOG_WRN("read_cb failed for \"%s\": %d", key, (int)rlen);
        return 0;
    }
    if (rlen >= sizeof(buf))
    {
        TLOG_WRN("Value for \"%s\" too long (%u bytes)", key, (unsigned)len);
        return 0;
    }
    buf[rlen] = '\0';

    if (0 == strcmp(key, APP_SETTINGS_KEY_SEN66_VOC_ALGORITHM_STATE))
    {
        app_settings_handler_set_sen66_voc_algorithm_state(key, buf, rlen);
    }
    else if (0 == strcmp(key, APP_SETTINGS_KEY_LED_BRIGHTNESS))
    {
        app_settings_handler_set_led_brightness(key, buf);
    }
    else if (0 == strcmp(key, APP_SETTINGS_KEY_LED_COLOR_TABLE_NIGHT))
    {
        app_settings_handler_set_led_color_table(key, MANUAL_BRIGHTNESS_LEVEL_NIGHT, buf, rlen);
    }
    else if (0 == strcmp(key, APP_SETTINGS_KEY_LED_COLOR_TABLE_DAY))
    {
        app_settings_handler_set_led_color_table(key, MANUAL_BRIGHTNESS_LEVEL_DAY, buf, rlen);
    }
    else if (0 == strcmp(key, APP_SETTINGS_KEY_LED_COLOR_TABLE_BRIGHT_DAY))
    {
        app_settings_handler_set_led_color_table(key, MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY, buf, rlen);
    }
    else
    {
        TLOG_WRN("Unknown key \"%s\" (len=%u)", key, (unsigned)len);
        app_settings_log_key(key, buf, rlen, false);
    }
    return 0;
}

/* Optional commit callback, called after settings_load() completes */
static int // NOSONAR: Zephyr API
app_settings_handler_commit(void)
{
    LOG_DBG("app/ settings committed");
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(
    app,                         /* root name: "app" */
    "app",                       /* subtree */
    NULL,                        /* get handler */
    app_settings_handler_set,    /* set handler */
    app_settings_handler_commit, /* commit handler */
    NULL                         /* export handler */
);

void
app_settings_save_sen66_voc_algorithm_state(uint32_t unix_timestamp, const sen66_voc_algorithm_state_t* const p_state)
{
    k_mutex_lock(&g_sen66_voc_algorithm_state_mutex, K_FOREVER);
    g_sen66_voc_algorithm_state.unix_timestamp = unix_timestamp;
    g_sen66_voc_algorithm_state.state          = *p_state;
    k_mutex_unlock(&g_sen66_voc_algorithm_state_mutex);

    const app_settings_sen66_voc_algorithm_state_t voc_alg_state_with_time = g_sen66_voc_algorithm_state;
    app_settings_save_bin_key(
        APP_SETTINGS_KEY_PREFIX_APP "/" APP_SETTINGS_KEY_SEN66_VOC_ALGORITHM_STATE,
        (const char*)&voc_alg_state_with_time,
        sizeof(voc_alg_state_with_time));
}

app_settings_sen66_voc_algorithm_state_t
app_settings_get_sen66_voc_algorithm_state(void)
{
    k_mutex_lock(&g_sen66_voc_algorithm_state_mutex, K_FOREVER);
    const app_settings_sen66_voc_algorithm_state_t voc_alg_state_with_time = g_sen66_voc_algorithm_state;
    k_mutex_unlock(&g_sen66_voc_algorithm_state_mutex);
    return voc_alg_state_with_time;
}

uint32_t
app_settings_get_sen66_voc_algorithm_state_timestamp(void)
{
    k_mutex_lock(&g_sen66_voc_algorithm_state_mutex, K_FOREVER);
    const uint32_t unix_timestamp = g_sen66_voc_algorithm_state.unix_timestamp;
    k_mutex_unlock(&g_sen66_voc_algorithm_state_mutex);
    return unix_timestamp;
}

bool
app_settings_expose_serial_number(const bool flag_expose)
{
    if (flag_expose == g_flag_config_mode)
    {
        return false;
    }
    g_flag_config_mode                  = flag_expose;
    const device_id_str_t device_id_str = get_device_id_str();
    if (flag_expose)
    {
        TLOG_INF("Expose device serial number: %s", device_id_str.serial_number);
    }
    else
    {
        TLOG_INF("Hide device serial number: %s", device_id_str.serial_number);
    }
#if defined(CONFIG_BT_DIS_SERIAL_NUMBER)
    app_settings_save_key(
        APP_SETTINGS_FULL_KEY_BT_DIS_SERIAL,
        device_id_str.serial_number,
        strlen(device_id_str.serial_number) + 1);
#endif
    return true;
}

void
app_settings_reload(void)
{
    // Reload settings to update in-memory values for BLE subsystem.
    zephyr_api_ret_t err = settings_load();
    if (0 != err)
    {
        TLOG_ERR("Settings loading failed: %d", err);
    }
    else
    {
        TLOG_INF("Settings reloaded successfully");
    }
}
