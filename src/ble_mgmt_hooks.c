#include "ble_mgmt_hooks.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/stat_mgmt/stat_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/settings_mgmt/settings_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/fs_mgmt/fs_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/shell_mgmt/shell_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/enum_mgmt/enum_mgmt.h>
#if IS_ENABLED(CONFIG_IMG_MANAGER)
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#endif
#include "tlog.h"
LOG_MODULE_REGISTER(ble_hooks, LOG_LEVEL_INF);

#define BLE_MGMT_HOOKS_ENABLED (0)

#if BLE_MGMT_HOOKS_ENABLED && IS_ENABLED(CONFIG_MCUMGR)
static const char*
conv_mgmt_event_to_str(const uint32_t event)
{
    switch (event)
    {
        case MGMT_EVT_OP_CMD_RECV:
            return "MGMT_EVT_OP_CMD_RECV";
        case MGMT_EVT_OP_CMD_STATUS:
            return "MGMT_EVT_OP_CMD_STATUS";
        case MGMT_EVT_OP_CMD_DONE:
            return "MGMT_EVT_OP_CMD_DONE";
        case MGMT_EVT_OP_CMD_ALL:
            return "MGMT_EVT_OP_CMD_ALL";

        case MGMT_EVT_OP_FS_MGMT_FILE_ACCESS:
            return "MGMT_EVT_OP_FS_MGMT_FILE_ACCESS";

        case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK:
            return "MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK";
        case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
            return "MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED";
        case MGMT_EVT_OP_IMG_MGMT_DFU_STARTED:
            return "MGMT_EVT_OP_IMG_MGMT_DFU_STARTED";
        case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
            return "MGMT_EVT_OP_IMG_MGMT_DFU_PENDING";
        case MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED:
            return "MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED";
        case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK_WRITE_COMPLETE:
            return "MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK_WRITE_COMPLETE";
        case MGMT_EVT_OP_IMG_MGMT_IMAGE_SLOT_STATE:
            return "MGMT_EVT_OP_IMG_MGMT_IMAGE_SLOT_STATE";

        case MGMT_EVT_OP_OS_MGMT_RESET:
            return "MGMT_EVT_OP_OS_MGMT_RESET";
        case MGMT_EVT_OP_OS_MGMT_INFO_CHECK:
            return "MGMT_EVT_OP_OS_MGMT_INFO_CHECK";
        case MGMT_EVT_OP_OS_MGMT_INFO_APPEND:
            return "MGMT_EVT_OP_OS_MGMT_INFO_APPEND";
        case MGMT_EVT_OP_OS_MGMT_DATETIME_GET:
            return "MGMT_EVT_OP_OS_MGMT_DATETIME_GET";
        case MGMT_EVT_OP_OS_MGMT_DATETIME_SET:
            return "MGMT_EVT_OP_OS_MGMT_DATETIME_SET";
        case MGMT_EVT_OP_OS_MGMT_BOOTLOADER_INFO:
            return "MGMT_EVT_OP_OS_MGMT_BOOTLOADER_INFO";

        case MGMT_EVT_OP_SETTINGS_MGMT_ACCESS:
            return "MGMT_EVT_OP_SETTINGS_MGMT_ACCESS";

        case MGMT_EVT_OP_ENUM_MGMT_DETAILS:
            return "MGMT_EVT_OP_ENUM_MGMT_DETAILS";

        default:
            return "MGMT_EVT_OP:Unknown";
    }
}

