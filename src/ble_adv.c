/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "ble_adv.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/logging/log_backend_ble.h>
#include "ruuvi_air_types.h"
#include "tlog.h"
#include "nfc.h"
#include "nus.h"
#include "utils.h"
#include "ble_mgmt_hooks.h"
#include "opt_rgb_ctrl.h"
#include "data_fmt_e1.h"
#include "data_fmt_6.h"
#include "sys_utils.h"
#include "zephyr_api.h"

_Static_assert(sizeof(measurement_cnt_t) == sizeof(re_e1_seq_cnt_t), "");
_Static_assert(sizeof(radio_mac_t) == sizeof(re_e1_mac_addr_t), "");

LOG_MODULE_REGISTER(ble_adv, LOG_LEVEL_INF);

#define USE_BLE (1 && IS_ENABLED(CONFIG_RUUVI_AIR_USE_BLE))

#define RUUVI_BLE_ADV_NORMAL_IS_ENABLED   IS_ENABLED(CONFIG_RUUVI_AIR_USE_BLE_ADV_NORMAL)
#define RUUVI_BLE_ADV_EXTENDED_IS_ENABLED IS_ENABLED(CONFIG_RUUVI_AIR_USE_BLE_ADV_EXTENDED)
#define RUUVI_BLE_ADV_CODED_IS_ENABLED    IS_ENABLED(CONFIG_RUUVI_AIR_USE_BLE_ADV_CODED)

#define RUUVI_BLE_ADV_NORMAL_IS_CONNECTABLE   (1)
#define RUUVI_BLE_ADV_EXTENDED_IS_CONNECTABLE (1)
#define RUUVI_BLE_ADV_CODED_IS_CONNECTABLE    (0)

#define RUUVI_ADV_INTERVAL_MIN (338 /* 211.25 ms */)
#define RUUVI_ADV_INTERVAL_MAX (510 /* 318.75 ms */)

#define RUUVI_CODED_ADV_INTERVAL_MIN (1280 /* 800 ms */)
#define RUUVI_CODED_ADV_INTERVAL_MAX (1600 /* 1000 ms */)

#define RUUVI_MANUFACTURER_ID (0x0499U)
#define RUUVI_SERVICE_UUID    (0xFC98)

#define NUM_RECORDS_IN_ADVS_PACKET     (3)
#define NUM_RECORDS_IN_EXT_ADVS_PACKET (2)
#define NUM_RECORDS_IN_SCAN_RSP_PACKET (1)

#define BLE_MANUFACTURER_DATA_BUF_SIZE_LEGACY   (22)
#define BLE_MANUFACTURER_DATA_BUF_SIZE_EXTENDED (42)
#define BLE_MANUFACTURER_DATA_OFFSET            (2)

typedef enum ble_adv_type_e
{
    BLE_ADV_TYPE_NORMAL   = 0,
    BLE_ADV_TYPE_EXTENDED = 1,
    BLE_ADV_TYPE_CODED    = 2,
    BLE_ADV_TYPE_NUM,
} ble_adv_type_e;

typedef struct ble_adv_params_t
{
    const uint32_t              bt_le_adv_opts;
    const uint32_t              interval_min;
    const uint32_t              interval_max;
    const bool                  is_connectable;
    const struct bt_data* const ad;
    const size_t                ad_len;
    const struct bt_data* const sd;
    const size_t                sd_len;
} ble_adv_params_t;

typedef struct ble_adv_info_t
{
    const char* const             name;
    const ble_adv_params_t        params;
    const struct bt_le_ext_adv_cb adv_cb;
    const bool                    is_enabled;
    bool                          is_active;
    bool                          is_connectable;
    struct bt_le_ext_adv*         p_adv;
    struct bt_conn*               p_conn;
} ble_adv_info_t;

static char g_bt_name[sizeof(CONFIG_BT_DEVICE_NAME) + 5];

