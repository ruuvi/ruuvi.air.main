/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_rtc.h"
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(RTC, LOG_LEVEL_INF);

#if CONFIG_RTC
#if DT_NODE_EXISTS(DT_ALIAS(rtc_0))
#define RTC_NODE DT_ALIAS(rtc_0)
#else
#error "'rtc-0' devicetree alias is not defined"
#endif

#define RTC_NODE DT_ALIAS(rtc_0)
#if DT_NODE_EXISTS(RTC_NODE) && DT_NODE_HAS_STATUS(RTC_NODE, okay)
static const struct device* const rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc_0));
#else
#error "'rtc-0' devicetree alias is not defined properly"
#endif
#endif // CONFIG_RTC

bool
app_rtc_get_time(struct tm* const p_tm_time)
{
#if CONFIG_RTC
    if (!device_is_ready(rtc_dev))
    {
#if RUUVI_MOCK_MEASUREMENTS
        LOG_DBG("RTC device not ready");
#else
        LOG_ERR("RTC device not ready");
#endif
        gmtime_r(&(time_t) { time(NULL) }, p_tm_time);
        return false;
    }

    struct rtc_time time_rtc = { 0 };
    int             ret      = rtc_get_time(rtc_dev, &time_rtc);
    if ((0 != ret) && (-ENODATA != ret))
    {
        LOG_ERR("Failed to get RTC time, error: %d", ret);
        gmtime_r(&(time_t) { time(NULL) }, p_tm_time);
        return false;
    }

    p_tm_time->tm_sec   = time_rtc.tm_sec;
    p_tm_time->tm_min   = time_rtc.tm_min;
    p_tm_time->tm_hour  = time_rtc.tm_hour;
    p_tm_time->tm_mday  = time_rtc.tm_mday;
    p_tm_time->tm_mon   = time_rtc.tm_mon;
    p_tm_time->tm_year  = time_rtc.tm_year;
    p_tm_time->tm_wday  = time_rtc.tm_wday;
    p_tm_time->tm_yday  = time_rtc.tm_yday;
    p_tm_time->tm_isdst = time_rtc.tm_isdst;

    if (0 != ret)
    {
        return false;
    }
    return true;
#else  // CONFIG_RTC
    gmtime_r(&(time_t) { time(NULL) }, p_tm_time);
    return false;
#endif // CONFIG_RTC
}

bool
app_rtc_set_time(const struct tm* const p_tm_time)
{
#if CONFIG_RTC
    if (!device_is_ready(rtc_dev))
    {
#if RUUVI_MOCK_MEASUREMENTS
        LOG_DBG("RTC device not ready");
#else
        LOG_ERR("RTC device not ready");
#endif
        const time_t    unix_time = timeutil_timegm(p_tm_time);
        struct timespec ts        = {
                   .tv_sec  = unix_time,
                   .tv_nsec = 0,
        };
        clock_settime(CLOCK_REALTIME, &ts);
#if RUUVI_MOCK_MEASUREMENTS
        LOG_DBG("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec, (int32_t)ts.tv_nsec);
#else
        LOG_INF("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec, (int32_t)ts.tv_nsec);
#endif
        return false;
    }
    struct rtc_time time_rtc = {
        .tm_sec   = p_tm_time->tm_sec,
        .tm_min   = p_tm_time->tm_min,
        .tm_hour  = p_tm_time->tm_hour,
        .tm_mday  = p_tm_time->tm_mday,
        .tm_mon   = p_tm_time->tm_mon,
        .tm_year  = p_tm_time->tm_year,
        .tm_wday  = p_tm_time->tm_wday,
        .tm_yday  = p_tm_time->tm_yday,
        .tm_isdst = p_tm_time->tm_isdst,
        .tm_nsec  = 0,
    };
    int ret = rtc_set_time(rtc_dev, &time_rtc);
    if (0 != ret)
    {
        LOG_ERR("Failed to set RTC time, error: %d", ret);
        return false;
    }
    return true;
#else // CONFIG_RTC
    const time_t    unix_time = timeutil_timegm(p_tm_time);
    struct timespec ts        = {
               .tv_sec  = unix_time,
               .tv_nsec = 0,
    };
    clock_settime(CLOCK_REALTIME, &ts);
#if RUUVI_MOCK_MEASUREMENTS
    LOG_DBG("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec, (int32_t)ts.tv_nsec);
#else
    LOG_INF("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec, (int32_t)ts.tv_nsec);
#endif
#endif // CONFIG_RTC
}
