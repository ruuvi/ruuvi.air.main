
#include "dsp_biquad_filter_a_weighting_16000.h"
#include "arm_math.h"
#include "dsp/filtering_functions.h"
#include "dsp_arm_biquad_cascade_df1_q15_patched.h"

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_16000_HZ_F32 \
    { \
        .sos_f32 = { \
            0.5319997f, \
            1.0639994f, \
            0.5319997f, \
            -0.8215473f, \
            -0.1687350f, /* First section */ \
            1.0000000f, \
            -2.0000000f, \
            1.0000000f, \
            1.7054977f, \
            -0.7159799f, /* Second section */ \
            1.0000000f, \
            -2.0000000f, \
            1.0000000f, \
            1.9838901f, \
            -0.9839550f, /* Third section */ \
        } \
    }

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_16000_HZ_Q15 \
    { \
        .sos_q15 = { \
            8716, \
            0, \
            17433, \
            8716, \
            -13460, \
            -2765, /* First section */ \
            16384, \
            0, \
            -32768, \
            16384, \
            27943, \
            -11731, /* Second section */ \
            16384, \
            0, \
            -32768, \
            16384, \
            32504, \
            -16121 /* Third section */ \
        } \
    }

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_16000_HZ_Q15_POST_SHIFT (1)

typedef struct dsp_biquad_cascade_df1_a_weighting_filter_sos_f32_t
{
    float32_t sos_f32[DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_F32];
} dsp_biquad_cascade_df1_a_weighting_filter_sos_f32_t;

typedef struct dsp_biquad_cascade_df1_a_weighting_filter_sos_q15_t
{
    q15_t sos_q15[DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_Q15];
} dsp_biquad_cascade_df1_a_weighting_filter_sos_q15_t;

static const dsp_biquad_cascade_df1_a_weighting_filter_sos_f32_t g_sos_16000_hz_f32
    = DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_16000_HZ_F32;

static const dsp_biquad_cascade_df1_a_weighting_filter_sos_q15_t g_sos_16000_hz_q15
    = DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_16000_HZ_Q15;

void
dsp_biquad_filter_a_weighting_16000_f32(
    dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t* const p_state,
    const float32_t* const                                       p_in_buf,
    float32_t* const                                             p_out_buf,
    const uint32_t                                               num_samples)
{
    arm_biquad_casd_df1_inst_f32 filter = { 0 };

    arm_biquad_cascade_df1_init_f32(
        &filter,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES,
        g_sos_16000_hz_f32.sos_f32,
        p_state->state_f32);

    arm_biquad_cascade_df1_f32(&filter, p_in_buf, p_out_buf, num_samples);
}

void
dsp_biquad_filter_a_weighting_16000_q15_cmsis(
    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t* const p_state,
    const q15_t* const                                           p_in_buf,
    q15_t* const                                                 p_out_buf,
    const uint32_t                                               num_samples)
{
    arm_biquad_casd_df1_inst_q15 filter = { 0 };
    arm_biquad_cascade_df1_init_q15(
        &filter,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES,
        g_sos_16000_hz_q15.sos_q15,
        p_state->state_q15,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_16000_HZ_Q15_POST_SHIFT);
    arm_biquad_cascade_df1_q15(&filter, p_in_buf, p_out_buf, num_samples);
}

void
dsp_biquad_filter_a_weighting_16000_q15(
    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t* const p_state,
    const q15_t* const                                           p_in_buf,
    q15_t* const                                                 p_out_buf,
    const uint32_t                                               num_samples)
{
    arm_biquad_casd_df1_inst_q15 filter = { 0 };
    arm_biquad_cascade_df1_init_q15(
        &filter,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES,
        g_sos_16000_hz_q15.sos_q15,
        p_state->state_q15,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_16000_HZ_Q15_POST_SHIFT);
    arm_biquad_cascade_df1_q15_patched(&filter, p_in_buf, p_out_buf, num_samples);
}
