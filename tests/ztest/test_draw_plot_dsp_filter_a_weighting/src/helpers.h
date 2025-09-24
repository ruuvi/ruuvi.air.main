/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef HELPERS_H
#define HELPERS_H

#include <stdint.h>
#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_Q15 (32767)

typedef struct filter_a_weighting_result_t
{
    float32_t rms_f32_unfiltered;
    float32_t rms_f32_filtered;
    float32_t rms_q15_filtered_cmsis; // calculated by CMSIS-DSP
    float32_t rms_q15_filtered_patched;
} filter_a_weighting_result_t;

void
generate_sine_wave(
    float32_t* const p_buffer,
    const uint32_t   buffer_size,
    const float32_t  amplitude,
    const float32_t  frequency,
    const float32_t  phase,
    const uint32_t   sample_rate);

void
convert_float_to_q15(const float32_t* const p_float_buffer, q15_t* const p_q15_buffer, const uint32_t num_samples);

#ifdef __cplusplus
}
#endif

#endif // HELPERS_H
