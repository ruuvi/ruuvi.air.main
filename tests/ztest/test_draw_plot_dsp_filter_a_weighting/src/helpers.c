/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "helpers.h"

void
generate_sine_wave(
    float32_t* const p_buffer,
    const uint32_t   buffer_size,
    const float32_t  amplitude,
    const float32_t  frequency,
    const float32_t  phase,
    const uint32_t   sample_rate)
{
    for (uint32_t i = 0; i < buffer_size; i++)
    {
        p_buffer[i] = amplitude
                      * arm_sin_f32(2 * PI * (float32_t)frequency * (float32_t)i / (float)sample_rate + phase);
    }
}

void
convert_float_to_q15(const float32_t* const p_float_buffer, q15_t* const p_q15_buffer, const uint32_t num_samples)
{
    for (uint32_t i = 0; i < num_samples; i++)
    {
        p_q15_buffer[i] = (q15_t)lrintf(p_float_buffer[i] * MAX_Q15);
    }
}
