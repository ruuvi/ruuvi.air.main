/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef APP_FW_VER_H
#define APP_FW_VER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void
app_fw_ver_init(void);

const char*
app_fw_ver_get(void);

const char*
app_hw_rev_get(void);

#ifdef __cplusplus
}
#endif

#endif // APP_FW_VER_H
