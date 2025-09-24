/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if !defined(APP_RTC_H)
#define APP_RTC_H

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

bool
app_rtc_get_time(struct tm* const p_tm_time);

bool
app_rtc_set_time(const struct tm* const p_tm_time);

#ifdef __cplusplus
}
#endif

#endif // APP_RTC_H
