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

ZTEST_SUITE(test_suite_dsp_rms, NULL, &test_setup, &test_suite_before, &test_suite_after, &test_teardown);

typedef struct test_suite_dsp_rms_fixture
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

ZTEST_F(test_suite_dsp_rms, test_dsp_rms_freq_1000hz_amplitude_100)
{
    const float32_t amplitude        = 1.0f; // 100% of full scale
    const float32_t expected_rms_f32 = amplitude / sqrtf(2.0f);
    const float32_t freq_hz          = 1000.0f; // 1 kHz
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    float32_t rms_f32 = 0.0f;
    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &rms_f32);

    q15_t rms_q15_cmsis = 0;
    arm_rms_q15(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK, &rms_q15_cmsis);

    const float32_t rms_q15 = dsp_rms_q15_f32(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    const uint32_t expected_rms_f32_u32 = lrintf(expected_rms_f32 * 10000);
    ZASSERT_EQ_INT(7071, expected_rms_f32_u32);

    const uint32_t rms_f32_u32 = lrintf(rms_f32 * 10000);
    ZASSERT_EQ_INT(7071, rms_f32_u32);

    const uint32_t expected_rms_q15_raw = lrintf(expected_rms_f32 * MAX_Q15);
    ZASSERT_EQ_INT(23170, expected_rms_q15_raw);

    const uint32_t rms_q15_raw = lrintf(rms_q15);
    ZASSERT_EQ_INT(23170, rms_q15_raw);
    const uint32_t rms_q15_u32 = lrintf(rms_q15 * 10000.0f / MAX_Q15);
    ZASSERT_EQ_INT(7071, rms_q15_u32);

    ZASSERT_EQ_INT(23170, rms_q15_cmsis); // CMSIS-DSP arm_rms_q15 is 2 LSB less accurate than expected
    const uint32_t rms_q15_cmsis_u32 = ((uint32_t)rms_q15_cmsis * 10000 + MAX_Q15 / 2) / MAX_Q15;
    ZASSERT_EQ_INT(7071, rms_q15_cmsis_u32);
}

ZTEST_F(test_suite_dsp_rms, test_dsp_rms_freq_1000hz_amplitude_50)
{
    const float32_t amplitude        = 0.5f; // 50% of full scale
    const float32_t expected_rms_f32 = amplitude / sqrtf(2.0f);
    const float32_t freq_hz          = 1000.0f; // 1 kHz
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    float32_t rms_f32 = 0.0f;
    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &rms_f32);

    q15_t rms_q15_cmsis = 0;
    arm_rms_q15(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK, &rms_q15_cmsis);

    const float32_t rms_q15 = dsp_rms_q15_f32(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    const uint32_t expected_rms_f32_u32 = lrintf(expected_rms_f32 * 10000);
    ZASSERT_EQ_INT(3536, expected_rms_f32_u32);

    const uint32_t rms_f32_u32 = lrintf(rms_f32 * 10000);
    ZASSERT_EQ_INT(3536, rms_f32_u32);

    const uint32_t expected_rms_q15_raw = lrintf(expected_rms_f32 * MAX_Q15);
    ZASSERT_EQ_INT(11585, expected_rms_q15_raw);

    const uint32_t rms_q15_raw = lrintf(rms_q15);
    ZASSERT_EQ_INT(11585, rms_q15_raw);
    const uint32_t rms_q15_u32 = lrintf(rms_q15 * 10000.0f / MAX_Q15);
    ZASSERT_EQ_INT(3536, rms_q15_u32);

    ZASSERT_EQ_INT(11583, rms_q15_cmsis); // CMSIS-DSP arm_rms_q15 is 2 LSB less accurate than expected
    const uint32_t rms_q15_cmsis_u32 = ((uint32_t)rms_q15_cmsis * 10000 + MAX_Q15 / 2) / MAX_Q15;
    ZASSERT_EQ_INT(3535, rms_q15_cmsis_u32);
}

