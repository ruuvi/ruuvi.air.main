/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "hist_log.h"
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/sys/crc.h>
#include "tlog.h"
#include "utils.h"
#include "ruuvi_endpoint_e1.h"
#include "app_rtc.h"

LOG_MODULE_REGISTER(hist_log, LOG_LEVEL_INF);

#define USE_HIST_LOG (1 && IS_ENABLED(CONFIG_RUUVI_AIR_ENABLE_HIST_LOG))

#if defined(RUUVI_MOCK_MEASUREMENTS) && RUUVI_MOCK_MEASUREMENTS
#define HIST_LOG_TEST_FILL_ALL_STORAGE (1)
#else
#define HIST_LOG_TEST_FILL_ALL_STORAGE (0)
#endif

#if USE_HIST_LOG

#define HIST_LOG_PARTITION_LABEL hist_storage
#define HIST_LOG_PARTITION_SIZE  PM_HIST_STORAGE_SIZE

#define HIST_LOG_FLASH_AREA_ID     FLASH_AREA_ID(HIST_LOG_PARTITION_LABEL)
#define HIST_LOG_FLASH_SECTOR_SIZE (4U * 1024U)
#define HIST_LOG_NUM_SECTORS       (HIST_LOG_PARTITION_SIZE / HIST_LOG_FLASH_SECTOR_SIZE)

static struct flash_sector g_hist_log_sectors[HIST_LOG_NUM_SECTORS];
#if HIST_LOG_TEST_FILL_ALL_STORAGE
static bool g_hist_log_full;
#endif

static struct fcb g_hist_log_fcb = {
    .f_magic       = HIST_LOG_FCB_SIGNATURE,
    .f_version     = HIST_LOG_FCB_FMT_VERSION,
    .f_sector_cnt  = HIST_LOG_NUM_SECTORS,
    .f_scratch_cnt = 1,
    .f_sectors     = g_hist_log_sectors,
    // We're using own CRC16 as part of records for data integrity check.
    // Automatic CRC check uses CRC8, which is not enough for our needs.
    // Also, automatic CRC caclulation requires reading from the flash,
    // which is less reliable compared to calculating CRC during record append.
    .f_flags = FCB_FLAGS_CRC_DISABLED,
};

static bool
hist_log_erase_flash_storage(void)
{
    const struct flash_area* p_fa = NULL;
    int                      rc   = flash_area_open(HIST_LOG_FLASH_AREA_ID, &p_fa);
    if (0 != rc)
    {
        TLOG_ERR("flash_area_open failed, rc=%d", rc);
        return false;
    }
    for (int i = 0; i < ARRAY_SIZE(g_hist_log_sectors); i++)
    {
        const struct flash_sector* p_fs = &g_hist_log_sectors[i];
        TLOG_INF(
            "flash_area_erase: sector[%d]: fs_off=%x, fs_size=%x",
            i,
            (unsigned)p_fs->fs_off,
            (unsigned)p_fs->fs_size);
        rc = flash_area_erase(p_fa, p_fs->fs_off, p_fs->fs_size);
        if (0 != rc)
        {
            TLOG_ERR("flash_area_erase failed, rc=%d", rc);
            flash_area_close(p_fa);
            return false;
        }
    }
    flash_area_close(p_fa);
    return true;
}

static bool
hist_log_check_sectors_count(void)
{
    size_t sector_count = ARRAY_SIZE(g_hist_log_sectors);
    int    rc           = flash_area_get_sectors(HIST_LOG_FLASH_AREA_ID, &sector_count, g_hist_log_sectors);
    if ((0 != rc) || (0 == sector_count))
    {
        TLOG_ERR("flash_area_get_sectors failed, rc=%d", rc);
        return false;
    }
    if (sector_count != HIST_LOG_NUM_SECTORS)
    {
        TLOG_ERR(
            "flash_area_get_sectors: sector_count=%u, expected=%u",
            (unsigned)sector_count,
            (unsigned)HIST_LOG_NUM_SECTORS);
        return false;
    }
    TLOG_DBG("flash_area has %u sectors", (unsigned)sector_count);
    for (int i = 0; i < sector_count; i++)
    {
        TLOG_DBG(
            "sector[%d]: fs_off=%x, fs_size=%x",
            i,
            (unsigned)g_hist_log_sectors[i].fs_off,
            (unsigned)g_hist_log_sectors[i].fs_size);
    }
    return true;
}

