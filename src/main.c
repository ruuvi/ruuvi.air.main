/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/timeutil.h>
#include "app_led.h"
#include "app_rtc.h"
#include "app_segger_rtt.h"
#include "sen66_wrap.h"
#include "sensors.h"
#include "ble_adv.h"
#include "nfc.h"
#include "hist_log.h"
#include "moving_avg.h"
#include "tlog.h"
#include "utils.h"
#include "mic_pdm.h"
#include "aqi.h"
#include "api.h"
#include "rgb_led.h"
#include "lp5810_test.h"
#include "opt_rgb_ctrl.h"
#include "app_settings.h"
#include "app_version.h"
#include "ncs_version.h"
#include "version.h"
// #if APP_VERSION_NUMBER != 0
// #include "app_commit.h"
// #endif
#include "ncs_commit.h"
#include "zephyr_commit.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define BOOT_MODE_TYPE_FACTORY_RESET (0xAC)

#define APP_MSG_QUEUE_SIZE               (2U)
#define APP_PERIOD_POLL_SENSORS_MS       (1000U)
#define APP_PERIOD_MEASURE_LUMINOSITY_MS (1000U / CONFIG_RUUVI_AIR_OPT4060_NUM_MEASUREMENTS_PER_SECOND)

typedef enum app_event_type
{
    APP_EVENT_TYPE_NONE               = 0,
    APP_EVENT_TYPE_POLL_SENSORS       = 1 << 0,
    APP_EVENT_TYPE_MEASURE_LUMINOSITY = 1 << 1,
    APP_EVENT_TYPE_REFRESH_LED        = 1 << 2,
} app_event_type_e;

static void
on_timer_poll_sensors(struct k_timer* timer_id);

static void
on_timer_measure_luminosity(struct k_timer* timer_id);

static K_EVENT_DEFINE(main_event);

static K_TIMER_DEFINE(app_timer_poll_sensors, &on_timer_poll_sensors, NULL);
static K_TIMER_DEFINE(app_timer_measure_luminosity, &on_timer_measure_luminosity, NULL);

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
    .type        = FS_LITTLEFS,
    .fs_data     = &storage,
    .storage_dev = (void*)FIXED_PARTITION_ID(littlefs_storage1),
    .mnt_point   = "/lfs1",
};

struct fs_mount_t* g_mountpoint = &lfs_storage_mnt;

static bool
mount_fs(void)
{
    int rc = fs_mkfs(FS_LITTLEFS, FIXED_PARTITION_ID(littlefs_storage1), NULL, 0);
    if (0 != rc)
    {
        LOG_ERR("FAIL: mkfs fa_id %d: res=%d", FIXED_PARTITION_ID(littlefs_storage1), rc);
        return false;
    }

    rc = fs_mount(g_mountpoint);
    if (0 != rc)
    {
        LOG_ERR(
            "FAIL: mount id %" PRIuPTR " at %s: %d",
            (uintptr_t)g_mountpoint->storage_dev,
            g_mountpoint->mnt_point,
            rc);
        return false;
    }
    LOG_INF("%s mounted successfully", g_mountpoint->mnt_point);

    struct fs_statvfs sbuf = { 0 };
    rc                     = fs_statvfs(g_mountpoint->mnt_point, &sbuf);
    if (rc < 0)
    {
        LOG_ERR("FAIL: statvfs: %d", rc);
        return false;
    }
    LOG_INF(
        "%s: bsize = %lu ; frsize = %lu ; blocks = %lu ; bfree = %lu",
        g_mountpoint->mnt_point,
        sbuf.f_bsize,
        sbuf.f_frsize,
        sbuf.f_blocks,
        sbuf.f_bfree);

    return true;
}

static void
on_timer_poll_sensors(struct k_timer* timer_id)
{
    TLOG_DBG("on_timer_poll_sensors");
    k_event_post(&main_event, APP_EVENT_TYPE_POLL_SENSORS);
}

static void
on_timer_measure_luminosity(struct k_timer* timer_id)
{
    TLOG_DBG("on_timer_measure_luminosity");
    k_event_post(&main_event, APP_EVENT_TYPE_MEASURE_LUMINOSITY);
}

void
app_post_event_refresh_led(void)
{
    TLOG_INF("Post event refresh_led");
    k_event_post(&main_event, APP_EVENT_TYPE_REFRESH_LED);
}

#if IS_ENABLED(CONFIG_BT_CTLR_ASSERT_HANDLER)
void
bt_ctlr_assert_handle(char* file, uint32_t line)
{
    LOG_ERR("%s: SoftDevice Controller ASSERT: %s, %d", __func__, file, line);
    __ASSERT(false, "SoftDevice Controller ASSERT: %s, %d\n", file, line);
    k_oops();
}
#endif

