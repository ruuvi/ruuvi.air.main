/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_IMAGE_TLV_H
#define RUUVI_IMAGE_TLV_H

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <bootutil/image.h>
#endif // CONFIG_BOOTLOADER_MCUBOOT

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_BOOTLOADER_MCUBOOT) || defined(CONFIG_MCUBOOT)

/* Image trailer TLV types are defined in
   ncs/v2.8.0/bootloader/mcuboot/boot/bootutil/include/bootutil/image.h

    xxA0-xxFF are vendor specific TLV types.
*/

#define IMAGE_TLV_RUUVI_HW_REV_ID   0x48A0 /* Ruuvi hardware revision ID */
#define IMAGE_TLV_RUUVI_HW_REV_NAME 0x48A1 /* Ruuvi hardware revision name */

#endif // CONFIG_BOOTLOADER_MCUBOOT || CONFIG_MCUBOOT

#ifdef __cplusplus
}
#endif

#endif // RUUVI_IMAGE_TLV_H
