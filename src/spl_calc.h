/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef SPL_CALC_H
#define SPL_CALC_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/dsp/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void
spl_calc_init(void);

bool
spl_calc_handle_buffer(q15_t* const p_buffer, float32_t* const p_buf_f32, const uint16_t num_samples);

float32_t
spl_calc_get_rms_max(void);

float32_t
spl_calc_get_rms_avg(void);

float32_t
spl_calc_get_rms_last_max(void);

float32_t
spl_calc_get_rms_last_avg(void);

#ifdef __cplusplus
}
#endif

#endif // SPL_CALC_H