ZTEST_F(test_suite_dsp_rms, test_dsp_rms_freq_1000hz_amplitude_1)
{
    const float32_t amplitude        = 0.01f; // 1% of full scale
    const float32_t expected_rms_f32 = amplitude / sqrtf(2.0f);
    const float32_t freq_hz          = 1000.0f; // 1 kHz
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    float32_t rms_f32 = 0.0f;
    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &rms_f32);

    q15_t rms_q15_cmsis = 0;
    arm_rms_q15(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK, &rms_q15_cmsis);

    const float32_t rms_q15 = dsp_rms_q15_f32(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    const uint32_t expected_rms_f32_u32 = lrintf(expected_rms_f32 * 10000);
    ZASSERT_EQ_INT(71, expected_rms_f32_u32);

    const uint32_t rms_f32_u32 = lrintf(rms_f32 * 10000);
    ZASSERT_EQ_INT(71, rms_f32_u32);

    const uint32_t expected_rms_q15_raw = lrintf(expected_rms_f32 * MAX_Q15);
    ZASSERT_EQ_INT(232, expected_rms_q15_raw);

    const uint32_t rms_q15_raw = lrintf(rms_q15);
    ZASSERT_EQ_INT(232, rms_q15_raw);
    const uint32_t rms_q15_u32 = lrintf(rms_q15 * 10000.0f / MAX_Q15);
    ZASSERT_EQ_INT(71, rms_q15_u32);

    ZASSERT_EQ_INT(181, rms_q15_cmsis); // CMSIS-DSP arm_rms_q15 is 51 LSB less accurate than expected
    const uint32_t rms_q15_cmsis_u32 = ((uint32_t)rms_q15_cmsis * 10000 + MAX_Q15 / 2) / MAX_Q15;
    ZASSERT_EQ_INT(55, rms_q15_cmsis_u32);
}

ZTEST_F(test_suite_dsp_rms, test_dsp_rms_freq_1000hz_amplitude_0_5)
{
    const float32_t amplitude        = 0.005f; // 0.5% of full scale
    const float32_t expected_rms_f32 = amplitude / sqrtf(2.0f);
    const float32_t freq_hz          = 1000.0f; // 1 kHz
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    float32_t rms_f32 = 0.0f;
    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &rms_f32);

    q15_t rms_q15_cmsis = 0;
    arm_rms_q15(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK, &rms_q15_cmsis);

    const float32_t rms_q15 = dsp_rms_q15_f32(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    const uint32_t expected_rms_f32_u32 = lrintf(expected_rms_f32 * 10000);
    ZASSERT_EQ_INT(35, expected_rms_f32_u32);

    const uint32_t rms_f32_u32 = lrintf(rms_f32 * 10000);
    ZASSERT_EQ_INT(35, rms_f32_u32);

    const uint32_t expected_rms_q15_raw = lrintf(expected_rms_f32 * MAX_Q15);
    ZASSERT_EQ_INT(116, expected_rms_q15_raw);

    const uint32_t rms_q15_raw = lrintf(rms_q15);
    ZASSERT_EQ_INT(116, rms_q15_raw);
    const uint32_t rms_q15_u32 = lrintf(rms_q15 * 10000.0f / MAX_Q15);
    ZASSERT_EQ_INT(35, rms_q15_u32);

    ZASSERT_EQ_INT(0, rms_q15_cmsis); // CMSIS-DSP arm_rms_q15 is 116 LSB less accurate than expected
    const uint32_t rms_q15_cmsis_u32 = ((uint32_t)rms_q15_cmsis * 10000 + MAX_Q15 / 2) / MAX_Q15;
    ZASSERT_EQ_INT(0, rms_q15_cmsis_u32);
}

