/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef AVG_ACCUM_H
#define AVG_ACCUM_H

#include <stdint.h>

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
    float                  sum;
    uint8_t                cnt;
    const avg_accum_type_e type : 8;
    const union
    {
        const int16_t  i16;
        const uint16_t u16;
    } invalid_value;
} avg_accum_t;

_Static_assert(sizeof(avg_accum_t) == 8);

#define AVG_ACCUM_INIT_I16(invalid_val_) \
    { \
        .sum = 0, .cnt = 0, .type = MOVING_AVG_VAL_TYPE_I16, .invalid_value = {.i16 = invalid_val_ } \
    }
#define AVG_ACCUM_INIT_U16(invalid_val_) \
    { \
        .sum = 0, .cnt = 0, .type = MOVING_AVG_VAL_TYPE_U16, .invalid_value = {.u16 = invalid_val_ } \
    }
#define AVG_ACCUM_INIT_F32() \
    { \
        .sum = 0, .cnt = 0, .type = MOVING_AVG_VAL_TYPE_F32 \
    }

void
avg_accum_add_i16(avg_accum_t* const p_accum, const int16_t val);
void
avg_accum_add_u16(avg_accum_t* const p_accum, const uint16_t val);
void
avg_accum_add_f32(avg_accum_t* const p_accum, const float val);

int16_t
avg_accum_calc_avg_i16(const avg_accum_t* const p_accum);

uint16_t
avg_accum_calc_avg_u16(const avg_accum_t* const p_accum);

float
avg_accum_calc_avg_f32(const avg_accum_t* const p_accum);

#ifdef __cplusplus
}
#endif

#endif // AVG_ACCUM_H