static const char*
conv_mcumgr_group_to_str(enum mcumgr_group_t group)
{
    switch (group)
    {
        case MGMT_GROUP_ID_OS:
            return "MGMT_GROUP_ID_OS";
        case MGMT_GROUP_ID_IMAGE:
            return "MGMT_GROUP_ID_IMAGE";
        case MGMT_GROUP_ID_STAT:
            return "MGMT_GROUP_ID_STAT";
        case MGMT_GROUP_ID_SETTINGS:
            return "MGMT_GROUP_ID_SETTINGS";
        case MGMT_GROUP_ID_LOG:
            return "MGMT_GROUP_ID_LOG";
        case MGMT_GROUP_ID_CRASH:
            return "MGMT_GROUP_ID_CRASH";
        case MGMT_GROUP_ID_SPLIT:
            return "MGMT_GROUP_ID_SPLIT";
        case MGMT_GROUP_ID_RUN:
            return "MGMT_GROUP_ID_RUN";
        case MGMT_GROUP_ID_FS:
            return "MGMT_GROUP_ID_FS";
        case MGMT_GROUP_ID_SHELL:
            return "MGMT_GROUP_ID_SHELL";
        case MGMT_GROUP_ID_ENUM:
            return "MGMT_GROUP_ID_ENUM";
        case MGMT_GROUP_ID_PERUSER:
            return "MGMT_GROUP_ID_PERUSER";
        case ZEPHYR_MGMT_GRP_BASIC:
            return "ZEPHYR_MGMT_GRP_BASIC";
        default:
            return "MGMT_GROUP_ID:Unknown";
    }
}

static const char*
conv_os_mgmt_id_to_str(uint16_t id)
{
    switch (id)
    {
        case OS_MGMT_ID_ECHO:
            return "OS_MGMT_ID_ECHO";
        case OS_MGMT_ID_CONS_ECHO_CTRL:
            return "OS_MGMT_ID_CONS_ECHO_CTRL";
        case OS_MGMT_ID_TASKSTAT:
            return "OS_MGMT_ID_TASKSTAT";
        case OS_MGMT_ID_MPSTAT:
            return "OS_MGMT_ID_MPSTAT";
        case OS_MGMT_ID_DATETIME_STR:
            return "OS_MGMT_ID_DATETIME_STR";
        case OS_MGMT_ID_RESET:
            return "OS_MGMT_ID_RESET";
        case OS_MGMT_ID_MCUMGR_PARAMS:
            return "OS_MGMT_ID_MCUMGR_PARAMS";
        case OS_MGMT_ID_INFO:
            return "OS_MGMT_ID_INFO";
        case OS_MGMT_ID_BOOTLOADER_INFO:
            return "OS_MGMT_ID_BOOTLOADER_INFO";
        default:
            return "OS_MGMT_ID:Unknown";
    }
}

#if IS_ENABLED(CONFIG_IMG_MANAGER)
static const char*
conv_img_mgmt_id_to_str(const uint16_t id)
{
    switch (id)
    {
        case IMG_MGMT_ID_STATE:
            return "IMG_MGMT_ID_STATE";
        case IMG_MGMT_ID_UPLOAD:
            return "IMG_MGMT_ID_UPLOAD";
        case IMG_MGMT_ID_FILE:
            return "IMG_MGMT_ID_FILE";
        case IMG_MGMT_ID_CORELIST:
            return "IMG_MGMT_ID_CORELIST";
        case IMG_MGMT_ID_CORELOAD:
            return "IMG_MGMT_ID_CORELOAD";
        case IMG_MGMT_ID_ERASE:
            return "IMG_MGMT_ID_ERASE";
        default:
            return "IMG_MGMT_ID:Unknown";
    }
}
#endif

static const char*
conv_stat_mgmt_id_to_str(const uint16_t id)
{
    switch (id)
    {
        case STAT_MGMT_ID_SHOW:
            return "STAT_MGMT_ID_SHOW";
        case STAT_MGMT_ID_LIST:
            return "STAT_MGMT_ID_LIST";
        default:
            return "STAT_MGMT_ID:Unknown";
    }
}

