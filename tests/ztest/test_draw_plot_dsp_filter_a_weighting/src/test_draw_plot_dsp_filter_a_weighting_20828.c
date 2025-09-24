/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dsp_biquad_filter_a_weighting_20828.h"
#include "dsp_rms.h"
#include "zassert.h"
#include "helpers.h"

#define NUM_PLOT_POINTS (200)

#define SAMPLE_RATE           20828
#define BLOCK_DURATION_MS     (20)
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
    test_suite_draw_plot_dsp_filter_a_weighting_20828,
    NULL,
    &test_setup,
    &test_suite_before,
    &test_suite_after,
    &test_teardown);

typedef struct test_suite_draw_plot_dsp_filter_a_weighting_20828_fixture
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

static filter_a_weighting_result_t
apply_filter_a_weighting(test_suite_fixture_t* const fixture)
{
    filter_a_weighting_result_t res = { 0 };

    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &res.rms_f32_unfiltered);

    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t state_f32 = { 0 };

    dsp_biquad_filter_a_weighting_20828_f32(
        &state_f32,
        fixture->in_buf_f32,
        fixture->out_buf_f32,
        NUM_SAMPLES_PER_BLOCK);
    arm_rms_f32(fixture->out_buf_f32, NUM_SAMPLES_PER_BLOCK, &res.rms_f32_filtered);

    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t state_q15_cmsis = { 0 };

    dsp_biquad_filter_a_weighting_20828_q15_cmsis(
        &state_q15_cmsis,
        fixture->in_buf_q15,
        fixture->out_buf_q15,
        NUM_SAMPLES_PER_BLOCK);
    res.rms_q15_filtered_cmsis = dsp_rms_q15_f32(fixture->out_buf_q15, NUM_SAMPLES_PER_BLOCK) / MAX_Q15;

    dsp_biquad_cascade_df1_a_weighting_filter_state_q15_t state_q15 = { 0 };

    dsp_biquad_filter_a_weighting_20828_q15(
        &state_q15,
        fixture->in_buf_q15,
        fixture->out_buf_q15,
        NUM_SAMPLES_PER_BLOCK);
    res.rms_q15_filtered_patched = dsp_rms_q15_f32(fixture->out_buf_q15, NUM_SAMPLES_PER_BLOCK) / MAX_Q15;

    return res;
}

ZTEST_F(test_suite_draw_plot_dsp_filter_a_weighting_20828, test_draw_plot)
{
    const float    amplitude  = 0.5f;
    const uint32_t num_points = 200;

    const char* filename = "result_20828.csv";
    printf("Create file %s\n", filename);
    FILE* fp = fopen(filename, "w");
    zassert_not_null(fp);
    fprintf(fp, "freq,rms_unfiltered,rms_f32_filtered,rms_q15_filtered_cmsis,rms_q15_filtered_patched\n");

    // Define the range for logarithmic spacing
    const float freq_start = 10;    // 10 Hz
    const float freq_end   = 22000; // 22 kHz
    // Compute evenly spaced logarithmic points
    for (int i = 0; i < num_points; ++i)
    {
        float       fraction = (float)i / (float)(num_points - 1);
        const float freq     = freq_start * powf(freq_end / freq_start, fraction);

        const float rms_unfiltered = amplitude / sqrtf(2.0f);

        generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq, 0, SAMPLE_RATE);
        const filter_a_weighting_result_t res = apply_filter_a_weighting(fixture);

        fprintf(
            fp,
            "%f,%f,%f,%f,%f\n",
            (double)freq,
            (double)rms_unfiltered,
            (double)res.rms_f32_filtered,
            (double)res.rms_q15_filtered_cmsis,
            (double)res.rms_q15_filtered_patched);
    }
}
