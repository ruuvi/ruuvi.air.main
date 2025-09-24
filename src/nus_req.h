/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef NUS_REQ_H
#define NUS_REQ_H

#include <stdint.h>
#include <stdbool.h>
#include "ruuvi_endpoints.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t nus_req_src_idx_t;

typedef uint32_t nus_req_time_t;

typedef struct nus_req_t
{
    re_type_t         req_re_type;
    nus_req_src_idx_t src_idx;
    re_op_t           req_re_op;
    nus_req_time_t    current_time_s;
    nus_req_time_t    start_time_s;
} nus_req_t;

bool
nus_req_parse(const uint8_t* const p_raw_message, const uint16_t len, nus_req_t* const p_req);

#ifdef __cplusplus
}
#endif

#endif // NUS_REQ_H
