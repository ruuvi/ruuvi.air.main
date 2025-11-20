/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "spl_calc.h"
#include <assert.h>
#include "mic_pdm.h"
#include "dsp_rms.h"
#include "tlog.h"
#include "dsp/filtering_functions.h"
#if CONFIG_RUUVI_AIR_MIC_PDM_SAMPLE_RATE == 16000
#include "dsp_biquad_filter_a_weighting_16000.h"
#elif CONFIG_RUUVI_AIR_MIC_PDM_SAMPLE_RATE == 20828
#include "dsp_biquad_filter_a_weighting_20828.h"
#else
#error "Unsupported sample rate"
#endif

LOG_MODULE_REGISTER(spl_calc, LOG_LEVEL_INF);

#define SPL_CALC_AVERAGING_PERIOD_SEC (60)

#define MAX_Q15   (32767)
#define MAX_Q15_F (32767.0f)

typedef struct accum_rms_t
{
    float32_t arr_sum_of_square[MIC_PDM_NUM_BLOCKS_PER_SECOND];
    uint16_t  cnt;
} accum_rms_t;

typedef struct moving_window_rms_t
{
    float32_t arr_rms[SPL_CALC_AVERAGING_PERIOD_SEC];
    uint16_t  idx;
    uint16_t  cnt;
} moving_window_rms_t;

typedef struct moving_window_mean_t
{
    q31_t    arr_of_sums[MIC_PDM_MEAN_MOVING_AVG_WINDOW_SIZE];
    uint16_t idx;
    uint16_t cnt;
} moving_window_mean_t;

static accum_rms_t          g_accum_rms_unfiltered;
static accum_rms_t          g_accum_rms_filtered;
static moving_window_rms_t  g_moving_max_rms;
static moving_window_rms_t  g_moving_avg_rms;
static moving_window_mean_t g_moving_mean;

static dsp_biquad_cascade_df1_a_weighting_filter_state_f32_t g_weighting_filter_state_f32 = { 0 };

void
spl_calc_init(void)
{
    memset(&g_accum_rms_unfiltered, 0, sizeof(g_accum_rms_unfiltered));
    g_accum_rms_unfiltered.cnt = 0;

    memset(&g_accum_rms_filtered, 0, sizeof(g_accum_rms_filtered));
    g_accum_rms_filtered.cnt = 0;

    memset(&g_moving_max_rms, 0, sizeof(g_moving_max_rms));
    g_moving_max_rms.idx = 0;
    g_moving_max_rms.cnt = 0;

    memset(&g_moving_avg_rms, 0, sizeof(g_moving_avg_rms));
    g_moving_avg_rms.idx = 0;
    g_moving_avg_rms.cnt = 0;

    memset(&g_moving_mean, 0, sizeof(g_moving_mean));
    g_moving_mean.idx = 0;
    g_moving_mean.cnt = 0;

    memset(&g_weighting_filter_state_f32, 0, sizeof(g_weighting_filter_state_f32));
}

static bool
accum_rms_add(accum_rms_t* p_accum_rms, const float32_t sum_of_squares)
{
    p_accum_rms->arr_sum_of_square[p_accum_rms->cnt] = sum_of_squares;
    p_accum_rms->cnt += 1;
    if (p_accum_rms->cnt >= MIC_PDM_NUM_BLOCKS_PER_SECOND)
    {
        p_accum_rms->cnt = 0;
        return true;
    }
    return false;
}

static float32_t
accum_rms_get_max(const accum_rms_t* const p_accum_rms)
{
    assert(p_accum_rms->cnt == 0);
    float32_t max = 0;
    for (uint32_t i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND; ++i)
    {
        if (p_accum_rms->arr_sum_of_square[i] > max)
        {
            max = p_accum_rms->arr_sum_of_square[i];
        }
    }
    return sqrtf(max / (float32_t)MIC_PDM_NUM_SAMPLES_IN_BLOCK) / MAX_Q15_F;
}

static float32_t
accum_rms_get_avg(const accum_rms_t* const p_accum_rms)
{
    assert(p_accum_rms->cnt == 0);
    float32_t sum = 0;
    for (uint32_t i = 0; i < MIC_PDM_NUM_BLOCKS_PER_SECOND; ++i)
    {
        sum += p_accum_rms->arr_sum_of_square[i];
    }
    return sqrtf(sum / (MIC_PDM_NUM_SAMPLES_IN_BLOCK * MIC_PDM_NUM_BLOCKS_PER_SECOND)) / MAX_Q15_F;
}

static q15_t
moving_window_mean_add(
    moving_window_mean_t* p_moving_window_mean,
    const q15_t* const    p_buffer,
    const uint16_t        num_samples)
{
    const q31_t sum_of_vals_in_buf = dsp_calc_sum_q15_q31(p_buffer, num_samples);

    p_moving_window_mean->arr_of_sums[p_moving_window_mean->idx] = sum_of_vals_in_buf;
    p_moving_window_mean->idx += 1;
    if (p_moving_window_mean->idx >= MIC_PDM_MEAN_MOVING_AVG_WINDOW_SIZE)
    {
        p_moving_window_mean->idx = 0;
    }
    if (p_moving_window_mean->cnt < MIC_PDM_MEAN_MOVING_AVG_WINDOW_SIZE)
    {
        p_moving_window_mean->cnt += 1;
    }
    assert(p_moving_window_mean->cnt > 0);
    assert(p_moving_window_mean->cnt <= UINT16_MAX);
    q63_t sum = 0;
    for (uint32_t i = 0; i < p_moving_window_mean->cnt; ++i)
    {
        sum += p_moving_window_mean->arr_of_sums[i];
    }
    return (q15_t)(sum / (p_moving_window_mean->cnt * MIC_PDM_NUM_SAMPLES_IN_BLOCK));
}

