/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_AIR_API_H
#define RUUVI_AIR_API_H

#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

float
api_calc_air_quality_index(const sensors_measurement_t* const p_measurement);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_AIR_API_H