static bool
hist_log_fcb_init(struct fcb* const p_fcb)
{
    int rc = fcb_init(HIST_LOG_FLASH_AREA_ID, p_fcb);
    if (rc != 0)
    {
        if (-ENOMSG == rc)
        {
            TLOG_ERR("fcb_init failed, -ENOMSG, need to erase storage area");
            if (!hist_log_erase_flash_storage())
            {
                TLOG_ERR("erase_flash_storage failed");
                return false;
            }
            rc = fcb_init(HIST_LOG_FLASH_AREA_ID, p_fcb);
            if (0 != rc)
            {
                TLOG_ERR("fcb_init failed, rc=%d", rc);
                return false;
            }
        }
        else
        {
            TLOG_ERR("fcb_init failed, rc=%d", rc);
            return false;
        }
    }
    TLOG_INF("fcb_is_empty: %d", fcb_is_empty(p_fcb));
    TLOG_INF("fcb_free_sector_cnt: %d", fcb_free_sector_cnt(p_fcb));
    return true;
}

static bool
hist_log_check_flash_driver(void)
{
    const struct flash_area* fa = NULL;
    int                      rc = flash_area_open(HIST_LOG_FLASH_AREA_ID, &fa);
    if (0 != rc)
    {
        LOG_ERR("flash_area_open failed, rc=%d", rc);
        return false;
    }
    if (!flash_area_has_driver(fa))
    {
        LOG_ERR("flash_area_has_driver failed");
        return false;
    }

    uint32_t block_size = flash_area_align(fa);
    LOG_INF("flash_area_align: %d", block_size);

    const struct device* p_device = flash_area_get_device(fa);
    LOG_INF("flash_area_get_device: %p", p_device);

    flash_area_close(fa);
    return true;
}
#endif

bool
hist_log_init(const bool is_rtc_valid)
{
#if USE_HIST_LOG
#if HIST_LOG_TEST_FILL_ALL_STORAGE
    g_hist_log_full = false;
#endif
    if (!hist_log_check_flash_driver())
    {
        return false;
    }

    if (!hist_log_check_sectors_count())
    {
        return false;
    }

    if (!is_rtc_valid)
    {
        TLOG_WRN("RTC is not valid, erase flash storage");
        if (!hist_log_erase_flash_storage())
        {
            TLOG_ERR("erase_flash_storage failed");
            return false;
        }
    }

    if (!hist_log_fcb_init(&g_hist_log_fcb))
    {
        return false;
    }

    TLOG_INF("FCB initialized successfully");
#endif

#if HIST_LOG_TEST_FILL_ALL_STORAGE

    uint32_t timestamp = time(NULL);
    uint32_t cnt       = 0;
    TLOG_WRN("Fill hist log storage with mock records...");
    while (!g_hist_log_full)
    {
        const re_e1_data_t e1_data = re_e1_data_invalid(cnt, 0);
        uint8_t            buffer[RE_E1_DATA_LENGTH];
        (void)re_e1_encode(buffer, &e1_data);
        hist_log_record_data_t record_data = { 0 };
        memcpy(&record_data.buf, buffer, sizeof(record_data.buf));

        if (!hist_log_append_record(timestamp, &record_data, false))
        {
            TLOG_ERR("Failed to append record");
            break;
        }
        timestamp += 5 * 60;
        struct tm tm_time = { 0 };
        gmtime_r(&(time_t) { timestamp }, &tm_time);
        app_rtc_set_time(&tm_time);
        cnt += 1;
    }
    TLOG_WRN("Hist log storage was filled with %u mock records", cnt);
#endif

    return true;
}

