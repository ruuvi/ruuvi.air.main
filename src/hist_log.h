/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef HIST_LOG_H
#define HIST_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include "ruuvi_endpoints.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HIST_LOG_FCB_SIGNATURE   (0x52555556) // "RUUV"
#define HIST_LOG_FCB_FMT_VERSION (1)

typedef struct hist_log_record_data_t
{
    uint8_t buf[RE_LOG_WRITE_AIRQ_RECORD_LEN - RE_LOG_WRITE_AIRQ_PAYLOAD_OFS];
} hist_log_record_data_t;

typedef struct hist_log_record_t
{
    uint32_t               timestamp; // Offset: 0
    hist_log_record_data_t data;      // Offset: 4
    uint8_t                crc16[2];  // Offset: 38
} hist_log_record_t;

_Static_assert(sizeof(hist_log_record_t) == 40, "hist_log_record_t record size is not 40 bytes");

typedef bool (*hist_log_record_handler_t)(
    const uint32_t                      timestamp,
    const hist_log_record_data_t* const p_data,
    void*                               p_user_data);

bool
hist_log_init(const bool is_rtc_valid);

bool
hist_log_append_record(const uint32_t timestamp, const hist_log_record_data_t* const p_data, const bool flag_print_log);

bool
hist_log_read_records(hist_log_record_handler_t p_cb, void* const p_user_data, const uint32_t timestamp_start);

void
hist_log_print_free_sectors(void);

#ifdef __cplusplus
}
#endif

#endif // HIST_LOG_H