static uint8_t g_mfg_data[BLE_MANUFACTURER_DATA_BUF_SIZE_LEGACY] = {
    RUUVI_MANUFACTURER_ID & BYTE_MASK,
    (RUUVI_MANUFACTURER_ID >> BYTE_SHIFT_1) & BYTE_MASK,
};

static uint8_t g_mfg_data_ext[BLE_MANUFACTURER_DATA_BUF_SIZE_EXTENDED] = {
    RUUVI_MANUFACTURER_ID & BYTE_MASK,
    (RUUVI_MANUFACTURER_ID >> BYTE_SHIFT_1) & BYTE_MASK,
};

static const struct bt_data g_ad[NUM_RECORDS_IN_ADVS_PACKET] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, RUUVI_SERVICE_UUID& BYTE_MASK, (RUUVI_SERVICE_UUID >> BYTE_SHIFT_1) & BYTE_MASK),
#if IS_ENABLED(CONFIG_RUUVI_AIR_ENABLE_BLE_LOGGING)
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, LOGGER_BACKEND_BLE_ADV_UUID_DATA),
#else
    BT_DATA(BT_DATA_MANUFACTURER_DATA, g_mfg_data, sizeof(g_mfg_data)),
#endif
};

static const struct bt_data g_ad_ext[NUM_RECORDS_IN_EXT_ADVS_PACKET] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA, g_mfg_data_ext, sizeof(g_mfg_data_ext)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, RUUVI_SERVICE_UUID& BYTE_MASK, (RUUVI_SERVICE_UUID >> BYTE_SHIFT_1) & BYTE_MASK),
};

static const struct bt_data g_sd[NUM_RECORDS_IN_SCAN_RSP_PACKET] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, g_bt_name, sizeof(g_bt_name) - 1),
};

static void
adv_norm_connected_cb(struct bt_le_ext_adv* p_adv, struct bt_le_ext_adv_connected_info* p_conn_info);
static void
adv_ext_connected_cb(struct bt_le_ext_adv* p_adv, struct bt_le_ext_adv_connected_info* p_conn_info);
static void
adv_coded_connected_cb(struct bt_le_ext_adv* p_adv, struct bt_le_ext_adv_connected_info* p_conn_info);
static void
adv_norm_sent_cb(struct bt_le_ext_adv* adv, struct bt_le_ext_adv_sent_info* info);
static void
adv_ext_sent_cb(struct bt_le_ext_adv* adv, struct bt_le_ext_adv_sent_info* info);
static void
adv_coded_sent_cb(struct bt_le_ext_adv* adv, struct bt_le_ext_adv_sent_info* info);