static void
moving_window_rms_add(moving_window_rms_t* p_moving_window_rms, const float32_t rms)
{
    p_moving_window_rms->arr_rms[p_moving_window_rms->idx] = rms;
    p_moving_window_rms->idx += 1;
    if (p_moving_window_rms->idx >= SPL_CALC_AVERAGING_PERIOD_SEC)
    {
        p_moving_window_rms->idx = 0;
    }
    if (p_moving_window_rms->cnt < SPL_CALC_AVERAGING_PERIOD_SEC)
    {
        p_moving_window_rms->cnt += 1;
    }
}

static float32_t
moving_window_rms_get_max(const moving_window_rms_t* const p_moving_window_rms)
{
    float32_t max = 0;
    uint32_t  idx = p_moving_window_rms->idx;
    uint32_t  cnt = p_moving_window_rms->cnt;
    if (0 == cnt)
    {
        return NAN;
    }
    while (cnt > 0)
    {
        cnt -= 1;
        if (0 == idx)
        {
            idx = SPL_CALC_AVERAGING_PERIOD_SEC - 1;
        }
        else
        {
            idx -= 1;
        }
        if (p_moving_window_rms->arr_rms[idx] > max)
        {
            max = p_moving_window_rms->arr_rms[idx];
        }
    }
    return max;
}

static float32_t
moving_window_rms_get_avg(const moving_window_rms_t* const p_moving_window_rms)
{
    float32_t sum = 0;
    uint32_t  idx = p_moving_window_rms->idx;
    uint32_t  cnt = p_moving_window_rms->cnt;
    if (0 == cnt)
    {
        return NAN;
    }
    while (cnt > 0)
    {
        cnt -= 1;
        if (0 == idx)
        {
            idx = SPL_CALC_AVERAGING_PERIOD_SEC - 1;
        }
        else
        {
            idx -= 1;
        }
        sum += p_moving_window_rms->arr_rms[idx];
    }
    return sum / (float32_t)p_moving_window_rms->cnt;
}

static float32_t
moving_window_rms_get_last(const moving_window_rms_t* const p_moving_window_rms)
{
    if (0 == p_moving_window_rms->cnt)
    {
        return NAN;
    }
    if (0 == p_moving_window_rms->idx)
    {
        return p_moving_window_rms->arr_rms[SPL_CALC_AVERAGING_PERIOD_SEC - 1];
    }
    return p_moving_window_rms->arr_rms[p_moving_window_rms->idx - 1];
}

bool
spl_calc_handle_buffer(q15_t* const p_buffer, float32_t* const p_buf_f32, const uint16_t num_samples)
{
    q15_t mean_val = moving_window_mean_add(&g_moving_mean, p_buffer, num_samples);

    for (uint16_t i = 0; i < num_samples; ++i)
    {
        p_buffer[i] -= mean_val;
    }

    bool        is_rms_ready             = false;
    const q63_t sum_of_square_unfiltered = dsp_sum_of_square_q15(p_buffer, num_samples);

    if (accum_rms_add(&g_accum_rms_unfiltered, (float32_t)sum_of_square_unfiltered))
    {
        const float32_t rms_unfiltered_max = accum_rms_get_max(&g_accum_rms_unfiltered);
        moving_window_rms_add(&g_moving_max_rms, rms_unfiltered_max);
    }

#if CONFIG_RUUVI_AIR_MIC_PDM_SAMPLE_RATE == 16000
    dsp_biquad_filter_a_weighting_16000_f32(&g_weighting_filter_state_f32, p_buf_f32, p_buf_f32, num_samples);
#elif CONFIG_RUUVI_AIR_MIC_PDM_SAMPLE_RATE == 20828
    dsp_biquad_filter_a_weighting_20828_f32(&g_weighting_filter_state_f32, p_buf_f32, p_buf_f32, num_samples);
#else
#error "Unsupported sample rate"
#endif
    const float32_t sum_of_square_filtered = dsp_sum_of_square_f32(p_buf_f32, num_samples) * (MAX_Q15_F * MAX_Q15_F);
    if (accum_rms_add(&g_accum_rms_filtered, sum_of_square_filtered))
    {
        const float32_t rms_filtered_avg = accum_rms_get_avg(&g_accum_rms_filtered);
        moving_window_rms_add(&g_moving_avg_rms, rms_filtered_avg);
        is_rms_ready = true;
    }
    return is_rms_ready;
}

float32_t
spl_calc_get_rms_max(void)
{
    return moving_window_rms_get_max(&g_moving_max_rms);
}

float32_t
spl_calc_get_rms_avg(void)
{
    return moving_window_rms_get_avg(&g_moving_avg_rms);
}

float32_t
spl_calc_get_rms_last_max(void)
{
    return moving_window_rms_get_last(&g_moving_max_rms);
}

float32_t
spl_calc_get_rms_last_avg(void)
{
    return moving_window_rms_get_last(&g_moving_avg_rms);
}
