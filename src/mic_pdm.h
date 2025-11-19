/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef MIC_PDM_H
#define MIC_PDM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIC_PDM_BLOCK_DURATION_MS     (50)
#define MIC_PDM_NUM_BLOCKS_PER_SECOND (1000 / MIC_PDM_BLOCK_DURATION_MS)

/* Calculate the DC offset at the 500 ms interval so that frequencies above 5 Hz are not affected. */
#define MIC_PDM_MEAN_MOVING_AVG_WINDOW_SIZE (500 / MIC_PDM_BLOCK_DURATION_MS)

#if !defined(CONFIG_RUUVI_AIR_MIC_PDM_SAMPLE_RATE)
#error "MIC PDM sample rate is not defined"
#endif
#define MIC_PDM_SAMPLE_RATE (CONFIG_RUUVI_AIR_MIC_PDM_SAMPLE_RATE)

#define MIC_PDM_BYTES_PER_SAMPLE sizeof(int16_t)

/* Size of a block for 20 ms of audio data. */
#define MIC_PDM_BLOCK_SIZE(_sample_rate, _number_of_channels) /* NOSONAR: used in array declarations */ \
    (MIC_PDM_BYTES_PER_SAMPLE * (_sample_rate / MIC_PDM_NUM_BLOCKS_PER_SECOND) * _number_of_channels)

#define MIC_PDM_MAX_BLOCK_SIZE MIC_PDM_BLOCK_SIZE(MIC_PDM_SAMPLE_RATE, 1)

#define MIC_PDM_NUM_SAMPLES_IN_BLOCK ((int32_t)(MIC_PDM_MAX_BLOCK_SIZE / MIC_PDM_BYTES_PER_SAMPLE))

typedef int8_t spl_db_t;
#define SPL_DB_INVALID (0)

void
mic_pdm_get_measurements(spl_db_t* const p_inst_db_a, spl_db_t* const p_avg_db_a, spl_db_t* const p_max_spl_db);

#ifdef __cplusplus
}
#endif

#endif // MIC_PDM_H
