/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef HW_IMG_HW_REV_H
#define HW_IMG_HW_REV_H

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/fs/fs.h>
#include <bootutil/image.h>
#include <fw_info_bare.h>
#include "ruuvi_fa_id.h"
#endif // CONFIG_BOOTLOADER_MCUBOOT
#include "ruuvi_image_tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_BOOTLOADER_MCUBOOT)

#define FW_INFO_HW_REV_NAME_MAX_LEN 15

typedef struct fw_image_hw_rev_t
{
    uint32_t hw_rev_num;
    char     hw_rev_name[FW_INFO_HW_REV_NAME_MAX_LEN + 1];
} fw_image_hw_rev_t;

typedef enum fw_img_id_e
{
    FW_IMG_ID_APP,
    FW_IMG_ID_FWLOADER,
    FW_IMG_ID_MCUBOOT0,
    FW_IMG_ID_MCUBOOT1,
} fw_img_id_e;

bool
fw_img_hw_rev_find_in_flash_area(const fa_id_t fa_id, fw_image_hw_rev_t* const p_hw_rev);

bool
fw_img_hw_rev_find_in_file(struct fs_file_t* const p_file, fw_image_hw_rev_t* const p_hw_rev);

bool
fw_img_get_image_info(
    const fw_img_id_e           fw_img_id,
    struct image_version* const p_fw_ver,
    const struct fw_info**      p_p_fw_info,
    fw_image_hw_rev_t* const    p_hw_rev);

void
fw_img_print_image_info(
    const fw_img_id_e           fw_img_id,
    struct image_version* const p_fw_ver,
    fw_image_hw_rev_t* const    p_hw_rev);

#endif // CONFIG_BOOTLOADER_MCUBOOT

#ifdef __cplusplus
}
#endif

#endif // HW_IMG_HW_REV_H
