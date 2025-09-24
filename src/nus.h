/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_AIR_NUS_H
#define RUUVI_AIR_NUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool
nus_init(void);

bool
nus_is_notif_enabled(void);

bool
nus_is_reading_hist_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_AIR_NUS_H
