/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "nus.h"
#include <stddef.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/nus.h>
#include "tlog.h"
#include "ruuvi_endpoints.h"
#include "hist_log.h"
#include "nus_req.h"
#include "sys_utils.h"
#include "zephyr_api.h"

LOG_MODULE_REGISTER(nus, LOG_LEVEL_INF);

#define RUUVI_AIR_NUS_MAX_PACKET_LENGTH (244U)

typedef struct nus_hist_log_user_data_t
{
    struct bt_conn* const   p_conn;
    const uint32_t          local_time_offset_s;
    const re_type_t         req_re_type;
    const nus_req_src_idx_t src_idx;
    uint32_t                records_cnt;
    uint32_t                packets_cnt;
    bool                    is_multi_packet;
    uint8_t                 msg_offset;
    uint8_t                 msg[RUUVI_AIR_NUS_MAX_PACKET_LENGTH];
} nus_hist_log_user_data_t;

typedef struct nus_fifo_cmd_t
{
    void*           fifo_reserved;
    struct bt_conn* p_conn;
    nus_req_t       req;
} nus_fifo_cmd_t;

static K_FIFO_DEFINE(g_nus_fifo_cmd);
static int32_t g_nus_cnt_notif_enabled;
static bool    g_nus_reading_hist_in_progress;

bool
nus_is_reading_hist_in_progress(void)
{
    return g_nus_reading_hist_in_progress;
}

static void
nus_cb_on_notif_enabled(bool enabled, void* ctx)
{
    ARG_UNUSED(ctx);

    if (enabled)
    {
        g_nus_cnt_notif_enabled += 1;
    }
    else
    {
        g_nus_cnt_notif_enabled -= 1;
    }
    TLOG_WRN("%s: %s, cnt=%d", __func__, (enabled ? "Enabled" : "Disabled"), (int)g_nus_cnt_notif_enabled);
}

bool
nus_is_notif_enabled(void)
{
    if (0 != g_nus_cnt_notif_enabled)
    {
        return true;
    }
    return false;
}

static void
nus_hist_log_pack_uint32(uint8_t* const p_buf, const uint32_t val)
{
    p_buf[BYTE_IDX_0] = (uint8_t)((val >> BYTE_SHIFT_3) & BYTE_MASK);
    p_buf[BYTE_IDX_1] = (uint8_t)((val >> BYTE_SHIFT_2) & BYTE_MASK);
    p_buf[BYTE_IDX_2] = (uint8_t)((val >> BYTE_SHIFT_1) & BYTE_MASK);
    p_buf[BYTE_IDX_3] = (uint8_t)((val >> BYTE_SHIFT_0) & BYTE_MASK);
}

static void
nus_hist_log_pack_buffer(uint8_t* const p_buf, const uint8_t* const p_data, const size_t len)
{
    if ((NULL == p_buf) || (NULL == p_data) || (0 == len))
    {
        return;
    }
    if (len > RUUVI_AIR_NUS_MAX_PACKET_LENGTH)
    {
        return;
    }
    memcpy(p_buf, p_data, len);
}

static void
nus_hist_log_pack_record(
    uint8_t* const                      p_buf,
    const uint32_t                      timestamp_s,
    const hist_log_record_data_t* const p_hist_record)
{
    nus_hist_log_pack_uint32(&p_buf[RE_LOG_WRITE_AIRQ_TIMESTAMP_MSB_OFS], timestamp_s);
    nus_hist_log_pack_buffer(&p_buf[RE_LOG_WRITE_AIRQ_PAYLOAD_OFS], p_hist_record->buf, sizeof(p_hist_record->buf));
}

static bool
nus_send_with_retries(nus_hist_log_user_data_t* const p_data)
{
    bool res = false;
    LOG_HEXDUMP_DBG(p_data->msg, p_data->msg_offset, "bt_nus_send");
    while (1)
    {
        int64_t                time_start = k_uptime_get();
        const zephyr_api_ret_t err        = bt_nus_send(p_data->p_conn, p_data->msg, p_data->msg_offset);
        int64_t                delta_ms   = k_uptime_get() - time_start;
        if (0 != err)
        {
            TLOG_INF("bt_nus_send: err %d, delta %u ms", err, (uint32_t)delta_ms);
            if (-EAGAIN == err)
            {
                TLOG_WRN("Failed to send packet to NUS, err %d (EAGAIN)", err);
                k_msleep(10); // NOSONAR: avoid busy loop
                continue;
            }
            if (-ENOMEM == err)
            {
                TLOG_ERR("Failed to send packet to NUS, err %d (ENOMEM)", err);
                k_msleep(10); // NOSONAR: avoid busy loop
                continue;
            }
            TLOG_ERR("Failed to send packet to NUS, err %d", err);
            res = false;
        }
        else
        {
            res = true;
        }
        break;
    }
    p_data->msg_offset = 0;
    return res;
}

