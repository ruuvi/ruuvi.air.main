/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_FA_ID_H
#define RUUVI_FA_ID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FA_ID_INVALID ((fa_id_t)0xFFU)

typedef uint8_t fa_id_t;

typedef uint8_t slot_id_t;

#ifdef __cplusplus
}
#endif

#endif // RUUVI_FA_ID_H
