/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef MIC_SPG08P4HM4H_H
#define MIC_SPG08P4HM4H_H

#ifdef __cplusplus
extern "C" {
#endif

// The sensitivity of the SPG08P4HM4H-1 microphone is specified as -26 dBFS at 94 dB SPL @ 1 kHz.
#define MIC_REFERENCE_SPL_DB (94)
#define MIC_SENSITIVITY_DBFS (-26)

#define MIC_MIN_PDM_CLK_FREQ (1000000)
#define MIC_MAX_PDM_CLK_FREQ (4800000)

#ifdef __cplusplus
}
#endif

#endif // MIC_SPG08P4HM4H_H
