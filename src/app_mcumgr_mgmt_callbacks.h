/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef APP_MGMT_CALLBACKS_H
#define APP_MGMT_CALLBACKS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void
app_mcumgr_mgmt_callbacks_init(const char* const p_mnt_point);

bool
app_mcumgr_mgmt_callbacks_is_uploading_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif // APP_MGMT_CALLBACKS_H