static ble_adv_info_t g_ble_adv_info[BLE_ADV_TYPE_NUM] = {
    [BLE_ADV_TYPE_NORMAL] = {
        .name = "Normal",
        .params    = {
            .bt_le_adv_opts = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_SCANNABLE,
            .interval_min = RUUVI_ADV_INTERVAL_MIN,
            .interval_max = RUUVI_ADV_INTERVAL_MAX,
            .is_connectable = RUUVI_BLE_ADV_NORMAL_IS_CONNECTABLE,
            .ad = g_ad,
            .ad_len = ARRAY_SIZE(g_ad),
            .sd = g_sd,
            .sd_len = ARRAY_SIZE(g_sd),
        },
        .adv_cb    = {
            .connected = &adv_norm_connected_cb,
            .sent      = &adv_norm_sent_cb,
        },
        .is_enabled = RUUVI_BLE_ADV_NORMAL_IS_ENABLED,
        .is_active = false,
        .is_connectable = false,
        .p_adv     = NULL,
        .p_conn    = NULL,
    },
    [BLE_ADV_TYPE_EXTENDED] = {
        .name = "Extended",
        .params    = {
            .bt_le_adv_opts = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_TX_POWER,
            .interval_min = RUUVI_ADV_INTERVAL_MIN,
            .interval_max = RUUVI_ADV_INTERVAL_MAX,
            .is_connectable = RUUVI_BLE_ADV_EXTENDED_IS_CONNECTABLE,
            .ad = g_ad_ext,
            .ad_len = ARRAY_SIZE(g_ad_ext),
            .sd = NULL,
            .sd_len = 0,
        },
        .adv_cb    = {
            .connected = &adv_ext_connected_cb,
            .sent = &adv_ext_sent_cb,
        },
        .is_enabled = RUUVI_BLE_ADV_EXTENDED_IS_ENABLED,
        .is_active = false,
        .is_connectable = false,
        .p_adv     = NULL,
        .p_conn    = NULL,
    },
    [BLE_ADV_TYPE_CODED] = {
        .name = "Coded",
        .params    = {
            .bt_le_adv_opts = BT_LE_ADV_OPT_USE_IDENTITY
                | BT_LE_ADV_OPT_EXT_ADV
                | BT_LE_ADV_OPT_CODED
                | BT_LE_ADV_OPT_USE_TX_POWER,
            .interval_min = RUUVI_CODED_ADV_INTERVAL_MIN,
            .interval_max = RUUVI_CODED_ADV_INTERVAL_MAX,
            .is_connectable = RUUVI_BLE_ADV_CODED_IS_CONNECTABLE,
            .ad = g_ad_ext,
            .ad_len = ARRAY_SIZE(g_ad_ext),
            .sd = NULL,
            .sd_len = 0,
        },
        .adv_cb    = {
            .connected = &adv_coded_connected_cb,
            .sent = &adv_coded_sent_cb,
        },
        .is_enabled = RUUVI_BLE_ADV_CODED_IS_ENABLED,
        .is_active = false,
        .is_connectable = false,
        .p_adv     = NULL,
        .p_conn    = NULL,
    },
};

static radio_mac_t g_ble_mac;

static struct k_work g_advertise_work;

static sensors_measurement_t g_measurement;
static measurement_cnt_t     g_measurement_cnt;
static sensors_flags_t       g_ble_adv_flags;

static void
send_data_over_nus(void)
{
    for (int32_t i = 0; i < BLE_ADV_TYPE_NUM; ++i)
    {
        ble_adv_info_t* const p_adv_info = &g_ble_adv_info[i];
        if ((NULL != p_adv_info->p_conn) && nus_is_notif_enabled())
        {
            const zephyr_api_ret_t res = bt_nus_send(
                p_adv_info->p_conn,
                &g_mfg_data_ext[BLE_MANUFACTURER_DATA_OFFSET],
                RE_E1_OFFSET_ADDR_MSB);
            if (0 != res)
            {
                TLOG_ERR("nus_send_data failed, err %d", res);
            }
        }
    }
}

