/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <time.h>
#include "sen66_wrap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sensors_poll_result_t
{
    SENSORS_POLL_RESULT_OK,
    SENSORS_POLL_RESULT_ERR,
    SENSORS_POLL_RESULT_NOT_READY,
} sensors_poll_result_t;

typedef struct sensors_measurement_t
{
    sen66_wrap_measurement_t sen66;
    float                    dps310_temperature;
    float                    dps310_pressure;
    float                    shtc3_temperature;
    float                    shtc3_humidity;
    float                    luminosity;
    float                    sound_inst_dba;
    float                    sound_avg_dba;
    float                    sound_peak_spl_db;
    float                    air_quality_index;
} sensors_measurement_t;

bool
sensors_init(void);

sensors_poll_result_t
sensors_poll(const time_t cur_unix_time);

sensors_measurement_t
sensors_get_measurement(void);

void
sensors_reinit(void);

void
sensors_get_from_cache_sen66_voc_algorithm_state(
    uint32_t* const                    p_cur_unix_time32,
    sen66_voc_algorithm_state_t* const p_voc_alg_state);

#ifdef __cplusplus
}
#endif

#endif // SENSORS_H
