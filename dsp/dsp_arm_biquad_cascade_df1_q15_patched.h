/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef DSP_ARM_BIQUAD_CASCADE_DF1_Q15_PATCHED_H
#define DSP_ARM_BIQUAD_CASCADE_DF1_Q15_PATCHED_H

#include "dsp/filtering_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

void arm_biquad_cascade_df1_q15_patched(
  const arm_biquad_casd_df1_inst_q15 * S,
  const q15_t * pSrc,
        q15_t * pDst,
        uint32_t blockSize);

#ifdef __cplusplus
}
#endif

#endif
