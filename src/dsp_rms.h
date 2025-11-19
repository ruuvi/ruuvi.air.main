/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef DSP_RMS_H
#define DSP_RMS_H

#include <stdint.h>
#include <zephyr/dsp/types.h>

#ifdef __cplusplus
extern "C" {
#endif

q63_t
dsp_sum_of_square_q15(const q15_t* p_src, const uint32_t block_size);

float32_t
dsp_sum_of_square_f32(const float32_t* p_src, const uint32_t block_size);

float32_t
dsp_rms_q15_f32(const q15_t* p_src, const uint32_t block_size);

q31_t
dsp_calc_sum_q15_q31(const q15_t* p_src, const uint16_t block_size);

#ifdef __cplusplus
}
#endif

#endif // DSP_RMS_H