static void
update_ble_adv_data(
    const sensors_measurement_t* const p_measurement,
    const measurement_cnt_t            measurement_cnt,
    const radio_mac_t                  radio_mac,
    const sensors_flags_t              flags)
{
    const re_6_data_t data_format_6 = data_fmt_6_init(
        p_measurement,
        (uint16_t)(measurement_cnt & UINT16_MASK),
        radio_mac,
        (re_6_flags_t) {
            .flag_calibration_in_progress = flags.flag_calibration_in_progress,
            .flag_button_pressed          = flags.flag_button_pressed,
            .flag_rtc_running_on_boot     = flags.flag_rtc_running_on_boot,
        });
    re_status_t enc_code = re_6_encode(&g_mfg_data[BLE_MANUFACTURER_DATA_OFFSET], &data_format_6);
    if (RE_SUCCESS != enc_code)
    {
        TLOG_ERR("re_6_encode failed (err %d)", enc_code);
    }
#if !IS_ENABLED(CONFIG_RUUVI_AIR_ENABLE_BLE_LOGGING)
    LOG_HEXDUMP_INF(g_mfg_data, sizeof(g_mfg_data), "Sending advertising data:");
#endif
    nfc_update_data(&g_mfg_data[BLE_MANUFACTURER_DATA_OFFSET], sizeof(g_mfg_data) - BLE_MANUFACTURER_DATA_OFFSET);

    memset(
        &g_mfg_data_ext[BLE_MANUFACTURER_DATA_OFFSET],
        UINT8_MAX,
        sizeof(g_mfg_data_ext) - BLE_MANUFACTURER_DATA_OFFSET);
    const re_e1_data_t data_e1 = data_fmt_e1_init(
        p_measurement,
        measurement_cnt,
        radio_mac,
        (re_e1_flags_t) {
            .flag_calibration_in_progress = flags.flag_calibration_in_progress,
            .flag_button_pressed          = flags.flag_button_pressed,
            .flag_rtc_running_on_boot     = flags.flag_rtc_running_on_boot,
        });
    enc_code = re_e1_encode(&g_mfg_data_ext[BLE_MANUFACTURER_DATA_OFFSET], &data_e1);
    if (RE_SUCCESS != enc_code)
    {
        TLOG_ERR("re_e0_encode failed (err %d)", enc_code);
    }
#if !IS_ENABLED(CONFIG_RUUVI_AIR_ENABLE_BLE_LOGGING)
    LOG_HEXDUMP_INF(g_mfg_data_ext, sizeof(g_mfg_data_ext), "Sending extended advertising data:");
#endif

    // Send data to connected device via NUS
    if (!nus_is_reading_hist_in_progress())
    {
        send_data_over_nus();
    }
}

static ble_adv_info_t*
ble_adv_find_by_adv(const struct bt_le_ext_adv* const p_adv)
{
    for (int32_t i = 0; i < BLE_ADV_TYPE_NUM; ++i)
    {
        if (g_ble_adv_info[i].p_adv == p_adv)
        {
            return &g_ble_adv_info[i];
        }
    }
    return NULL;
}

static ble_adv_info_t*
ble_adv_find_by_conn(const struct bt_conn* const p_conn)
{
    for (int32_t i = 0; i < BLE_ADV_TYPE_NUM; ++i)
    {
        if (g_ble_adv_info[i].p_conn == p_conn)
        {
            return &g_ble_adv_info[i];
        }
    }
    return NULL;
}

static void
on_connect_handler(ble_adv_info_t* const p_info, struct bt_conn* const p_conn)
{
    if (NULL == p_info)
    {
        TLOG_ERR("Could not find advertiser");
        return;
    }
    TLOG_WRN("Advertising was automatically stopped for Advertiser[%s]", p_info->name);

    TLOG_INF("Switch to 15 ms interval for Advertiser[%s]", g_ble_adv_info[BLE_ADV_TYPE_NORMAL].name);
    struct bt_le_conn_param conn_params = *BT_LE_CONN_PARAM(
        0x000c, /* interval_min: 0x000c * 1.25 ms = 15 ms */
        0x000c, /* interval_max: same as min for stable timing */
        0,      /* slave latency */
        400     /* supervision timeout (500 ms) */
    );
    zephyr_api_ret_t res = bt_conn_le_param_update(p_conn, &conn_params);
    if (0 != res)
    {
        TLOG_ERR("PHY update request failed for 15 ms connection interval, err %d", res);
        TLOG_INF("Switch to 20 ms interval for Advertiser[%s]", g_ble_adv_info[BLE_ADV_TYPE_NORMAL].name);
        conn_params = *BT_LE_CONN_PARAM(
            0x0010, /* interval_min: 0x0010 * 1.25 ms = 20 ms */
            0x0010, /* interval_max: same as min for stable timing */
            0,      /* slave latency */
            400     /* supervision timeout (500 ms) */
        );
        res = bt_conn_le_param_update(p_conn, &conn_params);
        if (0 != res)
        {
            TLOG_ERR("PHY update request failed for 20 ms connection interval, err %d", res);
        }
    }

    p_info->p_conn    = p_conn;
    p_info->is_active = false;
    k_work_submit(&g_advertise_work);
}