static const char*
conv_settings_mgmt_id_to_str(const uint16_t id)
{
    switch (id)
    {
        case SETTINGS_MGMT_ID_READ_WRITE:
            return "SETTINGS_MGMT_ID_READ_WRITE";
        case SETTINGS_MGMT_ID_DELETE:
            return "SETTINGS_MGMT_ID_DELETE";
        case SETTINGS_MGMT_ID_COMMIT:
            return "SETTINGS_MGMT_ID_COMMIT";
        case SETTINGS_MGMT_ID_LOAD_SAVE:
            return "SETTINGS_MGMT_ID_LOAD_SAVE";
        default:
            return "SETTINGS_MGMT_ID:Unknown";
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

static const char*
conv_shell_mgmt_id_to_str(const uint16_t id)
{
    switch (id)
    {
        case SHELL_MGMT_ID_EXEC:
            return "SHELL_MGMT_ID_EXEC";
        default:
            return "SHELL_MGMT_ID:Unknown";
    }
}

static const char*
conv_enum_mgmt_id_to_str(const uint16_t id)
{
    switch (id)
    {
        case ENUM_MGMT_ID_COUNT:
            return "ENUM_MGMT_ID_COUNT";
        case ENUM_MGMT_ID_LIST:
            return "ENUM_MGMT_ID_LIST";
        case ENUM_MGMT_ID_SINGLE:
            return "ENUM_MGMT_ID_SINGLE";
        case ENUM_MGMT_ID_DETAILS:
            return "ENUM_MGMT_ID_DETAILS";
        default:
            return "ENUM_MGMT_ID:Unknown";
    }
}

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

/**
 * @brief Function to be called on MGMT notification/event.
 *
 * This callback function is used to notify an application or system about a MCUmgr mgmt event.
 *
 * @param event		#mcumgr_op_t.
 * @param prev_status	#mgmt_cb_return of the previous handler calls, if it is an error then it
 *			will be the first error that was returned by a handler (i.e. this handler
 *			is being called for a notification only, the return code will be ignored).
 * @param rc		If ``prev_status`` is #MGMT_CB_ERROR_RC then this is the SMP error that
 *			was returned by the first handler that failed. If ``prev_status`` is
 *			#MGMT_CB_ERROR_ERR then this will be the group error rc code returned by
 *			the first handler that failed. If the handler wishes to raise an SMP
 *			error, this must be set to the #mcumgr_err_t status and #MGMT_CB_ERROR_RC
 *			must be returned by the function, if the handler wishes to raise a ret
 *			error, this must be set to the group ret status and #MGMT_CB_ERROR_ERR
 *			must be returned by the function.
 * @param group		If ``prev_status`` is #MGMT_CB_ERROR_ERR then this is the group of the
 *			ret error that was returned by the first handler that failed. If the
 *			handler wishes to raise a ret error, this must be set to the group ret
 *			status and #MGMT_CB_ERROR_ERR must be returned by the function.
 * @param abort_more	Set to true to abort further processing by additional handlers.
 * @param data		Optional event argument.
 * @param data_size	Size of optional event argument (0 if no data is provided).
 *
 * @return		#mgmt_cb_return indicating the status to return to the calling code (only
 *			checked when this is the first failure reported by a handler).
 */

static void
mgmt_cb_on_env_grp_smp(const uint32_t event, void* data, size_t data_size)
{
    TLOG_WRN("MGMT_EVT_GRP_SMP: event 0x%08x", event);
    switch (event)
    {
        case MGMT_EVT_OP_CMD_RECV:
        {
            struct mgmt_evt_op_cmd_arg* p_cmd_recv = (struct mgmt_evt_op_cmd_arg*)data;
            switch (p_cmd_recv->group)
            {
                case MGMT_GROUP_ID_OS:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_OS: group 0x%04x, id=0x%04x (%s), opcode 0x%04x (%s)",
                        p_cmd_recv->group,
                        p_cmd_recv->id,
                        conv_os_mgmt_id_to_str(p_cmd_recv->id),
                        p_cmd_recv->op,
                        conv_mcumgr_op_to_str(p_cmd_recv->op));
                    break;
#if IS_ENABLED(CONFIG_IMG_MANAGER)
                case MGMT_GROUP_ID_IMAGE:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_IMAGE: group 0x%04x, id=0x%04x (%s), opcode 0x%04x (%s)",
                        p_cmd_recv->group,
                        p_cmd_recv->id,
                        conv_img_mgmt_id_to_str(p_cmd_recv->id),
                        p_cmd_recv->op,
                        conv_mcumgr_op_to_str(p_cmd_recv->op));
                    break;
#endif
                case MGMT_GROUP_ID_STAT:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_STAT: group 0x%04x, id=0x%04x (%s), opcode 0x%04x (%s)",
                        p_cmd_recv->group,
                        p_cmd_recv->id,
                        conv_stat_mgmt_id_to_str(p_cmd_recv->id),
                        p_cmd_recv->op,
                        conv_mcumgr_op_to_str(p_cmd_recv->op));
                    break;
                case MGMT_GROUP_ID_SETTINGS:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_SETTINGS: group 0x%04x, id=0x%04x (%s), opcode 0x%04x "
                        "(%s)",
                        p_cmd_recv->group,
                        p_cmd_recv->id,
                        conv_settings_mgmt_id_to_str(p_cmd_recv->id),
                        p_cmd_recv->op,
                        conv_mcumgr_op_to_str(p_cmd_recv->op));
                    break;
                case MGMT_GROUP_ID_FS:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_FS: group 0x%04x, id=0x%04x (%s), opcode 0x%04x (%s)",
                        p_cmd_recv->group,
                        p_cmd_recv->id,
                        conv_fs_mgmt_id_to_str(p_cmd_recv->id),
                        p_cmd_recv->op,
                        conv_mcumgr_op_to_str(p_cmd_recv->op));
                    break;
                case MGMT_GROUP_ID_SHELL:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_SHELL: group 0x%04x, id=0x%04x (%s), opcode 0x%04x (%s)",
                        p_cmd_recv->group,
                        p_cmd_recv->id,
                        conv_shell_mgmt_id_to_str(p_cmd_recv->id),
                        p_cmd_recv->op,
                        conv_mcumgr_op_to_str(p_cmd_recv->op));
                    break;
                case MGMT_GROUP_ID_ENUM:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: MGMT_GROUP_ID_ENUM: group 0x%04x, id=0x%04x (%s), opcode 0x%04x (%s)",
                        p_cmd_recv->group,
                        p_cmd_recv->id,
                        conv_enum_mgmt_id_to_str(p_cmd_recv->id),
                        p_cmd_recv->op,
                        conv_mcumgr_op_to_str(p_cmd_recv->op));
                    break;
                default:
                    TLOG_WRN(
                        "MGMT_EVT_OP_CMD_RECV: group 0x%04x (%s), id=0x%04x, opcode 0x%04x",
                        p_cmd_recv->group,
                        conv_mcumgr_group_to_str(p_cmd_recv->group),
                        p_cmd_recv->id,
                        p_cmd_recv->op);
                    break;
            }
            break;
        }
        case MGMT_EVT_OP_CMD_STATUS:
        {
            struct mgmt_evt_op_cmd_arg* p_cmd_done_arg = (struct mgmt_evt_op_cmd_arg*)data;
            TLOG_WRN(
                "MGMT_EVT_OP_CMD_STATUS: group 0x%04x, id=0x%04x, err 0x%04x",
                p_cmd_done_arg->group,
                p_cmd_done_arg->id,
                p_cmd_done_arg->err);
            break;
        }
        case MGMT_EVT_OP_CMD_DONE:
        {
            struct mgmt_evt_op_cmd_arg* p_cmd_done_arg = (struct mgmt_evt_op_cmd_arg*)data;
            TLOG_WRN(
                "MGMT_EVT_OP_CMD_DONE: group 0x%04x, id=0x%04x, err 0x%04x",
                p_cmd_done_arg->group,
                p_cmd_done_arg->id,
                p_cmd_done_arg->err);
            break;
        }
        default:
            LOG_HEXDUMP_WRN(data, data_size, "MGMT_EVT_GRP_SMP: Data:");
            break;
    }
}

