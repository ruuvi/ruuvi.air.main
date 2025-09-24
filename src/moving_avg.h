/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef MOVING_AVG_H
#define MOVING_AVG_H

#include <stdint.h>
#include <stdbool.h>
#include "sensors.h"
#include "hist_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void
moving_avg_init(void);

bool
moving_avg_append(const sensors_measurement_t* p_measurement);

hist_log_record_data_t
moving_avg_get_accum(void);

#ifdef __cplusplus
}
#endif

#endif // MOVING_AVG_H