static void
log_time_info(const char* const p_prefix, const struct tm* const p_tm_time, const time_t clock_unix_time)
{
    const time_t rtc_unix_time = timeutil_timegm(p_tm_time);
    LOG_INF(
        "%s: %04d-%02d-%02d %02d:%02d:%02d, unix_time=%" PRIu32 ", clock=%" PRIu32,
        p_prefix,
        p_tm_time->tm_year + TIME_UTILS_BASE_YEAR,
        p_tm_time->tm_mon + 1,
        p_tm_time->tm_mday,
        p_tm_time->tm_hour,
        p_tm_time->tm_min,
        p_tm_time->tm_sec,
        (uint32_t)rtc_unix_time,
        (uint32_t)clock_unix_time);
}

static bool
check_rtc_clock(void)
{
    bool is_rtc_valid = false;

#if RUUVI_MOCK_MEASUREMENTS
    const time_t current_time = 0;
#else
    const time_t current_time = time(NULL);
#endif

    if ((current_time < RUUVI_AIR_MIN_UNIX_TIME) || (current_time > RUUVI_AIR_MAX_UNIX_TIME))
    {
        struct tm    base_tm_time   = { 0 };
        const time_t base_unix_time = RUUVI_AIR_MIN_UNIX_TIME;
        gmtime_r(&base_unix_time, &base_tm_time);
        LOG_WRN(
            "Current time is out of range, setting to base time: %04d-%02d-%02d %02d:%02d:%02d, unix time: %" PRIu32,
            base_tm_time.tm_year + TIME_UTILS_BASE_YEAR,
            base_tm_time.tm_mon + 1,
            base_tm_time.tm_mday,
            base_tm_time.tm_hour,
            base_tm_time.tm_min,
            base_tm_time.tm_sec,
            (uint32_t)base_unix_time);
        if (!app_rtc_set_time(&base_tm_time))
        {
#if CONFIG_RTC
#if RUUVI_MOCK_MEASUREMENTS
            LOG_DBG("Failed to set RTC time to base time");
#else
            LOG_ERR("Failed to set RTC time to base time");
#endif
#else // CONFIG_RTC
#if !RUUVI_MOCK_MEASUREMENTS
            LOG_WRN("There is no hardware RTC, setting system time to base time");
#endif
#endif // CONFIG_RTC
        }
        LOG_INF("System time: %" PRIu32, (uint32_t)time(NULL));
    }
    else
    {
        is_rtc_valid          = true;
        struct tm tm_cur_time = { 0 };
        gmtime_r(&current_time, &tm_cur_time);
        LOG_INF(
            "Current time is valid: %04d-%02d-%02d %02d:%02d:%02d, unix time: %" PRIu32,
            tm_cur_time.tm_year + TIME_UTILS_BASE_YEAR,
            tm_cur_time.tm_mon + 1,
            tm_cur_time.tm_mday,
            tm_cur_time.tm_hour,
            tm_cur_time.tm_min,
            tm_cur_time.tm_sec,
            (uint32_t)current_time);
    }

    return is_rtc_valid;
}

static void
poll_sensors(void)
{
    static uint16_t measurement_cnt = 0;

    app_led_green_set_if_button_is_not_pressed(true);

    const sensors_poll_result_t poll_res = sensors_poll();

    app_led_green_set_if_button_is_not_pressed(false);

#if 1
    switch (poll_res)
    {
        case SENSORS_POLL_RESULT_OK:
            break;
        case SENSORS_POLL_RESULT_ERR:
            break;
        case SENSORS_POLL_RESULT_NOT_READY:
            TLOG_WRN("SENSORS: poll result not ready");
            return;
    }
    measurement_cnt += 1;

    const sensors_measurement_t measurement = sensors_get_measurement();
    if (moving_avg_append(&measurement))
    {
        const hist_log_record_data_t record = moving_avg_get_accum();
        if (!hist_log_append_record((uint32_t)time(NULL), &record, true))
        {
            LOG_ERR("hist_log_append_record failed");
        }
        hist_log_print_free_sectors();
    }

    if (IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_AQI))
    {
        if (app_settings_is_led_mode_auto())
        {
            aqi_recalc_auto_brightness_level(measurement.luminosity);
        }
        aqi_update_led(api_calc_air_quality_index(&measurement));
    }
    else if (IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_CALIBRATE))
    {
        lp5810_test_calibrate();
    }
    else if (IS_ENABLED(CONFIG_RUUVI_AIR_LED_MODE_TEST_RGBW))
    {
        lp5810_test_rgbw();
    }
    else
    {
        // Do nothing, LED is off
    }

    ble_adv_restart(&measurement, measurement_cnt);

#if 0
    LOG_INF("system_heap:");
    extern struct sys_heap _system_heap;
    sys_heap_print_info(&_system_heap, false);
#if 0
    // This requires patching zephyr/lib/libc/common/source/stdlib/malloc.c - need to remove 'static' from z_malloc_heap
    LOG_INF("z_malloc_heap:");
    extern struct sys_heap z_malloc_heap;
    sys_heap_print_info(&z_malloc_heap, false);
#endif
#endif
    if (SENSORS_POLL_RESULT_ERR == poll_res)
    {
        sensors_reinit();
    }
#endif

