/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <zephyr/dsp/types.h>
#include <time.h>
#include "sen66_wrap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sensors_poll_result_e
{
    SENSORS_POLL_RESULT_OK,
    SENSORS_POLL_RESULT_ERR,
    SENSORS_POLL_RESULT_NOT_READY,
} sensors_poll_result_e;

typedef struct sensors_measurement_t
{
    sen66_wrap_measurement_t sen66;
    float32_t                dps310_temperature;
    float32_t                dps310_pressure;
    float32_t                shtc3_temperature;
    float32_t                shtc3_humidity;
    float32_t                luminosity;
    float32_t                sound_inst_dba;
    float32_t                sound_avg_dba;
    float32_t                sound_peak_spl_db;
    float32_t                air_quality_index;
    bool                     flag_nox_calibration_in_progress;
} sensors_measurement_t;

typedef struct sensors_flags_t
{
    bool flag_calibration_in_progress : 1; //!< Flag: Calibration in progress
    bool flag_button_pressed : 1;          //!< Flag: Button pressed
    bool flag_rtc_running_on_boot : 1;     //!< Flag: RTC was running on boot
} sensors_flags_t;

bool
sensors_init(void);

sensors_poll_result_e
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