static bool
nus_hist_log_record_handler(
    const uint32_t                      timestamp_local,
    const hist_log_record_data_t* const p_hist_record,
    void*                               p_user_data)
{
    nus_hist_log_user_data_t* const p_data      = (nus_hist_log_user_data_t*)p_user_data;
    const uint32_t                  timestamp_s = timestamp_local + p_data->local_time_offset_s;

    const uint32_t record_led = RE_LOG_WRITE_AIRQ_RECORD_LEN;

    if (0 == p_data->msg_offset)
    {
        memset(&p_data->msg[0], UINT8_MAX, sizeof(p_data->msg));

        p_data->msg[RE_STANDARD_DESTINATION_INDEX]      = p_data->src_idx;
        p_data->msg[RE_STANDARD_SOURCE_INDEX]           = RE_STANDARD_DESTINATION_AIRQ;
        p_data->msg[RE_STANDARD_OPERATION_INDEX]        = p_data->is_multi_packet ? RE_STANDARD_LOG_MULTI_WRITE
                                                                                  : RE_STANDARD_LOG_VALUE_WRITE;
        p_data->msg_offset                              = RE_STANDARD_PAYLOAD_START_INDEX;
        p_data->msg[RE_LOG_WRITE_MULTI_NUM_RECORDS_IDX] = 1;
        p_data->msg_offset += 1;
        p_data->msg[RE_LOG_WRITE_MULTI_RECORD_LEN_IDX] = record_led;
        p_data->msg_offset += 1;
    }
    else
    {
        p_data->msg[RE_LOG_WRITE_MULTI_NUM_RECORDS_IDX] += 1;
    }
    nus_hist_log_pack_record(&p_data->msg[p_data->msg_offset], timestamp_s, p_hist_record);
    p_data->msg_offset += record_led;
    p_data->records_cnt += 1;

    const uint32_t max_num_records_in_packet = (RUUVI_AIR_NUS_MAX_PACKET_LENGTH - RE_LOG_WRITE_MULTI_PAYLOAD_IDX)
                                               / record_led;

    if ((!p_data->is_multi_packet) || (max_num_records_in_packet == p_data->msg[RE_LOG_WRITE_MULTI_NUM_RECORDS_IDX]))
    {
        LOG_HEXDUMP_DBG(p_data->msg, p_data->msg_offset, "bt_nus_send");
        p_data->packets_cnt += 1;
        if (!nus_send_with_retries(p_data))
        {
            return false;
        }
    }

    return true;
}

static bool
app_sensor_send_eof(__unused struct bt_conn* const p_conn, nus_hist_log_user_data_t* const p_data)
{
    if (0 != p_data->msg_offset)
    {
        const bool res = nus_send_with_retries(p_data); // Send remaining data
        if (!res)
        {
            return false;
        }
    }

    const uint32_t record_led = RE_LOG_WRITE_AIRQ_RECORD_LEN;

    memset(&p_data->msg[0], UINT8_MAX, sizeof(p_data->msg));

    p_data->msg[RE_STANDARD_DESTINATION_INDEX]      = p_data->src_idx;
    p_data->msg[RE_STANDARD_SOURCE_INDEX]           = RE_STANDARD_DESTINATION_AIRQ;
    p_data->msg[RE_STANDARD_OPERATION_INDEX]        = p_data->is_multi_packet ? RE_STANDARD_LOG_MULTI_WRITE
                                                                              : RE_STANDARD_LOG_VALUE_WRITE;
    p_data->msg[RE_LOG_WRITE_MULTI_NUM_RECORDS_IDX] = 0;
    p_data->msg[RE_LOG_WRITE_MULTI_RECORD_LEN_IDX]  = record_led;

    p_data->msg_offset = RE_LOG_WRITE_MULTI_PAYLOAD_IDX;

    if (!nus_send_with_retries(p_data))
    {
        return false;
    }

    return true;
}

static bool
app_sensor_log_read(struct bt_conn* const p_conn, const nus_req_t* const p_req)
{
    const uint32_t local_system_time_s = (uint32_t)time(NULL);

    TLOG_WRN(
        "Sending logged data. Current time: %" PRIu32 ", Start time: %" PRIu32 ", System time: %" PRIu32
        ", Shifted local time: %" PRIu32,
        p_req->current_time_s,
        p_req->start_time_s,
        local_system_time_s,
        local_system_time_s);

    g_nus_reading_hist_in_progress = true;

    const int64_t time_start = k_uptime_get();

    const uint32_t local_time_offset_s = p_req->current_time_s - local_system_time_s;
    const uint32_t local_start_time_s  = (p_req->start_time_s > local_time_offset_s)
                                             ? (p_req->start_time_s - local_time_offset_s)
                                             : 0;

    nus_hist_log_user_data_t user_data = {
        .p_conn              = p_conn,
        .local_time_offset_s = local_time_offset_s,
        .req_re_type         = p_req->req_re_type,
        .src_idx             = p_req->src_idx,
        .records_cnt         = 0,
        .packets_cnt         = 0,
        .is_multi_packet     = (RE_LOG_R_MULTI == p_req->req_re_op) ? true : false,
        .msg_offset          = 0,
    };

    bool res = true;
    if (!hist_log_read_records(&nus_hist_log_record_handler, &user_data, local_start_time_s))
    {
        TLOG_ERR("Failed to read records");
        res = false;
    }
    if (!app_sensor_send_eof(p_conn, &user_data))
    {
        TLOG_ERR("Failed to send EOF");
        res = false;
    }

    const int64_t delta_ms = k_uptime_get() - time_start;
    TLOG_WRN(
        "History log was sent: %" PRIu32 " records, %" PRIu32 " packets, time: %u.%03u seconds",
        user_data.records_cnt,
        user_data.packets_cnt,
        (uint32_t)(delta_ms / 1000),
        (uint32_t)(delta_ms % 1000));

    g_nus_reading_hist_in_progress = false;

    return res;
}