#if IS_ENABLED(CONFIG_RUUVI_AIR_LOG_TIME)
    {
        struct tm tm_time_rtc = { 0 };
        if (!app_rtc_get_time(&tm_time_rtc))
        {
            const time_t cur_unix_time = time(NULL);
            struct tm    tm_cur_time   = { 0 };
            gmtime_r(&cur_unix_time, &tm_cur_time);
#if IS_ENABLED(CONFIG_RTC) && (!RUUVI_MOCK_MEASUREMENTS)
            LOG_ERR("Failed to get RTC time");
#endif
            log_time_info("System time", &tm_cur_time, cur_unix_time);
        }
        else
        {
            log_time_info("RTC time", &tm_time_rtc, time(NULL));
        }
    }
#endif
}

int
main(void)
{
#if defined(CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION)
    TLOG_INF(
        "### RuuviAir: Image version: %s (FwInfoCnt: %u)",
        CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION,
        CONFIG_FW_INFO_FIRMWARE_VERSION);
#endif
#if APP_VERSION_NUMBER != 0
    TLOG_INF(
        "### RuuviAir: Version: %s, build: %s, APP_VERSION_NUMBER: %s",
        APP_VERSION_EXTENDED_STRING,
        STRINGIFY(APP_BUILD_VERSION),
        STRINGIFY(APP_VERSION_NUMBER));
    // TLOG_INF(
    //     "### RuuviAir: Version: %s, build: %s, commit: %s",
    //     APP_VERSION_EXTENDED_STRING,
    //     STRINGIFY(APP_BUILD_VERSION),
    //     APP_COMMIT_STRING);
#else
    TLOG_INF("### RuuviAir: Version: %s, build: %s", APP_VERSION_EXTENDED_STRING, STRINGIFY(APP_BUILD_VERSION));
#endif
    TLOG_INF(
        "### RuuviAir: NCS version: %s, build: %s, commit: %s",
        NCS_VERSION_STRING,
        STRINGIFY(NCS_BUILD_VERSION),
        NCS_COMMIT_STRING);
    TLOG_INF(
        "### RuuviAir: Kernel version: %s, build: %s, commit: %s",
        KERNEL_VERSION_EXTENDED_STRING,
        STRINGIFY(BUILD_VERSION),
        ZEPHYR_COMMIT_STRING);

    if (bootmode_check(BOOT_MODE_TYPE_FACTORY_RESET))
    {
        LOG_WRN("Factory reset was performed.");
        bootmode_clear();
    }

    app_led_late_init_pwm();

    app_segger_rtt_check_data_location_and_size();

    rgb_led_init((rgb_led_brightness_t)CONFIG_RUUVI_AIR_LED_BRIGHTNESS);
    aqi_init();
    opt_rgb_ctrl_init(aqi_get_led_currents_alpha());

    const bool is_rtc_valid = check_rtc_clock();

    (void)mount_fs();

    if (!app_settings_init())
    {
        LOG_ERR("app_settings_init failed");
    }

    if (!hist_log_init(is_rtc_valid))
    {
        LOG_ERR("hist_log_init failed");
    }

    if (!sensors_init())
    {
        LOG_ERR("sensors_init failed");
        return -1;
    }
    LOG_INF("Sensors initialized");

#if 0
    // Uncomment this to attach debugger while sleeping
    LOG_INF("Seeep 10 seconds...");
    k_msleep(10000);
    LOG_INF("Wake up");
#endif

    if (!ble_adv_init())
    {
        LOG_ERR("ble_adv_init failed");
        return -1;
    }

    if (!nfc_init(ble_adv_get_mac()))
    {
        LOG_ERR("NFC init failed");
        return -1;
    }
    LOG_INF("NFC init ok");

    k_timer_start(&app_timer_poll_sensors, K_MSEC(0), K_MSEC(APP_PERIOD_POLL_SENSORS_MS));
    k_timer_start(&app_timer_measure_luminosity, K_MSEC(0), K_MSEC(APP_PERIOD_MEASURE_LUMINOSITY_MS));

    while (1)
    {
        const uint32_t events = k_event_wait(
            &main_event,
            APP_EVENT_TYPE_POLL_SENSORS | APP_EVENT_TYPE_MEASURE_LUMINOSITY | APP_EVENT_TYPE_REFRESH_LED,
            false,
            K_FOREVER);
        k_event_clear(&main_event, events);
        if (0 != (events & APP_EVENT_TYPE_POLL_SENSORS))
        {
            poll_sensors();
        }
        if (0 != (events & APP_EVENT_TYPE_MEASURE_LUMINOSITY))
        {
            opt_rgb_ctrl_measure_luminosity();
        }
        if (0 != (events & APP_EVENT_TYPE_REFRESH_LED))
        {
            aqi_refresh_led();
        }
    }
}

/**
 * Declare the symbol pointing to the former implementation of sys_reboot function
 */
extern void
__real_sys_reboot(int type);

/**
 * Redefine sys_reboot function to print a message before actually restarting
 */
void
__wrap_sys_reboot(int type) // NOSONAR
{
    TLOG_WRN("Turning off LED before reboot");
    opt_rgb_ctrl_turn_off_led_before_reboot();
    TLOG_WRN("Rebooting...");
    k_msleep(25); // Give some time to print log message

    /* Call the former implementation to actually restart the board */
    __real_sys_reboot(type);
}
