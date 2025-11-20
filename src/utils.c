#include "utils.h"
#include <time.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/sys/timeutil.h>
#include "sys_utils.h"
#include "tlog.h"

LOG_MODULE_REGISTER(utils, LOG_LEVEL_INF);

uint64_t
get_device_id(void)
{
    uint64_t       id      = 0;
    const uint64_t dev_id0 = NRF_FICR->DEVICEID[0];
    const uint64_t dev_id1 = NRF_FICR->DEVICEID[1];
    id |= ((dev_id0 >> BYTE_SHIFT_3) & BYTE_MASK) << BYTE_SHIFT_7;
    id |= ((dev_id0 >> BYTE_SHIFT_2) & BYTE_MASK) << BYTE_SHIFT_6;
    id |= ((dev_id0 >> BYTE_SHIFT_1) & BYTE_MASK) << BYTE_SHIFT_5;
    id |= ((dev_id0 >> BYTE_SHIFT_0) & BYTE_MASK) << BYTE_SHIFT_4;
    id |= ((dev_id1 >> BYTE_SHIFT_3) & BYTE_MASK) << BYTE_SHIFT_3;
    id |= ((dev_id1 >> BYTE_SHIFT_2) & BYTE_MASK) << BYTE_SHIFT_2;
    id |= ((dev_id1 >> BYTE_SHIFT_1) & BYTE_MASK) << BYTE_SHIFT_1;
    id |= ((dev_id1 >> BYTE_SHIFT_0) & BYTE_MASK) << BYTE_SHIFT_0;
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
        for (uint32_t i = 0; i < BT_ADDR_SIZE; ++i)
        {
            mac |= (uint64_t)addr[0].a.val[i] << (i * BITS_PER_BYTE);
        }
    }
    TLOG_INF(
        "BLE MAC: %02x:%02x:%02x:%02x:%02x:%02x",
        (uint8_t)(mac >> BYTE_SHIFT_0) & BYTE_MASK,
        (uint8_t)((mac >> BYTE_SHIFT_1) & BYTE_MASK),
        (uint8_t)((mac >> BYTE_SHIFT_2) & BYTE_MASK),
        (uint8_t)((mac >> BYTE_SHIFT_3) & BYTE_MASK),
        (uint8_t)((mac >> BYTE_SHIFT_4) & BYTE_MASK),
        (uint8_t)((mac >> BYTE_SHIFT_5) & BYTE_MASK));
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
            tm_time.tm_year + TIME_UTILS_BASE_YEAR,
            tm_time.tm_mon + 1,
            tm_time.tm_mday,
            tm_time.tm_hour,
            tm_time.tm_min,
            tm_time.tm_sec,
            unixtime);
    }
}
