
#include "dsp_biquad_filter_a_weighting_20828.h"
#include "arm_math.h"
#include "dsp/filtering_functions.h"
#include "dsp_arm_biquad_cascade_df1_q15_patched.h"

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_20828_HZ_F32 \
    { \
        .sos_f32 = { \
            0.4653359f, \
            0.9306718f, \
            0.4653359f, \
            -0.5911916f, \
            -0.0873769f, /* First section */ \
            1.0000000f, \
            -2.0001416f, \
            1.0001416f, \
            1.7677357f, \
            -0.7741392f, /* Second section */ \
            1.0000000f, \
            -1.9998584f, \
            0.9998584f, \
            1.9876129f, \
            -0.9876513f, /* Third section */ \
        } \
    }

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_20828_HZ_Q15 \
    { \
        .sos_q15 = { \
            3812, \
            0, \
            7624, \
            3812, \
            -4843, \
            -716, /* First section */ \
            8192, \
            0, \
            -16385, \
            8193, \
            14481, \
            -6342, /* Second section */ \
            8192, \
            0, \
            -16383, \
            8191, \
            16283, \
            -8091, /* Third section */ \
        } \
    }

#define DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_20828_HZ_Q15_POST_SHIFT (2)

typedef struct dsp_biquad_cascade_df1_a_weighting_filter_sos_f32_t
{
    float32_t sos_f32[DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_F32];
} dsp_biquad_cascade_df1_a_weighting_filter_sos_f32_t;

typedef struct dsp_biquad_cascade_df1_a_weighting_filter_sos_q15_t
{
    q15_t sos_q15[DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_COEFFICIENTS_Q15];
} dsp_biquad_cascade_df1_a_weighting_filter_sos_q15_t;

static const dsp_biquad_cascade_df1_a_weighting_filter_sos_f32_t g_sos_20828_hz_f32
    = DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_20828_HZ_F32;

const dsp_biquad_cascade_df1_a_weighting_filter_sos_q15_t g_sos_20828_hz_q15
    = DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_20828_HZ_Q15;

void
dsp_biquad_filter_a_weighting_20828_f32(
    dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t* const p_state,
    const float32_t* const                                       p_in_buf,
    float32_t* const                                             p_out_buf,
    const uint32_t                                               num_samples)
{
    arm_biquad_casd_df1_inst_f32 filter = { 0 };

    arm_biquad_cascade_df1_init_f32(
        &filter,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES,
        g_sos_20828_hz_f32.sos_f32,
        p_state->state_f32);

    arm_biquad_cascade_df1_f32(&filter, p_in_buf, p_out_buf, num_samples);
}

void
dsp_biquad_filter_a_weighting_20828_q15_cmsis(
    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t* const p_state,
    const q15_t* const                                           p_in_buf,
    q15_t* const                                                 p_out_buf,
    const uint32_t                                               num_samples)
{
    arm_biquad_casd_df1_inst_q15 filter = { 0 };
    arm_biquad_cascade_df1_init_q15(
        &filter,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES,
        g_sos_20828_hz_q15.sos_q15,
        p_state->state_q15,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_20828_HZ_Q15_POST_SHIFT);
    arm_biquad_cascade_df1_q15(&filter, p_in_buf, p_out_buf, num_samples);
}

void
dsp_biquad_filter_a_weighting_20828_q15(
    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t* const p_state,
    const q15_t* const                                           p_in_buf,
    q15_t* const                                                 p_out_buf,
    const uint32_t                                               num_samples)
{
    arm_biquad_casd_df1_inst_q15 filter = { 0 };
    arm_biquad_cascade_df1_init_q15(
        &filter,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_NUM_2ND_ORDER_STAGES,
        g_sos_20828_hz_q15.sos_q15,
        p_state->state_q15,
        DSP_BIQUAD_CASCADE_DF1_A_WEIGHTING_FILTER_COEFFICIENTS_20828_HZ_Q15_POST_SHIFT);
    arm_biquad_cascade_df1_q15_patched(&filter, p_in_buf, p_out_buf, num_samples);
}
