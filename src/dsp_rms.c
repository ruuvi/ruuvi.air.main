#include "dsp_rms.h"
#include "dsp/filtering_functions.h"

q63_t
dsp_sum_of_square_q15(const q15_t* p_src, const uint32_t block_size)
{
    q63_t    sum       = 0;
    uint32_t block_cnt = block_size;
    while (block_cnt > 0U)
    {
        const q15_t val = *p_src++; // NOSONAR
        sum += ((q31_t)val * val);
        block_cnt -= 1;
    }
    return sum;
}

float32_t
dsp_sum_of_square_f32(const float32_t* p_src, const uint32_t block_size)
{
    float32_t sum       = 0;
    uint32_t  block_cnt = block_size;
    while (block_cnt > 0U)
    {
        const float32_t val = *p_src++; // NOSONAR
        sum += val * val;
        block_cnt -= 1;
    }
    return sum;
}

float32_t
dsp_rms_q15_f32(const q15_t* p_src, const uint32_t block_size)
{
    q63_t sum = dsp_sum_of_square_q15(p_src, block_size);
    return sqrtf((float32_t)sum / (float32_t)block_size);
}

q31_t
dsp_calc_sum_q15_q31(const q15_t* p_src, const uint16_t block_size)
{
    q31_t    sum       = 0;
    uint16_t block_cnt = block_size;
    while (block_cnt > 0U)
    {
        sum += *p_src++; // NOSONAR
        block_cnt -= 1;
    }
    return sum;
}