static void
adv_norm_connected_cb(struct bt_le_ext_adv* p_adv, struct bt_le_ext_adv_connected_info* p_conn_info)
{
    TLOG_WRN(
        "Advertiser[%s] (idx=%d) connected, p_adv=%p, p_conn=%p",
        g_ble_adv_info[BLE_ADV_TYPE_NORMAL].name,
        bt_le_ext_adv_get_index(p_adv),
        (void*)p_adv,
        (void*)p_conn_info->conn);

    TLOG_INF("Switch PHY to 2M for Advertiser[%s]", g_ble_adv_info[BLE_ADV_TYPE_NORMAL].name);
    zephyr_api_ret_t res = bt_conn_le_phy_update(p_conn_info->conn, BT_CONN_LE_PHY_PARAM_2M);
    if (0 != res)
    {
        TLOG_ERR("PHY update request for 2M failed, err %d", res);
    }

    ble_adv_info_t* const p_info = ble_adv_find_by_adv(p_adv);
    on_connect_handler(p_info, p_conn_info->conn);
}

static void
adv_ext_connected_cb(struct bt_le_ext_adv* p_adv, struct bt_le_ext_adv_connected_info* p_conn_info)
{
    TLOG_WRN(
        "Advertiser[%s] (idx=%d) connected, p_adv=%p, p_conn=%p",
        g_ble_adv_info[BLE_ADV_TYPE_EXTENDED].name,
        bt_le_ext_adv_get_index(p_adv),
        (void*)p_adv,
        (void*)p_conn_info->conn);
    ble_adv_info_t* const p_info = ble_adv_find_by_adv(p_adv);
    on_connect_handler(p_info, p_conn_info->conn);
}

static void
adv_coded_connected_cb(struct bt_le_ext_adv* p_adv, struct bt_le_ext_adv_connected_info* p_conn_info)
{
    TLOG_WRN(
        "Advertiser[%s] (idx=%d) connected, p_adv=%p, p_conn=%p",
        g_ble_adv_info[BLE_ADV_TYPE_CODED].name,
        bt_le_ext_adv_get_index(p_adv),
        (void*)p_adv,
        (void*)p_conn_info->conn);
    ble_adv_info_t* const p_info = ble_adv_find_by_adv(p_adv);
    on_connect_handler(p_info, p_conn_info->conn);
}

static void
adv_norm_sent_cb(__unused struct bt_le_ext_adv* adv, __unused struct bt_le_ext_adv_sent_info* info)
{
    TLOG_DBG("Advertiser[%s] sent callback called", g_ble_adv_info[BLE_ADV_TYPE_NORMAL].name);
}

static void
adv_ext_sent_cb(__unused struct bt_le_ext_adv* adv, __unused struct bt_le_ext_adv_sent_info* info)
{
    TLOG_DBG("Advertiser[%s] sent callback called", g_ble_adv_info[BLE_ADV_TYPE_EXTENDED].name);
}

static void
adv_coded_sent_cb(__unused struct bt_le_ext_adv* adv, __unused struct bt_le_ext_adv_sent_info* info)
{
    TLOG_DBG("Advertiser[%s] sent callback called", g_ble_adv_info[BLE_ADV_TYPE_CODED].name);
}

