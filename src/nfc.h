/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_AIR_NFC_H
#define RUUVI_AIR_NFC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool
nfc_init(const uint64_t mac);

void
nfc_update_data(const uint8_t* const p_buf, const size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_AIR_NFC_H
