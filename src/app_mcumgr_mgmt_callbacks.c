/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_mcumgr_mgmt_callbacks.h"
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt.h>
#include <zephyr/logging/log.h>
#include "sensors.h"
#include "app_settings.h"
#include "ruuvi_fw_update.h"
#include "tlog.h"

LOG_MODULE_REGISTER(mcumgr_mgmt, LOG_LEVEL_INF);

#define RUUVI_FW_PATH_MAX_SIZE (64)

static const char* g_p_mnt_point;
static uint32_t    g_upload_cnt;

static const char*
conv_mcumgr_op_to_str(const enum mcumgr_op_t op)
{
    switch (op)
    {
        case MGMT_OP_READ:
            return "MGMT_OP_READ";
        case MGMT_OP_READ_RSP:
            return "MGMT_OP_READ_RSP";
        case MGMT_OP_WRITE:
            return "MGMT_OP_WRITE";
        case MGMT_OP_WRITE_RSP:
            return "MGMT_OP_WRITE_RSP";
        default:
            return "MGMT_OP:Unknown";
    }
}

static const char*
conv_fs_mgmt_id_to_str(const uint16_t id)
{
    switch (id)
    {
        case FS_MGMT_ID_FILE:
            return "FS_MGMT_ID_FILE";
        case FS_MGMT_ID_STAT:
            return "FS_MGMT_ID_STAT";
        case FS_MGMT_ID_HASH_CHECKSUM:
            return "FS_MGMT_ID_HASH_CHECKSUM";
        case FS_MGMT_ID_SUPPORTED_HASH_CHECKSUM:
            return "FS_MGMT_ID_SUPPORTED_HASH_CHECKSUM";
        case FS_MGMT_ID_OPENED_FILE:
            return "FS_MGMT_ID_OPENED_FILE";
        default:
            return "FS_MGMT_ID:Unknown";
    }
}

static enum mgmt_cb_return
mgmt_cb_cmd_reset(
    uint32_t                     event,
    __unused enum mgmt_cb_return prev_status,
    __unused int32_t*            rc,
    __unused uint16_t*           group,
    __unused bool*               abort_more,
    __unused void*               data,
    __unused size_t              data_size)
{
    if (MGMT_EVT_OP_OS_MGMT_RESET != event)
    {
        TLOG_ERR("%s: Unexpected event 0x%08x", __func__, event);
        return MGMT_CB_OK;
    }
    TLOG_WRN("MGMT_EVT_OP_OS_MGMT_RESET received, system will reboot!");
    uint32_t                    cur_unix_time32 = 0;
    sen66_voc_algorithm_state_t voc_alg_state   = { 0 };
    sensors_get_from_cache_sen66_voc_algorithm_state(&cur_unix_time32, &voc_alg_state);
    app_settings_save_sen66_voc_algorithm_state(cur_unix_time32, &voc_alg_state);
    return MGMT_CB_OK;
}

static enum mgmt_cb_return
mgmt_cb_cmd_recv(
    uint32_t                     event,
    __unused enum mgmt_cb_return prev_status,
    __unused int32_t*            rc,
    __unused uint16_t*           group,
    __unused bool*               abort_more,
    void*                        data,
    size_t                       data_size)
{
    if (MGMT_EVT_OP_CMD_RECV != event)
    {
        LOG_ERR("%s: Unexpected event 0x%08x", __func__, event);
        return MGMT_CB_OK;
    }
    if ((0 == data_size) || (NULL == data))
    {
        LOG_ERR("%s: Invalid data", __func__);
        return MGMT_CB_OK;
    }
    const struct mgmt_evt_op_cmd_arg* const p_cmd_recv = (struct mgmt_evt_op_cmd_arg*)data;
    if (MGMT_GROUP_ID_FS == p_cmd_recv->group)
    {
        g_upload_cnt += 1;
        LOG_INF(
            "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_FS: group 0x%04x, id=0x%04x (%s), opcode 0x%04x (%s)",
            p_cmd_recv->group,
            p_cmd_recv->id,
            conv_fs_mgmt_id_to_str(p_cmd_recv->id),
            p_cmd_recv->op,
            conv_mcumgr_op_to_str(p_cmd_recv->op));
    }
    return MGMT_CB_OK;
}

