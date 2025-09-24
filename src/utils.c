#include "utils.h"
#include <time.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include "tlog.h"

LOG_MODULE_REGISTER(utils, LOG_LEVEL_INF);

uint64_t
get_device_id(void)
{
    uint64_t id = 0;
    id |= (uint64_t)((NRF_FICR->DEVICEID[0] >> 24) & 0xFF) << 56;
    id |= (uint64_t)((NRF_FICR->DEVICEID[0] >> 16) & 0xFF) << 48;
    id |= (uint64_t)((NRF_FICR->DEVICEID[0] >> 8) & 0xFF) << 40;
    id |= (uint64_t)((NRF_FICR->DEVICEID[0] >> 0) & 0xFF) << 32;
    id |= (uint64_t)((NRF_FICR->DEVICEID[1] >> 24) & 0xFF) << 24;
    id |= (uint64_t)((NRF_FICR->DEVICEID[1] >> 16) & 0xFF) << 16;
    id |= (uint64_t)((NRF_FICR->DEVICEID[1] >> 8) & 0xFF) << 8;
    id |= (uint64_t)((NRF_FICR->DEVICEID[1] >> 0) & 0xFF) << 0;
    return id;
}

uint64_t
radio_address_get(void)
{
    uint64_t mac = 0;

    bt_addr_le_t addr[CONFIG_BT_ID_MAX];
    size_t       count = CONFIG_BT_ID_MAX;
    bt_id_get(addr, &count);

    if (count > 0)
    {
        for (int i = 0; i < 6; i++)
        {
            mac |= (uint64_t)addr[0].a.val[i] << (i * 8);
        }
    }
    TLOG_INF(
        "BLE MAC: %02x:%02x:%02x:%02x:%02x:%02x",
        (uint8_t)mac & 0xFF,
        (uint8_t)((mac >> 8) & 0xFF),
        (uint8_t)((mac >> 16) & 0xFF),
        (uint8_t)((mac >> 24) & 0xFF),
        (uint8_t)((mac >> 32) & 0xFF),
        (uint8_t)((mac >> 40) & 0xFF));
    return mac;
}

void
set_clock(const uint32_t unixtime, const bool flag_print_log)
{
    const struct timespec ts = { .tv_sec = unixtime, .tv_nsec = 0 };
    clock_settime(CLOCK_REALTIME, &ts);
    if (flag_print_log)
    {
        const time_t cur_time = time(NULL);
        struct tm    tm_time  = { 0 };
        gmtime_r(&cur_time, &tm_time);
        TLOG_INF(
            "Set clock: %04d-%02d-%02d %02d:%02d:%02d (%" PRIu32 ")",
            tm_time.tm_year + 1900,
            tm_time.tm_mon + 1,
            tm_time.tm_mday,
            tm_time.tm_hour,
            tm_time.tm_min,
            tm_time.tm_sec,
            unixtime);
    }
}
