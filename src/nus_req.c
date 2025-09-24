/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "nus_req.h"
#include "ruuvi_endpoints.h"
#include "tlog.h"

LOG_MODULE_DECLARE(nus, LOG_LEVEL_INF);

static bool
nus_req_parse_type(const uint8_t raw_req_type, re_type_t* const p_req_type)
{
    switch (raw_req_type)
    {
        case RE_STANDARD_DESTINATION_ACCELERATION:
            *p_req_type = RE_ACC_XYZ;
            break;
        case RE_STANDARD_DESTINATION_ACCELERATION_X:
            *p_req_type = RE_ACC_XYZ;
            break;
        case RE_STANDARD_DESTINATION_ACCELERATION_Y:
            *p_req_type = RE_ACC_XYZ;
            break;
        case RE_STANDARD_DESTINATION_ACCELERATION_Z:
            *p_req_type = RE_ACC_XYZ;
            break;
        case RE_STANDARD_DESTINATION_GYRATION:
            *p_req_type = RE_GYR_XYZ;
            break;
        case RE_STANDARD_DESTINATION_GYRATION_X:
            *p_req_type = RE_GYR_X;
            break;
        case RE_STANDARD_DESTINATION_GYRATION_Y:
            *p_req_type = RE_GYR_Y;
            break;
        case RE_STANDARD_DESTINATION_GYRATION_Z:
            *p_req_type = RE_GYR_Z;
            break;
        case RE_STANDARD_DESTINATION_ENVIRONMENTAL:
            *p_req_type = RE_ENV_ALL;
            break;
        case RE_STANDARD_DESTINATION_AIRQ:
            *p_req_type = RE_ENV_AIRQ;
            break;
        case RE_STANDARD_DESTINATION_TEMPERATURE:
            *p_req_type = RE_ENV_TEMP;
            break;
        case RE_STANDARD_DESTINATION_HUMIDITY:
            *p_req_type = RE_ENV_HUMI;
            break;
        case RE_STANDARD_DESTINATION_PRESSURE:
            *p_req_type = RE_ENV_PRES;
            break;
        case RE_STANDARD_DESTINATION_PASSWORD:
            *p_req_type = RE_SEC_PASS;
            break;

        default:
            TLOG_ERR("Unknown request type: %d", raw_req_type);
            return false;
    }

    return true;
}

static bool
nus_req_parse_op(const uint8_t raw_req_op, re_op_t* const p_req_op)
{
    switch (raw_req_op)
    {
        case RE_STANDARD_SENSOR_CONFIGURATION_WRITE:
            *p_req_op = RE_SENSOR_CONFIG_W;
            break;
        case RE_STANDARD_SENSOR_CONFIGURATION_READ:
            *p_req_op = RE_SENSOR_CONFIG_R;
            break;
        case RE_STANDARD_LOG_VALUE_WRITE:
            *p_req_op = RE_LOG_W;
            break;
        case RE_STANDARD_LOG_VALUE_READ:
            *p_req_op = RE_LOG_R;
            break;
        case RE_STANDARD_LOG_MULTI_WRITE:
            *p_req_op = RE_LOG_W_MULTI;
            break;
        case RE_STANDARD_LOG_MULTI_READ:
            *p_req_op = RE_LOG_R_MULTI;
            break;
        default:
            TLOG_ERR("Unknown request operation: %d", raw_req_op);
            return false;
    }
    return true;
}

bool
nus_req_parse(const uint8_t* const p_raw_message, const uint16_t len, nus_req_t* const p_req)
{
    if (NULL == p_raw_message)
    {
        TLOG_ERR("NULL message");
        return false;
    }
    if (len != RE_STANDARD_MESSAGE_LENGTH)
    {
        TLOG_ERR("Invalid message legnth: %d (expected %d)", len, RE_STANDARD_MESSAGE_LENGTH);
        return false;
    }
    if (!nus_req_parse_type(p_raw_message[RE_STANDARD_DESTINATION_INDEX], &p_req->req_re_type))
    {
        return false;
    }

    p_req->src_idx = p_raw_message[RE_STANDARD_SOURCE_INDEX];

    if (!nus_req_parse_op(p_raw_message[RE_STANDARD_OPERATION_INDEX], &p_req->req_re_op))
    {
        return false;
    }

    p_req->current_time_s = re_std_log_current_time(p_raw_message);
    p_req->start_time_s   = re_std_log_start_time(p_raw_message);

    if (p_req->current_time_s <= p_req->start_time_s)
    {
        TLOG_ERR("Invalid start time: %" PRIu32 " >= %" PRIu32, p_req->start_time_s, p_req->current_time_s);
        return false;
    }

    return true;
}