static bool
ble_adv_recreate(ble_adv_info_t* const p_info, const bool flag_connectable)
{
    if (NULL == p_info)
    {
        TLOG_ERR("p_info is NULL");
        return false;
    }
    if (!p_info->is_enabled)
    {
        TLOG_ERR("Advertiser[%s] is not enabled", p_info->name);
        return false;
    }
    if (NULL != p_info->p_adv)
    {
        TLOG_WRN("Stop advertising for Advertiser[%s]", p_info->name);
        zephyr_api_ret_t res = bt_le_ext_adv_stop(p_info->p_adv);
        if (0 != res)
        {
            TLOG_ERR("bt_le_ext_adv_stop failed for Advertiser[%s], err %d", p_info->name, res);
            return false;
        }
        res = bt_le_ext_adv_delete(p_info->p_adv);
        if (0 != res)
        {
            TLOG_ERR("bt_le_ext_adv_delete failed for Advertiser[%s], err %d", p_info->name, res);
            return false;
        }
        p_info->p_adv     = NULL;
        p_info->is_active = false;
    }

    p_info->is_connectable = flag_connectable;
    TLOG_WRN("Creating new Advertiser[%s]: %s", p_info->name, flag_connectable ? "connectable" : "non-connectable");
    uint32_t bt_le_adv_opts = p_info->params.bt_le_adv_opts;
    bt_le_adv_opts |= (flag_connectable ? BT_LE_ADV_OPT_CONNECTABLE : 0U);
    if (!flag_connectable)
    {
        bt_le_adv_opts &= (uint32_t)~BT_LE_ADV_OPT_SCANNABLE;
    }
    const zephyr_api_ret_t res = bt_le_ext_adv_create(
        BT_LE_ADV_PARAM(bt_le_adv_opts, p_info->params.interval_min, p_info->params.interval_max, NULL),
        &p_info->adv_cb,
        &p_info->p_adv);
    if (0 != res)
    {
        TLOG_ERR("bt_le_ext_adv_create failed for Advertiser(%s), err %d", p_info->name, res);
        return false;
    }
    return true;
}

static bool
check_if_connection_established(void)
{
    for (int32_t i = 0; i < BLE_ADV_TYPE_NUM; ++i)
    {
        const ble_adv_info_t* const p_info = &g_ble_adv_info[i];
        if (NULL != p_info->p_conn)
        {
            return true;
        }
    }
    return false;
}

static bool
ble_adv_recreate_if_needed(ble_adv_info_t* const p_info, const bool flag_connection_established)
{
    bool flag_need_recreate = !p_info->is_active;
    if (p_info->is_active)
    {
        if (flag_connection_established)
        {
            flag_need_recreate = flag_need_recreate || p_info->is_connectable;
        }
        else
        {
            flag_need_recreate = flag_need_recreate || (p_info->params.is_connectable && (!p_info->is_connectable));
        }
    }

    if (flag_need_recreate)
    {
        const bool flag_connectable = (!flag_connection_established) && p_info->params.is_connectable;
        if (!ble_adv_recreate(p_info, flag_connectable))
        {
            TLOG_ERR("ble_adv_recreate failed for Advertiser[%s]", p_info->name);
            return false;
        }
    }
    return true;
}

static size_t
ble_adv_remove_complete_name_from_adv_data(const ble_adv_info_t* const p_info, const bool flag_connection_established)
{
    size_t ad_len = p_info->params.ad_len;
    if (flag_connection_established && (NULL != p_info->params.ad) && (p_info->params.ad_len > 0))
    {
        // Remove Complete Local Name from advertising data if it's at the end
        const bool has_complete_name = (BT_DATA_NAME_COMPLETE == p_info->params.ad[ad_len - 1].type);
        if (has_complete_name)
        {
            ad_len -= 1;
        }
    }
    return ad_len;
}

static void
ble_adv_advertise_on_phy(ble_adv_info_t* const p_info, const bool flag_connection_established)
{
    if (!ble_adv_recreate_if_needed(p_info, flag_connection_established))
    {
        return;
    }
    const size_t     ad_len = ble_adv_remove_complete_name_from_adv_data(p_info, flag_connection_established);
    zephyr_api_ret_t err    = bt_le_ext_adv_set_data(
        p_info->p_adv,
        p_info->params.ad,
        ad_len,
        p_info->params.sd,
        p_info->params.sd_len);
    if (0 != err)
    {
        TLOG_ERR("bt_le_ext_adv_set_data failed for Advertiser[%s], err %d", p_info->name, err);
        return;
    }
    if (!p_info->is_active)
    {
        struct bt_le_ext_adv_start_param adv_start_param[1] = { BT_LE_EXT_ADV_START_PARAM_INIT(0, 0) };

        TLOG_WRN("Start advertising for Advertiser[%s]", p_info->name);
        err = bt_le_ext_adv_start(p_info->p_adv, adv_start_param);
        if (0 != err)
        {
            TLOG_ERR("bt_le_ext_adv_start failed for Advertiser[%s], err %d", p_info->name, err);
            return;
        }
        p_info->is_active = true;
    }
}

