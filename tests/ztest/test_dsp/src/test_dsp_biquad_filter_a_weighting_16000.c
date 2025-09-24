/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dsp_rms.h"
#include "dsp_biquad_filter_a_weighting_16000.h"
#include "zassert.h"

#define MAX_Q15 (32767)

#define SAMPLE_RATE           16000
#define BLOCK_DURATION_MS     (100)
#define NUM_BLOCKS_PER_SECOND (1000 / BLOCK_DURATION_MS)
#define NUM_SAMPLES_PER_BLOCK (SAMPLE_RATE / NUM_BLOCKS_PER_SECOND)

static void*
test_setup(void);

static void
test_suite_before(void* f);

static void
test_suite_after(void* f);

static void
test_teardown(void* f);

ZTEST_SUITE(
    test_suite_dsp_biquad_filter_a_weighting,
    NULL,
    &test_setup,
    &test_suite_before,
    &test_suite_after,
    &test_teardown);

typedef struct test_suite_dsp_biquad_filter_a_weighting_fixture
{
    float32_t in_buf_f32[NUM_SAMPLES_PER_BLOCK];
    q15_t     in_buf_q15[NUM_SAMPLES_PER_BLOCK];
    float32_t out_buf_f32[NUM_SAMPLES_PER_BLOCK];
    q15_t     out_buf_q15[NUM_SAMPLES_PER_BLOCK];
} test_suite_fixture_t;

static void*
test_setup(void)
{
    test_suite_fixture_t* p_fixture = calloc(1, sizeof(*p_fixture));
    assert(NULL != p_fixture);
    return p_fixture;
}

static void
test_suite_before(void* f)
{
    test_suite_fixture_t* p_fixture = f;
    memset(p_fixture, 0, sizeof(*p_fixture));
}

static void
test_suite_after(void* f)
{
}

static void
test_teardown(void* f)
{
    if (NULL != f)
    {
        free(f);
    }
}

static void
generate_sine_wave(
    float32_t* const p_buffer,
    const uint32_t   buffer_size,
    const float32_t  amplitude,
    const float32_t  frequency,
    const float32_t  phase)
{
    for (uint32_t i = 0; i < buffer_size; i++)
    {
        p_buffer[i] = amplitude * arm_sin_f32(2 * PI * (float32_t)frequency * (float32_t)i / SAMPLE_RATE + phase);
    }
}

static void
convert_float_to_q15(const float32_t* const p_float_buffer, q15_t* const p_q15_buffer, const uint32_t num_samples)
{
    for (uint32_t i = 0; i < num_samples; i++)
    {
        p_q15_buffer[i] = (q15_t)lrintf(p_float_buffer[i] * MAX_Q15);
    }
}

typedef struct filter_a_weighting_result_t
{
    float32_t rms_f32_unfiltered;
    float32_t rms_f32_filtered;
    float32_t rms_q15_filtered_cmsis; // calculated by CMSIS-DSP
    float32_t rms_q15_filtered_patched;
    float32_t deviation_rms_q15_filtered_cmsis;
    float32_t deviation_rms_q15_filtered_patched;
} filter_a_weighting_result_t;

static filter_a_weighting_result_t
apply_filter_a_weighting(test_suite_fixture_t* const fixture)
{
    filter_a_weighting_result_t res = { 0 };

    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &res.rms_f32_unfiltered);

    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t state_f32 = { 0 };

    dsp_biquad_filter_a_weighting_16000_f32(
        &state_f32,
        fixture->in_buf_f32,
        fixture->out_buf_f32,
        NUM_SAMPLES_PER_BLOCK);
    arm_rms_f32(fixture->out_buf_f32, NUM_SAMPLES_PER_BLOCK, &res.rms_f32_filtered);

    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t state_q15_cmsis = { 0 };

    dsp_biquad_filter_a_weighting_16000_q15_cmsis(
        &state_q15_cmsis,
        fixture->in_buf_q15,
        fixture->out_buf_q15,
        NUM_SAMPLES_PER_BLOCK);
    res.rms_q15_filtered_cmsis           = dsp_rms_q15_f32(fixture->out_buf_q15, NUM_SAMPLES_PER_BLOCK);
    res.deviation_rms_q15_filtered_cmsis = fabsf((res.rms_q15_filtered_cmsis / MAX_Q15 - res.rms_f32_filtered))
                                           / res.rms_f32_filtered * 100.0f;

    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t state_q15 = { 0 };

    dsp_biquad_filter_a_weighting_16000_q15(
        &state_q15,
        fixture->in_buf_q15,
        fixture->out_buf_q15,
        NUM_SAMPLES_PER_BLOCK);
    res.rms_q15_filtered_patched           = dsp_rms_q15_f32(fixture->out_buf_q15, NUM_SAMPLES_PER_BLOCK);
    res.deviation_rms_q15_filtered_patched = fabsf((res.rms_q15_filtered_patched / MAX_Q15 - res.rms_f32_filtered))
                                             / res.rms_f32_filtered * 100.0f;

    return res;
}