bool
hist_log_append_record(const uint32_t timestamp, const hist_log_record_data_t* const p_data, const bool flag_print_log)
{
#if USE_HIST_LOG
    hist_log_record_t record = {
        .timestamp = timestamp,
        .data      = *p_data,
        .crc16     = { 0, 0 },
    };
    const uint16_t crc16 = crc16_ccitt(0xFFFFU, (const uint8_t*)&record, offsetof(hist_log_record_t, crc16));
    record.crc16[0]      = crc16 & 0xFF;
    record.crc16[1]      = (crc16 >> 8) & 0xFF;

    struct fcb_entry  loc   = { 0 };
    struct fcb* const p_fcb = &g_hist_log_fcb;

    // Step 1: Allocate space for the new record in FCB
    int rc = fcb_append(p_fcb, sizeof(record), &loc);
    if (0 != rc)
    {
        if (-ENOSPC != rc)
        {
            TLOG_ERR("Failed to allocate space for FCB record: %d", rc);
            return false;
        }
        TLOG_WRN("FCB is full, rotate");
#if HIST_LOG_TEST_FILL_ALL_STORAGE
        g_hist_log_full = true;
#endif
        rc = fcb_rotate(p_fcb);
        if (0 != rc)
        {
            TLOG_ERR("fcb_rotate failed: %d", rc);
            return false;
        }
        rc = fcb_append(p_fcb, sizeof(record), &loc);
        if (0 != rc)
        {
            TLOG_ERR("fcb_append failed: %d", rc);
            return false;
        }
    }

    // Step 2: Write the data to flash
    off_t write_off = loc.fe_sector->fs_off + loc.fe_data_off;
    if (flag_print_log)
    {
        TLOG_INF(
            "Append record: time=%u, write_off=0x%08x, fs_off=0x%08x, fe_data_off=0x%04x",
            (unsigned)record.timestamp,
            (unsigned)write_off,
            (unsigned)loc.fe_sector->fs_off,
            (unsigned)loc.fe_data_off);
    }
    else
    {
        TLOG_DBG(
            "Append record: time=%u, write_off=0x%08x, fs_off=0x%08x, fe_data_off=0x%04x",
            (unsigned)record.timestamp,
            (unsigned)write_off,
            (unsigned)loc.fe_sector->fs_off,
            (unsigned)loc.fe_data_off);
    }
    LOG_HEXDUMP_DBG(&record, sizeof(record), "Write record");
    rc = flash_area_write(p_fcb->fap, write_off, &record, sizeof(record));
    if (0 != rc)
    {
        TLOG_ERR("flash_area_write failed: %d", rc);
        return false;
    }

    // Step 3: Finalize the entry
    rc = fcb_append_finish(p_fcb, &loc);
    if (0 != rc)
    {
        TLOG_ERR("fcb_append_finish failed: %d", rc);
        return false;
    }
#endif
    return true;
}

bool
hist_log_read_records(hist_log_record_handler_t p_cb, void* const p_user_data, const uint32_t timestamp_start)
{
#if USE_HIST_LOG
    TLOG_DBG("read_all_records");
    assert(NULL != p_cb);
    struct fcb_entry loc = {
        .fe_sector   = NULL,
        .fe_elem_off = 0,
        .fe_data_off = 0,
        .fe_data_len = 0,
    };
    struct fcb* const p_fcb = &g_hist_log_fcb;

    int rc = fcb_getnext(p_fcb, &loc);
    if (0 != rc)
    {
        TLOG_DBG("No records found");
        return true;
    }

    while (0 == rc)
    {
        const off_t read_off = loc.fe_sector->fs_off + loc.fe_data_off;
        TLOG_DBG(
            "flash_area_read: read_off=0x%08x, fs_off=0x%08x, fe_data_off=0x%04x",
            (unsigned)read_off,
            (unsigned)loc.fe_sector->fs_off,
            (unsigned)loc.fe_data_off);

        struct hist_log_record_t record = { 0 };

        rc = flash_area_read(p_fcb->fap, read_off, &record, sizeof(record));
        if (0 != rc)
        {
            TLOG_ERR("flash_area_read failed: %d", rc);
            return false;
        }
        LOG_HEXDUMP_DBG(&record, sizeof(record), "Read record");

        if (0 != (crc16_ccitt(0xFFFFU, (const uint8_t*)&record, sizeof(record))))
        {
            TLOG_ERR(
                "CRC16 check failed: read_off=%x, fs_off=%x, fe_data_off=%x",
                (unsigned)read_off,
                (unsigned)loc.fe_sector->fs_off,
                (unsigned)loc.fe_data_off);
        }
        else
        {
            if (record.timestamp >= timestamp_start)
            {
                TLOG_DBG("Read log record: time=%" PRIu32 " > start=%" PRIu32, record.timestamp, timestamp_start);
                if (!p_cb(record.timestamp, &record.data, p_user_data))
                {
                    return false;
                }
            }
            else
            {
                TLOG_DBG("Skip log record: time=%" PRIu32 " < start=%" PRIu32, record.timestamp, timestamp_start);
            }
        }

        rc = fcb_getnext(p_fcb, &loc);
    }
#endif
    return true;
}

void
hist_log_print_free_sectors(void)
{
#if USE_HIST_LOG
    TLOG_INF("fcb_free_sector_cnt: %d", fcb_free_sector_cnt(&g_hist_log_fcb));
#endif
#if 0
    for (int i = 0; i < ARRAY_SIZE(g_hist_log_sectors); i++) {
        const struct flash_sector *p_fs = &g_hist_log_sectors[i];
        TLOG_INF("sector[%d]: fs_off=%x, fs_size=%x", i, (unsigned)p_fs->fs_off, (unsigned)p_fs->fs_size);
    }
#endif
}
