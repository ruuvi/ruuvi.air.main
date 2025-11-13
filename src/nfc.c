/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "nfc.h"
#include <stdio.h>
#include <nfc_t2t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>
#include <zephyr/logging/log.h>
#include "app_led.h"
#include "ruuvi_endpoint_f0.h"
#include "app_fw_ver.h"
#include "app_version.h"
#include "utils.h"

LOG_MODULE_REGISTER(NFC, LOG_LEVEL_WRN);

#define USE_NFC (1)

#define NFC_FIELD_LED DK_LED4

#define MAX_REC_COUNT     4
#define NDEF_MSG_BUF_SIZE 256

static bool g_nfc_active = false;

static uint8_t       nfc_payload_id[]  = { 'I', 'D', ':', ' ', 'X', 'X', ':', 'X', 'X', ':', 'X', 'X', ':', 'X',
                                           'X', ':', 'X', 'X', ':', 'X', 'X', ':', 'X', 'X', ':', 'X', 'X', '\0' };
static uint8_t       nfc_payload_mac[] = { 'M', 'A', 'C', ':', ' ', 'X', 'X', ':', 'X', 'X', ':', 'X',
                                           'X', ':', 'X', 'X', ':', 'X', 'X', ':', 'X', 'X', '\0' };
static uint8_t       nfc_payload_sw[6 + sizeof(CONFIG_BT_DEVICE_NAME) + sizeof(APP_VERSION_EXTENDED_STRING)];
static uint8_t       nfc_payload_data[RE_F0_DATA_LENGTH];
static const uint8_t nfc_payload_id_lang_code[]   = { 'i', 'd' };
static const uint8_t nfc_payload_mac_lang_code[]  = { 'a', 'd' };
static const uint8_t nfc_payload_sw_lang_code[]   = { 's', 'w' };
static const uint8_t nfc_payload_data_lang_code[] = { 'd', 't' };

/* Buffer used to hold an NFC NDEF message. */
static uint8_t ndef_msg_buf[NDEF_MSG_BUF_SIZE];

static void
nfc_callback(void* context, nfc_t2t_event_t event, const uint8_t* data, size_t data_length)
{
    ARG_UNUSED(context);
    ARG_UNUSED(data);
    ARG_UNUSED(data_length);

    switch (event)
    {
        case NFC_T2T_EVENT_FIELD_ON:
            LOG_INF("NFC_T2T_EVENT_FIELD_ON");
            app_led_green_set_if_button_is_not_pressed(true);
            break;
        case NFC_T2T_EVENT_FIELD_OFF:
            LOG_INF("NFC_T2T_EVENT_FIELD_OFF");
            app_led_green_set_if_button_is_not_pressed(false);
            break;
        default:
            break;
    }
}

/**
 * @brief Function for encoding the NDEF text message.
 */
static bool
encode_nfc_msgs(uint8_t* buffer, uint32_t* len)
{
    int err;

    NFC_NDEF_TEXT_RECORD_DESC_DEF(
        nfc_en_text_rec_id,
        UTF_8,
        nfc_payload_id_lang_code,
        sizeof(nfc_payload_id_lang_code),
        nfc_payload_id,
        strlen(nfc_payload_id));
    NFC_NDEF_TEXT_RECORD_DESC_DEF(
        nfc_en_text_rec_mac,
        UTF_8,
        nfc_payload_mac_lang_code,
        sizeof(nfc_payload_mac_lang_code),
        nfc_payload_mac,
        strlen(nfc_payload_mac));
    NFC_NDEF_TEXT_RECORD_DESC_DEF(
        nfc_en_text_rec_sw,
        UTF_8,
        nfc_payload_sw_lang_code,
        sizeof(nfc_payload_sw_lang_code),
        nfc_payload_sw,
        strlen(nfc_payload_sw));
    NFC_NDEF_TEXT_RECORD_DESC_DEF(
        nfc_en_text_rec_data,
        UTF_8,
        nfc_payload_data_lang_code,
        sizeof(nfc_payload_data_lang_code),
        nfc_payload_data,
        sizeof(nfc_payload_data));

    /* Create NFC NDEF message description, capacity - MAX_REC_COUNT records */
    NFC_NDEF_MSG_DEF(nfc_text_msg, MAX_REC_COUNT);

    /* Add text records to NDEF text message */
    LOG_INF("Record: ID: %s", nfc_payload_id);
    err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg), &NFC_NDEF_TEXT_RECORD_DESC(nfc_en_text_rec_id));
    if (err < 0)
    {
        LOG_ERR("nfc_ndef_msg_record_add(id) failed, err=%d", err);
        return false;
    }
    LOG_INF("Record: MAC: %s", nfc_payload_mac);
    err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg), &NFC_NDEF_TEXT_RECORD_DESC(nfc_en_text_rec_mac));
    if (err < 0)
    {
        LOG_ERR("nfc_ndef_msg_record_add(mac) failed, err=%d", err);
        return false;
    }
    LOG_INF("Record: SW: %s", nfc_payload_sw);
    err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg), &NFC_NDEF_TEXT_RECORD_DESC(nfc_en_text_rec_sw));
    if (err < 0)
    {
        LOG_ERR("nfc_ndef_msg_record_add(sw) failed, err=%d", err);
        return false;
    }
    LOG_HEXDUMP_INF(nfc_payload_data, sizeof(nfc_payload_data), "Record: DATA:");
    err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg), &NFC_NDEF_TEXT_RECORD_DESC(nfc_en_text_rec_data));
    if (err < 0)
    {
        LOG_ERR("nfc_ndef_msg_record_add(data) failed, err=%d", err);
        return false;
    }

    err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg), buffer, len);
    if (err < 0)
    {
        LOG_ERR("nfc_ndef_msg_encode failed, err=%d", err);
        return false;
    }
    return true;
}