static void
print_filter_a_weighting_result(const float32_t amplitude, const uint32_t freq, const filter_a_weighting_result_t res)
{
    const float rms_unfiltered = amplitude / sqrtf(2.0f);
    printf("rms_f32[%d Hz] unfiltered = %f\n", freq, (double)rms_unfiltered);
    printf(
        "rms_f32[%d Hz] filtered = %f, gain=%f\n",
        freq,
        (double)res.rms_f32_filtered,
        (double)(res.rms_f32_filtered / rms_unfiltered));
    printf(
        "rms_q15[%d Hz] filtered (cmsis) = %f, gain=%f, deviation=%.04f%%\n",
        freq,
        (double)(res.rms_q15_filtered_cmsis / MAX_Q15),
        (double)(res.rms_q15_filtered_cmsis / MAX_Q15 / rms_unfiltered),
        (double)res.deviation_rms_q15_filtered_cmsis);
    printf(
        "rms_q15[%d Hz] filtered (patched) = %f, gain=%f, deviation=%.04f%%\n",
        freq,
        (double)res.rms_q15_filtered_patched / MAX_Q15,
        (double)(res.rms_q15_filtered_patched / MAX_Q15 / rms_unfiltered),
        (double)res.deviation_rms_q15_filtered_patched);
    printf("\n");
}

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_freq_1000_hz)
{
    const float32_t amplitude = 0.5f;
    const uint32_t  freq_hz   = 1000;
    printf("Generate sine wave %d Hz with amplitude %.03f\n", freq_hz, (double)amplitude);
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    const filter_a_weighting_result_t res = apply_filter_a_weighting(fixture);
    print_filter_a_weighting_result(amplitude, freq_hz, res);
    const float rms_unfiltered = amplitude / sqrtf(2.0f);
    ZASSERT_EQ_INT(354, lrintf(rms_unfiltered * 1000));
    ZASSERT_EQ_INT(355, lrintf(res.rms_f32_filtered * 1000));
    ZASSERT_EQ_INT(355, lrintf(res.rms_q15_filtered_patched * 1000 / MAX_Q15));
    // CMSIS-DSP arm_biquad_cascade_df1_q15 is less accurate than expected
    ZASSERT_EQ_INT(422, lrintf(res.rms_q15_filtered_cmsis * 1000 / MAX_Q15));
}

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_freq_100_hz)
{
    const float32_t amplitude = 0.5f;
    const uint32_t  freq_hz   = 100;
    printf("Generate sine wave %d Hz with amplitude %.03f\n", freq_hz, (double)amplitude);
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    const filter_a_weighting_result_t res = apply_filter_a_weighting(fixture);
    print_filter_a_weighting_result(amplitude, freq_hz, res);
    const float rms_unfiltered = amplitude / sqrtf(2.0f);
    ZASSERT_EQ_INT(354, lrintf(rms_unfiltered * 1000));
    ZASSERT_EQ_INT(39, lrintf(res.rms_f32_filtered * 1000));
    ZASSERT_EQ_INT(38, lrintf(res.rms_q15_filtered_patched * 1000 / MAX_Q15));
    // CMSIS-DSP arm_biquad_cascade_df1_q15 is less accurate than expected
    ZASSERT_EQ_INT(225, lrintf(res.rms_q15_filtered_cmsis * 1000 / MAX_Q15));
}

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_freq_4000_hz)
{
    const float32_t amplitude = 0.5f;
    const uint32_t  freq_hz   = 4000;
    printf("Generate sine wave %d Hz with amplitude %.03f\n", freq_hz, (double)amplitude);
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    const filter_a_weighting_result_t res = apply_filter_a_weighting(fixture);
    print_filter_a_weighting_result(amplitude, freq_hz, res);
    const float rms_unfiltered = amplitude / sqrtf(2.0f);
    ZASSERT_EQ_INT(354, lrintf(rms_unfiltered * 1000));
    ZASSERT_EQ_INT(375, lrintf(res.rms_f32_filtered * 1000));
    ZASSERT_EQ_INT(375, lrintf(res.rms_q15_filtered_patched * 1000 / MAX_Q15));
    // CMSIS-DSP arm_biquad_cascade_df1_q15 is less accurate than expected
    ZASSERT_EQ_INT(461, lrintf(res.rms_q15_filtered_cmsis * 1000 / MAX_Q15));
}

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_freq_6000_hz)
{
    const float32_t amplitude = 0.5f;
    const uint32_t  freq_hz   = 6000;
    printf("Generate sine wave %d Hz with amplitude %.03f\n", freq_hz, (double)amplitude);
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    const filter_a_weighting_result_t res = apply_filter_a_weighting(fixture);
    print_filter_a_weighting_result(amplitude, freq_hz, res);
    const float rms_unfiltered = amplitude / sqrtf(2.0f);
    ZASSERT_EQ_INT(354, lrintf(rms_unfiltered * 1000));
    ZASSERT_EQ_INT(220, lrintf(res.rms_f32_filtered * 1000));
    ZASSERT_EQ_INT(220, lrintf(res.rms_q15_filtered_patched * 1000 / MAX_Q15));
    // CMSIS-DSP arm_biquad_cascade_df1_q15 is less accurate than expected
    ZASSERT_EQ_INT(332, lrintf(res.rms_q15_filtered_cmsis * 1000 / MAX_Q15));
}

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_freq_7900_hz)
{
    const float32_t amplitude = 0.5f;
    const uint32_t  freq_hz   = 7990;
    printf("Generate sine wave %d Hz with amplitude %.03f\n", freq_hz, (double)amplitude);
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    const filter_a_weighting_result_t res = apply_filter_a_weighting(fixture);
    print_filter_a_weighting_result(amplitude, freq_hz, res);
    const float rms_unfiltered = amplitude / sqrtf(2.0f);
    ZASSERT_EQ_INT(354, lrintf(rms_unfiltered * 1000));
    ZASSERT_EQ_INT(0, lrintf(res.rms_f32_filtered * 1000));
    ZASSERT_EQ_INT(0, lrintf(res.rms_q15_filtered_patched * 1000 / MAX_Q15));
    // CMSIS-DSP arm_biquad_cascade_df1_q15 is less accurate than expected
    ZASSERT_EQ_INT(132, lrintf(res.rms_q15_filtered_cmsis * 1000 / MAX_Q15));
}

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_freq_10000_hz)
{
    const float32_t amplitude = 0.5f;
    const uint32_t  freq_hz   = 10000;
    printf("Generate sine wave %d Hz with amplitude %.03f\n", freq_hz, (double)amplitude);
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    const filter_a_weighting_result_t res = apply_filter_a_weighting(fixture);
    print_filter_a_weighting_result(amplitude, freq_hz, res);
    const float rms_unfiltered = amplitude / sqrtf(2.0f);
    ZASSERT_EQ_INT(354, lrintf(rms_unfiltered * 1000));
    ZASSERT_EQ_INT(220, lrintf(res.rms_f32_filtered * 1000));
    ZASSERT_EQ_INT(220, lrintf(res.rms_q15_filtered_patched * 1000 / MAX_Q15));
    // CMSIS-DSP arm_biquad_cascade_df1_q15 is less accurate than expected
    ZASSERT_EQ_INT(327, lrintf(res.rms_q15_filtered_cmsis * 1000 / MAX_Q15));
}

