/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

bool
app_watchdog_start(void);

void
app_watchdog_feed(void);

__NO_RETURN void
app_watchdog_force_trigger(void);

#ifdef __cplusplus
}
#endif

#endif // APP_WATCHDOG_H
