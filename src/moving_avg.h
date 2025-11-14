/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef MOVING_AVG_H
#define MOVING_AVG_H

#include <stdint.h>
#include <stdbool.h>
#include "ruuvi_air_types.h"
#include "sensors.h"
#include "hist_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void
moving_avg_init(void);

bool
moving_avg_append(const sensors_measurement_t* const p_measurement);

hist_log_record_data_t
moving_avg_get_accum(const measurement_cnt_t measurement_cnt, const radio_mac_t radio_mac, const sensors_flags_t flags);

#ifdef __cplusplus
}
#endif

#endif // MOVING_AVG_H
