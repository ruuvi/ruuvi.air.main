/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <arm_math.h>
#include "zassert.h"
#include "spl_calc.h"
#include "mic_pdm.h"

#define MAX_Q15 (32767)

static void*
test_setup(void);

static void
test_suite_before(void* f);

static void
test_suite_after(void* f);

static void
test_teardown(void* f);

ZTEST_SUITE(test_suite_spl_calc, NULL, &test_setup, &test_suite_before, &test_suite_after, &test_teardown);

typedef struct test_suite_spl_calc_fixture
{
    float32_t in_buf_f32[MIC_PDM_NUM_SAMPLES_IN_BLOCK];
    q15_t     in_buf_q15[MIC_PDM_NUM_SAMPLES_IN_BLOCK];
    // float32_t out_buf_f32[MIC_PDM_NUM_SAMPLES_IN_BLOCK];
    // q15_t     out_buf_q15[MIC_PDM_NUM_SAMPLES_IN_BLOCK];
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
    spl_calc_init();
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
    const float32_t  phase,
    const bool       flag_add)
{
    for (uint32_t i = 0; i < buffer_size; i++)
    {
        const float32_t x = amplitude
                            * arm_sin_f32(2 * PI * (float32_t)frequency * (float32_t)i / MIC_PDM_SAMPLE_RATE + phase);
        if (flag_add)
        {
            p_buffer[i] += x;
        }
        else
        {
            p_buffer[i] = x;
        }
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

ZTEST_F(test_suite_spl_calc, test_1)
{
    static float32_t buf_f32[MIC_PDM_NUM_SAMPLES_IN_BLOCK];
    // Generate a sine wave with amplitude 0.027 (regular voice) and frequency 1000 Hz
    generate_sine_wave(fixture->in_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK, 0.027f, 1000, 0, false);
    for (int i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND - 1; ++i)
    {
        memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
        zassert_false(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    }
    memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
    zassert_true(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    ZASSERT_EQ_FLOAT4(0.019095, spl_calc_get_rms_last_max());
    ZASSERT_EQ_FLOAT4(0.019144, spl_calc_get_rms_last_avg()); // at 1 kHz, A-weighting should not affect the result
    ZASSERT_EQ_FLOAT4(0.019095, spl_calc_get_rms_max());
    ZASSERT_EQ_FLOAT4(0.019144, spl_calc_get_rms_avg());

    // Add a sine wave with amplitude 0.05 and frequency 100 Hz
    generate_sine_wave(fixture->in_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK, 0.05f, 100, 0, true);
    for (int i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND - 1; ++i)
    {
        memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
        zassert_false(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    }
    memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
    zassert_true(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    ZASSERT_EQ_FLOAT4(0.040179, spl_calc_get_rms_last_max());
    ZASSERT_EQ_FLOAT4(0.019506, spl_calc_get_rms_last_avg());
    ZASSERT_EQ_FLOAT4(0.040179, spl_calc_get_rms_max());
    ZASSERT_EQ_FLOAT4(0.019323, spl_calc_get_rms_avg());

    // Add a sine wave with amplitude 0.04 and frequency 7900 Hz
    generate_sine_wave(fixture->in_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK, 0.04f, 7900, 0, true);
    for (int i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND - 1; ++i)
    {
        memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
        zassert_false(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    }
    memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
    zassert_true(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    ZASSERT_EQ_FLOAT4(0.049136, spl_calc_get_rms_last_max());
    ZASSERT_EQ_FLOAT4(0.019506, spl_calc_get_rms_last_avg());
    ZASSERT_EQ_FLOAT4(0.049136, spl_calc_get_rms_max());
    ZASSERT_EQ_FLOAT4(0.019384, spl_calc_get_rms_avg());

    // Remove low and high frequency components
    generate_sine_wave(fixture->in_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK, 0.027f, 1000, 0, false);
    for (int j = 0; j < 57; ++j)
    {
        for (int i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND - 1; ++i)
        {
            memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
            convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
            zassert_false(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
        }
        memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
        zassert_true(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    }
    ZASSERT_EQ_FLOAT4(0.019095, spl_calc_get_rms_last_max());
    ZASSERT_EQ_FLOAT4(0.019140, spl_calc_get_rms_last_avg()); // at 1 kHz, A-weighting should not affect the result
    ZASSERT_EQ_FLOAT4(0.049136, spl_calc_get_rms_max());
    ZASSERT_EQ_FLOAT4(0.019152, spl_calc_get_rms_avg());

    // Displacement of the first element of the ring buffer (without low and high frequency components)
    generate_sine_wave(fixture->in_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK, 0.027f, 1000, 0, false);
    for (int i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND - 1; ++i)
    {
        memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
        zassert_false(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    }
    memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
    zassert_true(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    ZASSERT_EQ_FLOAT4(0.019095, spl_calc_get_rms_last_max());
    ZASSERT_EQ_FLOAT4(0.019140, spl_calc_get_rms_last_avg()); // at 1 kHz, A-weighting should not affect the result
    ZASSERT_EQ_FLOAT4(0.049136, spl_calc_get_rms_max());
    ZASSERT_EQ_FLOAT4(0.019152, spl_calc_get_rms_avg());

    // Displacement of the 2nd element of the ring buffer (with low frequency components)
    generate_sine_wave(fixture->in_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK, 0.027f, 1000, 0, false);
    for (int i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND - 1; ++i)
    {
        memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
        zassert_false(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    }
    memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
    zassert_true(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    ZASSERT_EQ_FLOAT4(0.019095, spl_calc_get_rms_last_max());
    ZASSERT_EQ_FLOAT4(0.019140, spl_calc_get_rms_last_avg()); // at 1 kHz, A-weighting should not affect the result
    ZASSERT_EQ_FLOAT4(0.049136, spl_calc_get_rms_max());
    ZASSERT_EQ_FLOAT4(0.019146, spl_calc_get_rms_avg());

    // Displacement of the 3rd element of the ring buffer (with low and high frequency components)
    generate_sine_wave(fixture->in_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK, 0.027f, 1000, 0, false);
    for (int i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND - 1; ++i)
    {
        memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
        convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
        zassert_false(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    }
    memcpy(buf_f32, fixture->in_buf_f32, sizeof(buf_f32));
    convert_float_to_q15(fixture->in_buf_f32, fixture->in_buf_q15, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
    zassert_true(spl_calc_handle_buffer(fixture->in_buf_q15, buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK));
    ZASSERT_EQ_FLOAT4(0.019095, spl_calc_get_rms_last_max());
    ZASSERT_EQ_FLOAT4(0.019140, spl_calc_get_rms_last_avg()); // at 1 kHz, A-weighting should not affect the result
    ZASSERT_EQ_FLOAT4(0.019096, spl_calc_get_rms_max());
    ZASSERT_EQ_FLOAT4(0.019140, spl_calc_get_rms_avg());
}
