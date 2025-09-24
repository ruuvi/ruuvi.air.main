/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dsp_rms.h"
#include "dsp/statistics_functions.h"
#include "zassert.h"

#define PLOT_FILE_NAME "result.csv"

#define NUM_PLOT_POINTS (200)

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

ZTEST_SUITE(test_suite_draw_plot_dsp_rms, NULL, &test_setup, &test_suite_before, &test_suite_after, &test_teardown);

typedef struct test_suite_draw_plot_dsp_rms_fixture
{
    float32_t in_buf_f32[NUM_SAMPLES_PER_BLOCK];
    q15_t     in_buf_q15[NUM_SAMPLES_PER_BLOCK];
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

ZTEST_F(test_suite_draw_plot_dsp_rms, test_draw_plot_dsp_rms)
{
    const uint32_t freq_hz = 1000;

    printf("Create file %s\n", PLOT_FILE_NAME);
    FILE* fp = fopen(PLOT_FILE_NAME, "w");
    zassert_not_null(fp);
    fprintf(fp, "amplitude,rms_expected,rms_f32,rms_q15,rms_q15_cmsis\n");

    // Define the range for logarithmic spacing
    const float amplitude_start = 1.0f / (float)MAX_Q15;
    const float amplitude_end   = 1.0f;

    // Compute evenly spaced logarithmic points
    for (int i = 0; i < NUM_PLOT_POINTS; ++i)
    {
        float       fraction  = (float)i / (float)(NUM_PLOT_POINTS - 1);
        const float amplitude = amplitude_start * powf(amplitude_end / amplitude_start, fraction);

        const float rms_expected = amplitude / sqrtf(2.0f);
        generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

        float32_t rms_f32 = 0.0f;
        arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &rms_f32);

        const float32_t rms_q15_f32 = dsp_rms_q15_f32(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK) / MAX_Q15;

        q15_t rms_q15_cmsis = 0;
        arm_rms_q15(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK, &rms_q15_cmsis);
        const float32_t rms_q15_cmsis_f32 = (float32_t)rms_q15_cmsis / MAX_Q15;

        fprintf(
            fp,
            "%f,%f,%f,%f,%f\n",
            (double)amplitude,
            (double)rms_expected,
            (double)rms_f32,
            (double)rms_q15_f32,
            (double)rms_q15_cmsis_f32);
    }
    fclose(fp);
}