static void
mgmt_cb_on_env_grp_os(const uint32_t event, void* data, size_t data_size)
{
    TLOG_WRN("MGMT_EVT_GRP_OS: event 0x%08x", event);
    switch (event)
    {
        case MGMT_EVT_OP_OS_MGMT_BOOTLOADER_INFO:
        {
            struct os_mgmt_bootloader_info_data* p_bootloader_info_data = (struct os_mgmt_bootloader_info_data*)data;
            TLOG_WRN("MGMT_EVT_OP_OS_MGMT_BOOTLOADER_INFO, decoded=%zu", *p_bootloader_info_data->decoded);
            if (NULL != p_bootloader_info_data->query)
            {
                LOG_HEXDUMP_INF(p_bootloader_info_data->query->value, p_bootloader_info_data->query->len, "Query:");
            }
            break;
        }
        default:
            LOG_WRN("%s: Event: 0x%08x (%s)", __func__, event, conv_mgmt_event_to_str(event));
            LOG_HEXDUMP_WRN(data, data_size, "MGMT_EVT_GRP_SMP: Data:");
            break;
    }
}

#if IS_ENABLED(CONFIG_IMG_MANAGER)
static void
mgmt_cb_on_env_grp_img(const uint32_t event, void* data, size_t data_size)
{
    TLOG_WRN("MGMT_EVT_GRP_IMG: event 0x%08x", event);
    switch (event)
    {
        case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK:
        {
            const struct img_mgmt_upload_check* const p_data = data;
            if (NULL != p_data->req)
            {
                LOG_WRN(
                    "MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: Req: image=%d, off=%u, size=%u, img_data.len=%u, data_sha.len=%u, "
                    "upgrade=%d",
                    p_data->req->image,
                    p_data->req->off,
                    p_data->req->size,
                    p_data->req->img_data.len,
                    p_data->req->data_sha.len,
                    p_data->req->upgrade);
            }
            else
            {
                LOG_WRN("MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: Req: NULL");
            }
            if (NULL != p_data->action)
            {
                LOG_WRN(
                    "MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: Action: size=%u, write_bytes=%u, area_id=%d, proceed=%d, erase=%d",
                    (uint32_t)p_data->action->size,
                    p_data->action->write_bytes,
                    p_data->action->area_id,
                    p_data->action->proceed,
                    p_data->action->erase);
            }
            else
            {
                LOG_WRN("MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK: Action: NULL");
            }
            break;
        }
        case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
            LOG_HEXDUMP_WRN(data, data_size, "MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED: Data:");
            break;
        case MGMT_EVT_OP_IMG_MGMT_DFU_STARTED:
        case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
        case MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED:
        case MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK_WRITE_COMPLETE:
            LOG_HEXDUMP_WRN(data, data_size, "MGMT_EVT_GRP_IMG: Data:");
            break;
        case MGMT_EVT_OP_IMG_MGMT_IMAGE_SLOT_STATE:
        {
            const struct img_mgmt_state_slot_encode* const p_data = data;
            LOG_WRN(
                "MGMT_EVT_OP_IMG_MGMT_IMAGE_SLOT_STATE: Slot=%d, version=%s, flags=0x%x, ok=%d",
                p_data->slot,
                p_data->version ? p_data->version : "NULL",
                p_data->flags,
                *p_data->ok);
        }
        break;
        default:
            LOG_HEXDUMP_WRN(data, data_size, "MGMT_EVT_GRP_IMG: Data:");
            break;
    }
}
#endif

