/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_settings.h"
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include "utils.h"
#include "app_fw_ver.h"
#include "app_version.h"
#include "tlog.h"

LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_INF);

static bool g_flag_config_mode = false;

enum app_settings_led_mode g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_DAY;

static void
settings_runtime_load(void)
{
#if defined(CONFIG_BT_DIS_SETTINGS)
    settings_runtime_set("bt/dis/model", CONFIG_BT_DIS_MODEL, sizeof(CONFIG_BT_DIS_MODEL));
    settings_runtime_set("bt/dis/manuf", CONFIG_BT_DIS_MANUF, sizeof(CONFIG_BT_DIS_MANUF));
#if defined(CONFIG_BT_DIS_SERIAL_NUMBER)
    if (g_flag_config_mode)
    {
        const uint64_t device_id = get_device_id();
        char           device_id_str[8 * 3];
        snprintf(
            device_id_str,
            sizeof(device_id_str),
            "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
            (uint8_t)(device_id >> 56) & 0xFF,
            (uint8_t)(device_id >> 48) & 0xFF,
            (uint8_t)(device_id >> 40) & 0xFF,
            (uint8_t)(device_id >> 32) & 0xFF,
            (uint8_t)(device_id >> 24) & 0xFF,
            (uint8_t)(device_id >> 16) & 0xFF,
            (uint8_t)(device_id >> 8) & 0xFF,
            (uint8_t)(device_id >> 0) & 0xFF);
        settings_runtime_set("bt/dis/serial", device_id_str, strlen(device_id_str) + 1);
    }
    else
    {
        settings_runtime_set("bt/dis/serial", CONFIG_BT_DIS_SERIAL_NUMBER_STR, sizeof(CONFIG_BT_DIS_SERIAL_NUMBER_STR));
    }
#endif
#if defined(CONFIG_BT_DIS_SW_REV)
    settings_runtime_set("bt/dis/sw", CONFIG_BT_DIS_SW_REV_STR, sizeof(CONFIG_BT_DIS_SW_REV_STR));
#endif
#if defined(CONFIG_BT_DIS_FW_REV)
    char fw_ver_str[sizeof(CONFIG_BT_DEVICE_NAME) + 2 + sizeof(APP_VERSION_EXTENDED_STRING)];
    snprintf(fw_ver_str, sizeof(fw_ver_str), "%s v%s", CONFIG_BT_DEVICE_NAME, app_fw_ver_get());
    settings_runtime_set("bt/dis/fw", fw_ver_str, strlen(fw_ver_str) + 1);
#endif
#if defined(CONFIG_BT_DIS_HW_REV)
    settings_runtime_set("bt/dis/hw", CONFIG_BT_DIS_HW_REV_STR, sizeof(CONFIG_BT_DIS_HW_REV_STR));
#endif
#endif
}

bool
app_settings_init(void)
{
    settings_runtime_load();
    return true;
}

enum app_settings_led_mode
app_settings_get_led_mode(void)
{
    return g_led_mode;
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
app_settings_set_led_mode(const enum app_settings_led_mode mode)
{
    g_led_mode = mode;
}

void
app_settings_set_next_led_mode(void)
{
    switch (g_led_mode)
    {
        case APP_SETTINGS_LED_MODE_DISABLED:
            LOG_INF("Do not switch LED mode in DISABLED mode");
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY:
            LOG_INF("Switch LED mode BRIGHT_DAY -> DAY");
            g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_DAY;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_DAY:
            LOG_INF("Switch LED mode DAY -> NIGHT");
            g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_NIGHT;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_NIGHT:
            LOG_INF("Switch LED mode NIGHT -> OFF");
            g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_OFF;
            break;
        case APP_SETTINGS_LED_MODE_MANUAL_OFF:
            LOG_INF("Switch LED mode OFF -> BRIGHT_DAY");
            g_led_mode = APP_SETTINGS_LED_MODE_MANUAL_BRIGHT_DAY;
            break;
        case APP_SETTINGS_LED_MODE_AUTO:
            LOG_INF("Do not switch LED mode in AUTO mode");
            break;
        default:
            LOG_INF("Unknown LED mode=%d", g_led_mode);
            break;
    }
}