#if 0

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_3)
{
    const float    amplitude  = 0.5f;
    const uint32_t num_points = 200;
    float* const   arr_x      = malloc(num_points * sizeof(float));
    zassert_not_null(arr_x);
    float* const rms_unfiltered = malloc(num_points * sizeof(float));
    zassert_not_null(rms_unfiltered);
    float* const rms_f32_filtered = malloc(num_points * sizeof(float));
    zassert_not_null(rms_f32_filtered);
    float* const rms_q15_filtered_cmsis = malloc(num_points * sizeof(float));
    zassert_not_null(rms_q15_filtered_cmsis);
    float* const rms_q15_filtered_patched = malloc(num_points * sizeof(float));
    zassert_not_null(rms_q15_filtered_patched);

    FILE* fp = fopen("rms_q15_filtered.csv", "w");
    zassert_not_null(fp);
    fprintf(fp, "freq,rms_unfiltered,rms_f32_filtered,rms_q15_filtered_cmsis,rms_q15_filtered_patched\n");

    // Define the range for logarithmic spacing
    const float freq_start = 10;    // 10 Hz
    const float freq_end   = 22000; // 22 kHz
    // Compute evenly spaced logarithmic points
    for (int i = 0; i < num_points; ++i)
    {
        float fraction = (float)i / (float)(num_points - 1);
        arr_x[i]       = freq_start * powf(freq_end / freq_start, fraction);

        rms_unfiltered[i] = amplitude / sqrtf(2.0f);

        memset(g_in_buf_f32, 0, sizeof(g_in_buf_f32));
        generate_sine_wave(g_in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, arr_x[i], 0);
        const filter_a_weighting_result_t res = apply_filter_a_weighting(g_in_buf_f32);
        rms_f32_filtered[i]                   = res.rms_f32_filtered;
        rms_q15_filtered_cmsis[i]             = res.rms_q15_filtered_cmsis;
        rms_q15_filtered_patched[i]           = res.rms_q15_filtered_patched;

        fprintf(
            fp,
            "%f,%f,%f,%f,%f\n",
            (double)arr_x[i],
            (double)rms_unfiltered[i],
            (double)rms_f32_filtered[i],
            (double)rms_q15_filtered_cmsis[i],
            (double)rms_q15_filtered_patched[i]);
    }
}