static enum mgmt_cb_return
ble_adv_mgmt_cb(
    uint32_t            event,
    enum mgmt_cb_return prev_status,
    int32_t*            rc,
    uint16_t*           group,
    bool*               abort_more,
    void*               data,
    size_t              data_size)
{
    TLOG_WRN("%s: event 0x%08x (%s)", __func__, event, conv_mgmt_event_to_str(event));
    if (0 != data_size)
    {
        const uint16_t event_grp = event >> 16;
        if (event_grp == MGMT_EVT_GRP_SMP)
        {
            mgmt_cb_on_env_grp_smp(event, data, data_size);
        }
        else if (event_grp == MGMT_EVT_GRP_OS)
        {
            mgmt_cb_on_env_grp_os(event, data, data_size);
#if IS_ENABLED(CONFIG_IMG_MANAGER)
        }
        else if (event_grp == MGMT_EVT_GRP_IMG)
        {
            mgmt_cb_on_env_grp_img(event, data, data_size);
#endif
        }
        else
        {
            TLOG_WRN("Unknown event group, event 0x%08x", event);
        }
    }
    return MGMT_CB_OK;
}

static struct mgmt_callback g_mgmt_cb = {
    .callback = &ble_adv_mgmt_cb,
    .event_id = MGMT_EVT_OP_ALL,
};
#endif

void
ble_mgmt_hooks_init(void)
{
#if BLE_MGMT_HOOKS_ENABLED && IS_ENABLED(CONFIG_MCUMGR)
    mgmt_callback_register(&g_mgmt_cb);
#endif
}
