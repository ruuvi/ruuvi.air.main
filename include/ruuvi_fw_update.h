/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_FW_UPDATE_H
#define RUUVI_FW_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#define RUUVI_FW_PATH_MAX_SIZE (64)

#define RUUVI_FW_MCUBOOT0_FILE_NAME "signed_by_mcuboot_and_b0_mcuboot.bin"
#define RUUVI_FW_MCUBOOT1_FILE_NAME "signed_by_mcuboot_and_b0_s1_image.bin"
#define RUUVI_FW_LOADER_FILE_NAME   "ruuvi_air_fw_loader.signed.bin"
#define RUUVI_FW_APP_FILE_NAME      "ruuvi_air_fw.signed.bin"

#define RUUVI_FW_UPDATE_MOUNT_POINT "/lfs1"

#ifdef __cplusplus
}
#endif

#endif // RUUVI_FW_UPDATE_H