ZTEST_F(test_suite_dsp_biquad_filter_a_weighting, test_4)
{
    const float amplitude = 0.1f;
    const uint32_t num_points = 200;
    float* const arr_x = malloc(num_points * sizeof(float));
    zassert_not_null(arr_x);
    float* const arr_y_rms_expected = malloc(num_points * sizeof(float));
    zassert_not_null(arr_y_rms_expected);
    float* const arr_y_rms_q15_min = malloc(num_points * sizeof(float));
    zassert_not_null(arr_y_rms_q15_min);
    float* const arr_y_rms_q15_max = malloc(num_points * sizeof(float));
    zassert_not_null(arr_y_rms_q15_max);
    float* const arr_y_rms_f32_min = malloc(num_points * sizeof(float));
    zassert_not_null(arr_y_rms_f32_min);
    float* const arr_y_rms_f32_max = malloc(num_points * sizeof(float));
    zassert_not_null(arr_y_rms_f32_max);

    FILE *fp = fopen("freq_rms.csv", "w");
    zassert_not_null(fp);
    fprintf(fp, "freq,rms_expected,rms_q15_min,rms_q15_max,rms_f32_min,rms_f32_max\n");

    // Define the range for logarithmic spacing
    const float freq_start = 10; // 10 Hz
    const float freq_end = 22000; // 22 kHz
    // Compute evenly spaced logarithmic points
    for (int i = 0; i < num_points; ++i) {
        float fraction = (float)i / (float)(num_points - 1);
        arr_x[i] = freq_start * powf(freq_end / freq_start, fraction);

        arr_y_rms_expected[i] = amplitude / sqrtf(2.0f);

        const uint32_t phase_num_steps = 180;
        arr_y_rms_q15_min[i] = 1.0f;
        arr_y_rms_q15_max[i] = 0.0f;
        arr_y_rms_f32_min[i] = 1.0f;
        arr_y_rms_f32_max[i] = 0.0f;
        for (int phase = 0; phase < phase_num_steps; ++phase)
        {
            memset(g_in_buf_f32, 0, sizeof(g_in_buf_f32));
            add_sine_wave(g_in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, arr_x[i], (float)phase / (float)phase_num_steps * 2 * PI);
            const biquad_cascade_df1_result_t res = do_biquad_cascade_df1(g_in_buf_f32);
            if (arr_y_rms_q15_min[i] > res.rms_q15)
            {
                arr_y_rms_q15_min[i] = res.rms_q15;
            }
            if (arr_y_rms_q15_max[i] < res.rms_q15)
            {
                arr_y_rms_q15_max[i] = res.rms_q15;
            }
            if (arr_y_rms_f32_min[i] > res.rms_f32)
            {
                arr_y_rms_f32_min[i] = res.rms_f32;
            }
            if (arr_y_rms_f32_max[i] < res.rms_f32)
            {
                arr_y_rms_f32_max[i] = res.rms_f32;
            }
        }
        fprintf(
            fp,
            "%f,%f,%f,%f,%f,%f\n",
            (double)arr_x[i],
            (double)arr_y_rms_expected[i],
            (double)arr_y_rms_q15_min[i],
            (double)arr_y_rms_q15_max[i],
            (double)arr_y_rms_f32_min[i],
            (double)arr_y_rms_f32_max[i]);
    }
}
#endif
