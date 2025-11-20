/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef AVG_ACCUM_H
#define AVG_ACCUM_H

#include <stdint.h>
#include <zephyr/dsp/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum avg_accum_type_e
{
    MOVING_AVG_VAL_TYPE_I16 = 0,
    MOVING_AVG_VAL_TYPE_U16 = 1,
    MOVING_AVG_VAL_TYPE_F32 = 2,
} avg_accum_type_e;

typedef struct avg_accum_t
{
    float32_t              sum;
    uint8_t                cnt;
    const avg_accum_type_e type : 8; // NOSONAR
    const union
    {
        const int16_t  i16;
        const uint16_t u16;
    } invalid_value;
} avg_accum_t;

#define AVG_ACCUM_SIZE (8)
_Static_assert(AVG_ACCUM_SIZE == sizeof(avg_accum_t));

static inline avg_accum_t
avg_accum_init_i16(const int16_t invalid_val)
{
    return (avg_accum_t){
        .sum = 0,
        .cnt = 0,
        .type = MOVING_AVG_VAL_TYPE_I16,
        .invalid_value = {
            .i16 = invalid_val,
        },
    };
}

static inline avg_accum_t
avg_accum_init_u16(const uint16_t invalid_val)
{
    return (avg_accum_t){
        .sum = 0,
        .cnt = 0,
        .type = MOVING_AVG_VAL_TYPE_U16,
        .invalid_value = {
            .u16 = invalid_val,
        },
    };
}

static inline avg_accum_t
avg_accum_init_f32(void)
{
    return (avg_accum_t) {
        .sum  = 0,
        .cnt  = 0,
        .type = MOVING_AVG_VAL_TYPE_F32,
    };
}

void
avg_accum_add_i16(avg_accum_t* const p_accum, const int16_t val);
void
avg_accum_add_u16(avg_accum_t* const p_accum, const uint16_t val);
void
avg_accum_add_f32(avg_accum_t* const p_accum, const float32_t val);

int16_t
avg_accum_calc_avg_i16(const avg_accum_t* const p_accum);

uint16_t
avg_accum_calc_avg_u16(const avg_accum_t* const p_accum);

float32_t
avg_accum_calc_avg_f32(const avg_accum_t* const p_accum);

#ifdef __cplusplus
}
#endif

#endif // AVG_ACCUM_H
