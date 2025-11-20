/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(DSP_BIQUAD_FILTER_A_WEIGHTING_16000_H)
#define DSP_BIQUAD_FILTER_A_WEIGHTING_16000_H

#include <stdint.h>
#include <arm_math.h>

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES           (3)
#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_FOR_ONE_STAGE (5)
#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_STATE_VARS_FOR_ONE_STAGE   (4)

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_F32 \
    (DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES \
     * DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_FOR_ONE_STAGE)

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_Q15 \
    (DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES \
     * (DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_FOR_ONE_STAGE + 1))

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_STATE_VARS \
    (DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES \
     * DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_STATE_VARS_FOR_ONE_STAGE)

typedef struct dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t
{
    float32_t state_f32[DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_STATE_VARS];
} dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t;

typedef struct dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t
{
    q15_t state_q15[DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_STATE_VARS];
} dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t;

void
dsp_biquad_filter_a_weighting_16000_f32(
    dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t* const p_state,
    const float32_t* const                                       p_in_buf,
    float32_t* const                                             p_out_buf,
    const uint32_t                                               num_samples);

void
dsp_biquad_filter_a_weighting_16000_q15_cmsis(
    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t* const p_state,
    const q15_t* const                                           p_in_buf,
    q15_t* const                                                 p_out_buf,
    const uint32_t                                               num_samples);

void
dsp_biquad_filter_a_weighting_16000_q15(
    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t* const p_state,
    const q15_t* const                                           p_in_buf,
    q15_t* const                                                 p_out_buf,
    const uint32_t                                               num_samples);

#endif // DSP_BIQUAD_FILTER_A_WEIGHTING_16000_H
