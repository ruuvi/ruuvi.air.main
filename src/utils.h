/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_AIR_UTILS_H
#define RUUVI_AIR_UTILS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUUVI_AIR_MIN_UNIX_TIME (1577836800) /* 2020-01-01 00:00:00 UTC */
#define RUUVI_AIR_MAX_UNIX_TIME (0x7FFFFFFF) /* 2038-01-19 03:14:07 UTC */

uint64_t
get_device_id(void);

uint64_t
radio_address_get(void);

void
set_clock(const uint32_t unixtime, const bool flag_print_log);

void
app_post_event_refresh_led(void);

void
app_post_event_reload_settings(void);

#ifdef __cplusplus
}
#endif

#endif // RUUVI_AIR_UTILS_H