ZTEST_F(test_suite_dsp_rms, test_dsp_rms_freq_100hz_amplitude_50)
{
    const float32_t amplitude        = 0.5f; // 50% of full scale
    const float32_t expected_rms_f32 = amplitude / sqrtf(2.0f);
    const float32_t freq_hz          = 100.0f; // 100 Hz
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    float32_t rms_f32 = 0.0f;
    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &rms_f32);

    q15_t rms_q15_cmsis = 0;
    arm_rms_q15(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK, &rms_q15_cmsis);

    const float32_t rms_q15 = dsp_rms_q15_f32(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    const uint32_t expected_rms_f32_u32 = lrintf(expected_rms_f32 * 10000);
    ZASSERT_EQ_INT(3536, expected_rms_f32_u32);

    const uint32_t rms_f32_u32 = lrintf(rms_f32 * 10000);
    ZASSERT_EQ_INT(3535, rms_f32_u32);

    const uint32_t expected_rms_q15_raw = lrintf(expected_rms_f32 * MAX_Q15);
    ZASSERT_EQ_INT(11585, expected_rms_q15_raw);

    const uint32_t rms_q15_raw = lrintf(rms_q15);
    ZASSERT_EQ_INT(11585, rms_q15_raw);
    const uint32_t rms_q15_u32 = lrintf(rms_q15 * 10000.0f / MAX_Q15);
    ZASSERT_EQ_INT(3536, rms_q15_u32);

    ZASSERT_EQ_INT(11583, rms_q15_cmsis); // CMSIS-DSP arm_rms_q15 is 2 LSB less accurate than expected
    const uint32_t rms_q15_cmsis_u32 = ((uint32_t)rms_q15_cmsis * 10000 + MAX_Q15 / 2) / MAX_Q15;
    ZASSERT_EQ_INT(3535, rms_q15_cmsis_u32);
}

ZTEST_F(test_suite_dsp_rms, test_dsp_rms_freq_7990hz_amplitude_50)
{
    const float32_t amplitude        = 0.5f; // 50% of full scale
    const float32_t expected_rms_f32 = amplitude / sqrtf(2.0f);
    const float32_t freq_hz          = 7990.0f; // 8 kHz
    generate_sine_wave(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, amplitude, freq_hz, 0);
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    float32_t rms_f32 = 0.0f;
    arm_rms_f32(fixture->in_buf_f32, NUM_SAMPLES_PER_BLOCK, &rms_f32);

    q15_t rms_q15_cmsis = 0;
    arm_rms_q15(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK, &rms_q15_cmsis);

    const float32_t rms_q15 = dsp_rms_q15_f32(fixture->in_buf_q15, NUM_SAMPLES_PER_BLOCK);

    const uint32_t expected_rms_f32_u32 = lrintf(expected_rms_f32 * 10000);
    ZASSERT_EQ_INT(3536, expected_rms_f32_u32);

    const uint32_t rms_f32_u32 = lrintf(rms_f32 * 10000);
    ZASSERT_EQ_INT(3535, rms_f32_u32);

    const uint32_t expected_rms_q15_raw = lrintf(expected_rms_f32 * MAX_Q15);
    ZASSERT_EQ_INT(11585, expected_rms_q15_raw);

    const uint32_t rms_q15_raw = lrintf(rms_q15);
    ZASSERT_EQ_INT(11585, rms_q15_raw);
    const uint32_t rms_q15_u32 = lrintf(rms_q15 * 10000.0f / MAX_Q15);
    ZASSERT_EQ_INT(3535, rms_q15_u32);

    ZASSERT_EQ_INT(11583, rms_q15_cmsis); // CMSIS-DSP arm_rms_q15 is 2 LSB less accurate than expected
    const uint32_t rms_q15_cmsis_u32 = ((uint32_t)rms_q15_cmsis * 10000 + MAX_Q15 / 2) / MAX_Q15;
    ZASSERT_EQ_INT(3535, rms_q15_cmsis_u32);
}