static bool
check_is_file_path_equal_to_name(const char* const p_file_path, const char* const p_file_name)
{
    char expected_file_path[RUUVI_FW_PATH_MAX_SIZE] = { 0 };
    snprintf(expected_file_path, sizeof(expected_file_path), "%s/%s", g_p_mnt_point, p_file_name);
    if (0 != strcmp(p_file_path, expected_file_path))
    {
        return false;
    }
    return true;
}

static enum mgmt_cb_return
mgmt_cb_file_access(
    uint32_t                     event,
    __unused enum mgmt_cb_return prev_status,
    int32_t*                     rc,
    uint16_t*                    group,
    __unused bool*               abort_more,
    void*                        data,
    size_t                       data_size)
{
    if (MGMT_EVT_OP_FS_MGMT_FILE_ACCESS != event)
    {
        LOG_ERR("%s: Unexpected event 0x%08x", __func__, event);
        return MGMT_CB_OK;
    }
    if ((0 == data_size) || (NULL == data))
    {
        LOG_ERR("%s: Invalid data", __func__);
        return MGMT_CB_OK;
    }
    const struct fs_mgmt_file_access* const p_file_access = (struct fs_mgmt_file_access*)data;
    LOG_INF(
        "MGMT_EVT_OP_FS_MGMT_FILE_ACCESS: filename=%s, access=0x%02x",
        p_file_access->filename,
        p_file_access->access);
    if ((!check_is_file_path_equal_to_name(p_file_access->filename, RUUVI_FW_MCUBOOT0_FILE_NAME))
        && (!check_is_file_path_equal_to_name(p_file_access->filename, RUUVI_FW_MCUBOOT1_FILE_NAME))
        && (!check_is_file_path_equal_to_name(p_file_access->filename, RUUVI_FW_LOADER_FILE_NAME))
        && (!check_is_file_path_equal_to_name(p_file_access->filename, RUUVI_FW_APP_FILE_NAME)))
    {
        LOG_ERR("Invalid filename %s", p_file_access->filename);
        *group = MGMT_GROUP_ID_FS;
        *rc    = MGMT_ERR_EINVAL;
        return MGMT_CB_ERROR_ERR;
    }
    LOG_INF("Allowed access to file %s", p_file_access->filename);

    return MGMT_CB_OK;
}

static struct mgmt_callback g_mgmt_cb_event_grp_os_cmd_reset = {
    .callback = &mgmt_cb_cmd_reset,
    .event_id = MGMT_EVT_OP_OS_MGMT_RESET,
};

static struct mgmt_callback g_mgmt_cb_event_grp_smp_cmd_recv = {
    .callback = &mgmt_cb_cmd_recv,
    .event_id = MGMT_EVT_OP_CMD_RECV,
};

static struct mgmt_callback g_mgmt_cb_event_grp_fs_file_access = {
    .callback = &mgmt_cb_file_access,
    .event_id = MGMT_EVT_OP_FS_MGMT_FILE_ACCESS,
};

void
app_mcumgr_mgmt_callbacks_init(const char* const p_mnt_point)
{
    g_p_mnt_point = p_mnt_point;
    mgmt_callback_register(&g_mgmt_cb_event_grp_os_cmd_reset);
    mgmt_callback_register(&g_mgmt_cb_event_grp_smp_cmd_recv);
    mgmt_callback_register(&g_mgmt_cb_event_grp_fs_file_access);
}

bool
app_mcumgr_mgmt_callbacks_is_uploading_in_progress(void)
{
    static uint32_t g_last_upload_cnt = 0;
    const bool      is_uploading      = (g_upload_cnt != g_last_upload_cnt);
    g_last_upload_cnt                 = g_upload_cnt;
    return is_uploading;
}
