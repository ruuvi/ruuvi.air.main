/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef BLE_ADV_H
#define BLE_ADV_H

#include <stdbool.h>
#include <stdint.h>
#include "ruuvi_air_types.h"
#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
ble_adv_init(void);

radio_mac_t
ble_adv_get_mac(void);

void
ble_adv_restart(const sensors_measurement_t* const p_measurement, const measurement_cnt_t measurement_cnt);

#ifdef __cplusplus
}
#endif

#endif // BLE_ADV_H
