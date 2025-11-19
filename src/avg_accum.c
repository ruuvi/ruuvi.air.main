/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "avg_accum.h"
#include <stdbool.h>
#include <assert.h>
#include <math.h>

void
avg_accum_add_i16(avg_accum_t* const p_accum, const int16_t val)
{
    assert(p_accum->type == MOVING_AVG_VAL_TYPE_I16);
    assert(p_accum->cnt < UINT8_MAX);
    if ((p_accum->invalid_value.i16 != val) && (p_accum->cnt < UINT8_MAX))
    {
        p_accum->sum += (float32_t)val;
        p_accum->cnt += 1;
    }
}

void
avg_accum_add_u16(avg_accum_t* const p_accum, const uint16_t val)
{
    assert(p_accum->type == MOVING_AVG_VAL_TYPE_U16);
    assert(p_accum->cnt < UINT8_MAX);
    if ((p_accum->invalid_value.u16 != val) && (p_accum->cnt < UINT8_MAX))
    {
        p_accum->sum += (float32_t)val;
        p_accum->cnt += 1;
    }
}

void
avg_accum_add_f32(avg_accum_t* const p_accum, const float32_t val)
{
    assert(p_accum->type == MOVING_AVG_VAL_TYPE_F32);
    assert(p_accum->cnt < UINT8_MAX);
    if ((!(bool)isnan(val)) && (p_accum->cnt < UINT8_MAX))
    {
        p_accum->sum += val;
        p_accum->cnt += 1;
    }
}

int16_t
avg_accum_calc_avg_i16(const avg_accum_t* const p_accum)
{
    assert(MOVING_AVG_VAL_TYPE_I16 == p_accum->type);
    // ReSharper disable once CppDFAConstantConditions
    if ((0 == p_accum->cnt) || (UINT8_MAX == p_accum->cnt) || (MOVING_AVG_VAL_TYPE_I16 != p_accum->type))
    {
        return p_accum->invalid_value.i16;
    }
    const int32_t avg = lrintf(p_accum->sum / (float32_t)p_accum->cnt);
    if ((avg < INT16_MIN) || (avg > INT16_MAX))
    {
        return p_accum->invalid_value.i16;
    }
    return (int16_t)avg;
}

uint16_t
avg_accum_calc_avg_u16(const avg_accum_t* const p_accum)
{
    assert(MOVING_AVG_VAL_TYPE_U16 == p_accum->type);
    // ReSharper disable once CppDFAConstantConditions
    if ((0 == p_accum->cnt) || (UINT8_MAX == p_accum->cnt) || (MOVING_AVG_VAL_TYPE_U16 != p_accum->type))
    {
        return p_accum->invalid_value.u16;
    }
    const int32_t avg = lrintf(p_accum->sum / (float32_t)p_accum->cnt);
    if ((avg < 0) || (avg > UINT16_MAX))
    {
        return p_accum->invalid_value.u16;
    }
    return (uint16_t)avg;
}

float32_t
avg_accum_calc_avg_f32(const avg_accum_t* const p_accum)
{
    assert(MOVING_AVG_VAL_TYPE_F32 == p_accum->type);
    // ReSharper disable once CppDFAConstantConditions
    if ((0 == p_accum->cnt) || (UINT8_MAX == p_accum->cnt) || (MOVING_AVG_VAL_TYPE_F32 != p_accum->type))
    {
        return NAN;
    }
    return p_accum->sum / (float32_t)p_accum->cnt;
}
