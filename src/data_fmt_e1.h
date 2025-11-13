/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef DATA_FMT_E1_H
#define DATA_FMT_E1_H

#include <stdint.h>
#include "ruuvi_endpoint_e1.h"
#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

re_e1_data_t
data_fmt_e1_init(
    const sensors_measurement_t* const p_measurement,
    const re_e1_seq_cnt_t              seq_cnt,
    const re_e1_mac_addr_t             radio_mac);

#ifdef __cplusplus
}
#endif

#endif // DATA_FMT_E1_H