static void
advertise(__unused struct k_work* work)
{
    update_ble_adv_data(&g_measurement, g_measurement_cnt, g_ble_mac, g_ble_adv_flags);

    const bool flag_connection_established = check_if_connection_established();

    for (int32_t i = 0; i < BLE_ADV_TYPE_NUM; ++i)
    {
        ble_adv_info_t* const p_info = &g_ble_adv_info[i];
        if (!p_info->is_enabled)
        {
            continue;
        }
        ble_adv_advertise_on_phy(p_info, flag_connection_established);
    }
}

static void
set_bluetooth_device_name(const radio_mac_t mac)
{
    (void)snprintf(
        g_bt_name,
        sizeof(g_bt_name),
        "%s %02X%02X",
        CONFIG_BT_DEVICE_NAME,
        (uint8_t)((mac >> BYTE_SHIFT_1) & BYTE_MASK),
        (uint8_t)mac & BYTE_MASK);
    TLOG_INF("BLE Device Name: %s", g_bt_name);
    const zephyr_api_ret_t err = bt_set_name(g_bt_name);
    if (0 != err)
    {
        TLOG_ERR("Failed to set Bluetooth name, err %d", err);
    }
}

#if IS_ENABLED(CONFIG_RUUVI_AIR_ENABLE_BLE_LOGGING)
static void
logger_hook(bool enabled, void* ctx)
{
    TLOG_WRN("BLE logger backend: %s", enabled ? "enabled" : "disabled");
}
#endif

bool
ble_adv_init(void)
{
#if USE_BLE
    k_work_init(&g_advertise_work, &advertise);

    if (!nus_init())
    {
        TLOG_ERR("nus_init failed");
        return false;
    }

    ble_mgmt_hooks_init();

#if IS_ENABLED(CONFIG_RUUVI_AIR_ENABLE_BLE_LOGGING)
    logger_backend_ble_set_hook(logger_hook, NULL);
#endif

    zephyr_api_ret_t err = bt_enable(NULL);
    if (0 != err)
    {
        TLOG_ERR("Bluetooth init failed (err %d)", err);
        return false;
    }
    g_ble_mac = radio_address_get();
    set_bluetooth_device_name(g_ble_mac);

    TLOG_INF("Bluetooth initialized");
#endif
    return true;
}

radio_mac_t
ble_adv_get_mac(void)
{
    return g_ble_mac;
}

void
ble_adv_restart(
    const sensors_measurement_t* const p_measurement,
    const measurement_cnt_t            measurement_cnt,
    const sensors_flags_t              flags)
{
    g_measurement     = *p_measurement;
    g_measurement_cnt = measurement_cnt;
    g_ble_adv_flags   = flags;
#if USE_BLE
    k_work_submit(&g_advertise_work);
#endif
}

static void
connected(struct bt_conn* conn, uint8_t err)
{
    TLOG_WRN("Connected, conn=%p", (void*)conn);
    if (0 != err)
    {
        TLOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }
}

static void
disconnected(struct bt_conn* p_conn, uint8_t reason)
{
    TLOG_WRN("Disconnected, conn=%p (reason 0x%02x)", (void*)p_conn, reason);

    ble_adv_info_t* const p_info = ble_adv_find_by_conn(p_conn);
    if (NULL == p_info)
    {
        TLOG_ERR("Could not find advertiser for conn=%p", (void*)p_conn);
        return;
    }

    p_info->p_conn    = NULL;
    p_info->is_active = false;
    k_work_submit(&g_advertise_work);
    opt_rgb_ctrl_enable_led(true);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
};