static bool
nfc_restart(void)
{
    if (g_nfc_active)
    {
        if (nfc_t2t_emulation_stop() < 0)
        {
            LOG_ERR("nfc_t2t_emulation_stop failed");
        }
        g_nfc_active = false;
    }

    uint32_t len = sizeof(ndef_msg_buf);
    if (!encode_nfc_msgs(ndef_msg_buf, &len))
    {
        LOG_ERR("Cannot encode NFC messages");
        return false;
    }

    /* Set created message as the NFC payload */
    if (nfc_t2t_payload_set(ndef_msg_buf, len) < 0)
    {
        LOG_ERR("nfc_t2t_payload_set failed");
        return false;
    }

    /* Start sensing NFC field */
    if (nfc_t2t_emulation_start() < 0)
    {
        LOG_ERR("nfc_t2t_emulation_start failed");
        return false;
    }
    g_nfc_active = true;
    return true;
}

bool
nfc_init(const uint64_t mac)
{
#if USE_NFC
    snprintf(nfc_payload_sw, sizeof(nfc_payload_sw), "SW: %s v%s", CONFIG_BT_DEVICE_NAME, app_fw_ver_get());

    const uint64_t device_id = get_device_id();
    snprintf(
        nfc_payload_id,
        sizeof(nfc_payload_id),
        "ID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        (uint8_t)(device_id >> 56) & 0xFF,
        (uint8_t)(device_id >> 48) & 0xFF,
        (uint8_t)(device_id >> 40) & 0xFF,
        (uint8_t)(device_id >> 32) & 0xFF,
        (uint8_t)(device_id >> 24) & 0xFF,
        (uint8_t)(device_id >> 16) & 0xFF,
        (uint8_t)(device_id >> 8) & 0xFF,
        (uint8_t)(device_id >> 0) & 0xFF);

    snprintf(
        nfc_payload_mac,
        sizeof(nfc_payload_mac),
        "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
        (uint8_t)(mac >> 40) & 0xFF,
        (uint8_t)(mac >> 32) & 0xFF,
        (uint8_t)(mac >> 24) & 0xFF,
        (uint8_t)(mac >> 16) & 0xFF,
        (uint8_t)(mac >> 8) & 0xFF,
        (uint8_t)(mac >> 0) & 0xFF);

    /* Set up NFC */
    if (nfc_t2t_setup(nfc_callback, NULL) < 0)
    {
        LOG_ERR("nfc_t2t_setup failed");
        return false;
    }

    return nfc_restart();
#else
    return true;
#endif
}

void
nfc_update_data(const uint8_t* const p_buf, const size_t buf_len)
{
#if USE_NFC
    if (buf_len != sizeof(nfc_payload_data))
    {
        LOG_ERR("Expected data len for NFC is %d bytes, got %d", sizeof(nfc_payload_data), buf_len);
        return;
    }
    memcpy(nfc_payload_data, p_buf, buf_len);
    nfc_restart();
#endif
}