static bool
nus_handle_req_env_air(struct bt_conn* const p_conn, const nus_req_t* const p_req)
{
    if (RE_ENV_AIRQ != p_req->req_re_type)
    {
        TLOG_ERR("Unsupported request type: %d", p_req->req_re_type);
        return false;
    }
    if ((RE_LOG_R != p_req->req_re_op) && (RE_LOG_R_MULTI != p_req->req_re_op))
    {
        TLOG_ERR("Unsupported operation: %d", p_req->req_re_op);
        return false;
    }
    nus_fifo_cmd_t* p_cmd = k_malloc(sizeof(nus_fifo_cmd_t));
    if (NULL == p_cmd)
    {
        TLOG_ERR("Failed to allocate memory for command");
        return false;
    }
    memset(p_cmd, 0, sizeof(*p_cmd));
    p_cmd->p_conn = p_conn;
    p_cmd->req    = *p_req;
    k_fifo_put(&g_nus_fifo_cmd, p_cmd);

    return true;
}

static void
nus_handle_req(struct bt_conn* const p_conn, const uint8_t* p_raw_message, const uint16_t len)
{
    nus_req_t req = { 0 };
    if (!nus_req_parse(p_raw_message, len, &req))
    {
        TLOG_ERR("Failed to parse request");
        return;
    }
    switch (req.req_re_type)
    {
        case RE_ACC_XYZ:
        case RE_ACC_X:
        case RE_ACC_Y:
        case RE_ACC_Z:
        case RE_GYR_XYZ:
        case RE_GYR_X:
        case RE_GYR_Y:
        case RE_GYR_Z:
        case RE_ENV_ALL:
        case RE_ENV_TEMP:
        case RE_ENV_HUMI:
        case RE_ENV_PRES:
            TLOG_WRN("Sensor data request not supported: %d", req.req_re_type);
            break;

        case RE_ENV_AIRQ:
            if (!nus_handle_req_env_air(p_conn, &req))
            {
                TLOG_ERR("Failed to handle RE_ENV_AIRQ request");
            }
            break;

        case RE_SEC_PASS:
            TLOG_WRN("Password request not supported");
            break;

        default:
            TLOG_WRN("Unknown request type: 0x%02x", req.req_re_type);
            break;
    }
}

static void
nus_cb_on_received(struct bt_conn* conn, const void* data, uint16_t len, void* ctx)
{
    ARG_UNUSED(ctx);

    TLOG_INF("%s: Len: %d", __func__, len);
    LOG_HEXDUMP_INF(data, len, "Received data:");

    nus_handle_req(conn, data, len);
}

struct bt_nus_cb g_nus_listener = {
    .notif_enabled = &nus_cb_on_notif_enabled,
    .received      = &nus_cb_on_received,
};

bool
nus_init(void)
{
    g_nus_cnt_notif_enabled = 0;
    zephyr_api_ret_t err    = bt_nus_cb_register(&g_nus_listener, NULL);
    if (0 != err)
    {
        TLOG_ERR("Failed to register NUS callback: %d", err);
        return false;
    }
    TLOG_INF("NUS service successfully registered");
    return true;
}

static void
nus_thread(__unused void* p1, __unused void* p2, __unused void* p3)
{
    while (1)
    {
        struct nus_fifo_cmd_t* p_cmd = k_fifo_get(&g_nus_fifo_cmd, K_FOREVER);
        if (NULL == p_cmd)
        {
            TLOG_ERR("Failed to get command from FIFO");
            continue;
        }
        if (!app_sensor_log_read(p_cmd->p_conn, &p_cmd->req))
        {
            TLOG_ERR("Failed to read log");
        }
        k_free(p_cmd);
    }
}

K_THREAD_DEFINE(
    nus_tid,
    CONFIG_RUUVI_AIR_NUS_THREAD_STACK_SIZE,
    &nus_thread,
    NULL,
    NULL,
    NULL,
    CONFIG_RUUVI_AIR_NUS_THREAD_PRIORITY,
    0,
    0);
