/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef DATA_FMT_F0_H
#define DATA_FMT_F0_H

#include <stdint.h>
#include "ruuvi_endpoint_f0.h"
#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

re_f0_data_t
data_fmt_f0_init(
    const sensors_measurement_t* const p_measurement,
    const uint16_t                     measurement_cnt,
    const uint64_t                     radio_mac);

#ifdef __cplusplus
}
#endif

#endif // DATA_FMT_F0_H
