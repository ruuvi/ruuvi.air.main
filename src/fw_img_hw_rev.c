/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include "fw_img_hw_rev.h"
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <bootutil/bootutil_public.h>
#include <fw_info.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(main, LOG_LEVEL_INF);

static bool
find_tlvs_in_flash_area(
    const struct flash_area* const p_fa,
    const uint16_t                 magic,
    size_t                         start_offset,
    size_t* const                  p_tlvs_start_off,
    size_t* const                  p_tlvs_end_off)
{
    struct image_tlv_info tlv_info = { 0 };

    int rc = flash_area_read(p_fa, start_offset, &tlv_info, sizeof(tlv_info));
    if (0 != rc)
    {
        LOG_ERR("Failed to read TLV info from flash area %d, rc=%d", p_fa->fa_id, rc);
        return false;
    }

    if (tlv_info.it_magic != magic)
    {
        /* No TLVs. */
        LOG_ERR("TLVs with magic 0x%04x not found in flash area %d", magic, p_fa->fa_id);
        return false;
    }

    *p_tlvs_start_off = start_offset + sizeof(tlv_info);
    *p_tlvs_end_off   = *p_tlvs_start_off + tlv_info.it_tlv_tot;

    return true;
}

static bool
find_tlvs_in_file(
    struct fs_file_t* const p_file,
    const uint16_t          magic,
    size_t                  start_offset,
    size_t* const           p_tlvs_start_off,
    size_t* const           p_tlvs_end_off)
{
    struct image_tlv_info tlv_info = { 0 };

    int rc = fs_seek(p_file, start_offset, FS_SEEK_SET);
    if (0 != rc)
    {
        LOG_ERR("Failed to seek to the beginning of the image TLVs, rc=%d", rc);
        return false;
    }
    rc = fs_read(p_file, &tlv_info, sizeof(tlv_info));
    if (rc < 0)
    {
        LOG_ERR("Failed to read TLV info, rc=%d", rc);
        return false;
    }

    if (tlv_info.it_magic != magic)
    {
        /* No TLVs. */
        LOG_ERR("TLVs with magic 0x%04x not found", magic);
        return false;
    }

    *p_tlvs_start_off = start_offset + sizeof(tlv_info);
    *p_tlvs_end_off   = *p_tlvs_start_off + tlv_info.it_tlv_tot;

    return true;
}

bool
fw_img_hw_rev_find_in_flash_area(const int fa_id, fw_image_hw_rev_t* const p_hw_rev)
{
    bool                     is_success = false;
    const struct flash_area* p_fa       = NULL;

    p_hw_rev->hw_rev_num     = 0;
    p_hw_rev->hw_rev_name[0] = '\0';

    int rc = flash_area_open(fa_id, &p_fa);
    if (0 != rc)
    {
        LOG_ERR("Failed to open flash area %d, rc=%d", fa_id, rc);
        return false;
    }

    struct image_header img_hdr = { 0 };
    rc                          = boot_image_load_header(p_fa, &img_hdr);
    if (0 != rc)
    {
        goto func_exit;
    }

    /* Read the image's TLVs. Try to find the protected TLVs. */
    size_t tlvs_start_off = 0;
    size_t tlvs_end_off   = 0;
    if (!find_tlvs_in_flash_area(
            p_fa,
            IMAGE_TLV_PROT_INFO_MAGIC,
            img_hdr.ih_hdr_size + img_hdr.ih_img_size,
            &tlvs_start_off,
            &tlvs_end_off))
    {
        goto func_exit;
    }

    size_t data_off = tlvs_start_off;

    struct image_tlv tlv = { 0 };
    while (data_off + sizeof(tlv) <= tlvs_end_off)
    {
        rc = flash_area_read(p_fa, data_off, &tlv, sizeof(tlv));
        if (0 != rc)
        {
            LOG_ERR("Failed to read TLV from flash area %d, rc=%d", fa_id, rc);
            goto func_exit;
        }

        if (tlv.it_type == 0xff && tlv.it_len == 0xffff)
        {
            LOG_ERR("Invalid TLV in flash area %d", fa_id);
            goto func_exit;
        }
        if (tlv.it_type == IMAGE_TLV_RUUVI_HW_REV_ID)
        {
            if (tlv.it_len != sizeof(p_hw_rev->hw_rev_num))
            {
                LOG_ERR(
                    "Invalid Ruuvi HW revision ID TLV length %u in flash area %d, expected %zu",
                    tlv.it_len,
                    fa_id,
                    sizeof(p_hw_rev->hw_rev_num));
                goto func_exit;
            }
            if (0 != p_hw_rev->hw_rev_num)
            {
                LOG_ERR("Duplicate Ruuvi HW revision ID TLV in flash area %d", fa_id);
                goto func_exit;
            }
            uint8_t buf[sizeof(p_hw_rev->hw_rev_num)] = { 0 };
            rc                                        = flash_area_read(p_fa, data_off + sizeof(tlv), buf, sizeof(buf));
            if (0 != rc)
            {
                LOG_ERR("Failed to read TLV from flash area %d, rc=%d", fa_id, rc);
                goto func_exit;
            }
            p_hw_rev->hw_rev_num = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
            LOG_DBG("Found Ruuvi HW revision ID TLV in flash area %d: ID=%" PRIu32, fa_id, p_hw_rev->hw_rev_num);
        }
        if (tlv.it_type == IMAGE_TLV_RUUVI_HW_REV_NAME)
        {
            if (tlv.it_len >= sizeof(p_hw_rev->hw_rev_name))
            {
                LOG_ERR(
                    "Invalid Ruuvi HW revision name TLV length %u in flash area %d, expected %zu",
                    tlv.it_len,
                    fa_id,
                    sizeof(p_hw_rev->hw_rev_name));
                goto func_exit;
            }
            if ('\0' != p_hw_rev->hw_rev_name[0])
            {
                LOG_ERR("Duplicate Ruuvi HW revision name TLV in flash area %d", fa_id);
                goto func_exit;
            }
            rc = flash_area_read(p_fa, data_off + sizeof(tlv), p_hw_rev->hw_rev_name, tlv.it_len);
            if (0 != rc)
            {
                LOG_ERR("Failed to read TLV from flash area %d, rc=%d", fa_id, rc);
                goto func_exit;
            }
            p_hw_rev->hw_rev_name[tlv.it_len] = '\0';
            LOG_DBG("Found Ruuvi HW revision name TLV in flash area %d: name='%s'", fa_id, p_hw_rev->hw_rev_name);
        }

        if (('\0' != p_hw_rev->hw_rev_name[0]) && (0 != p_hw_rev->hw_rev_num))
        {
            /* Found both TLVs */
            LOG_DBG(
                "Found Ruuvi HW revision TLVs in flash area %d: ID=%" PRIu32 ", name='%s'",
                fa_id,
                p_hw_rev->hw_rev_num,
                p_hw_rev->hw_rev_name);
            is_success = true;
            break;
        }

        data_off += sizeof(tlv) + tlv.it_len;
    }
    if (!is_success)
    {
        LOG_ERR("Ruuvi HW revision TLVs not found in flash area %d", fa_id);
    }

func_exit:
    flash_area_close(p_fa);
    return is_success;
}

bool
fw_img_hw_rev_find_in_file(struct fs_file_t* const p_file, fw_image_hw_rev_t* const p_hw_rev)
{
    p_hw_rev->hw_rev_num     = 0;
    p_hw_rev->hw_rev_name[0] = '\0';

    const struct image_header img_hdr = { 0 };
    int                       rc      = fs_seek(p_file, 0, FS_SEEK_SET);
    if (0 != rc)
    {
        LOG_ERR("Failed to seek to the beginning of the file, rc=%d", rc);
        return false;
    }
    ssize_t len = fs_read(p_file, (void*)&img_hdr, sizeof(img_hdr));
    if (len < 0)
    {
        LOG_ERR("Failed reading image header, rc=%zd", len);
        return false;
    }
    if (len != sizeof(img_hdr))
    {
        LOG_ERR("Failed reading image header, len=%zd, expected len=%zu", len, sizeof(img_hdr));
        return false;
    }

    /* Read the image's TLVs. Try to find the protected TLVs. */
    size_t tlvs_start_off = 0;
    size_t tlvs_end_off   = 0;
    if (!find_tlvs_in_file(
            p_file,
            IMAGE_TLV_PROT_INFO_MAGIC,
            img_hdr.ih_hdr_size + img_hdr.ih_img_size,
            &tlvs_start_off,
            &tlvs_end_off))
    {
        return false;
    }

    size_t data_off = tlvs_start_off;

    struct image_tlv tlv = { 0 };
    while (data_off + sizeof(tlv) <= tlvs_end_off)
    {
        rc = fs_seek(p_file, data_off, FS_SEEK_SET);
        if (0 != rc)
        {
            LOG_ERR("Failed to seek to %zu, rc=%d", data_off, rc);
            return false;
        }
        rc = fs_read(p_file, &tlv, sizeof(tlv));
        if (rc < 0)
        {
            LOG_ERR("Failed to read TLV, rc=%d", rc);
            return false;
        }

        if (tlv.it_type == 0xff && tlv.it_len == 0xffff)
        {
            LOG_ERR("Invalid TLV");
            return false;
        }
        if (tlv.it_type == IMAGE_TLV_RUUVI_HW_REV_ID)
        {
            if (tlv.it_len != sizeof(p_hw_rev->hw_rev_num))
            {
                LOG_ERR(
                    "Invalid Ruuvi HW revision ID TLV length %u, expected %zu",
                    tlv.it_len,
                    sizeof(p_hw_rev->hw_rev_num));
                return false;
            }
            if (0 != p_hw_rev->hw_rev_num)
            {
                LOG_ERR("Duplicate Ruuvi HW revision ID TLV");
                return false;
            }
            uint8_t buf[sizeof(p_hw_rev->hw_rev_num)] = { 0 };
            rc                                        = fs_read(p_file, buf, sizeof(buf));
            if (rc < 0)
            {
                LOG_ERR("Failed to read TLV, rc=%d", rc);
                return false;
            }
            p_hw_rev->hw_rev_num = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
            LOG_DBG("Found Ruuvi HW revision ID TLV: ID=%" PRIu32, p_hw_rev->hw_rev_num);
        }
        if (tlv.it_type == IMAGE_TLV_RUUVI_HW_REV_NAME)
        {
            if (tlv.it_len >= sizeof(p_hw_rev->hw_rev_name))
            {
                LOG_ERR(
                    "Invalid Ruuvi HW revision name TLV length %u, expected %zu",
                    tlv.it_len,
                    sizeof(p_hw_rev->hw_rev_name));
                return false;
            }
            if ('\0' != p_hw_rev->hw_rev_name[0])
            {
                LOG_ERR("Duplicate Ruuvi HW revision name TLV");
                return false;
            }
            rc = fs_read(p_file, p_hw_rev->hw_rev_name, tlv.it_len);
            if (rc < 0)
            {
                LOG_ERR("Failed to read TLV, rc=%d", rc);
                return false;
            }
            p_hw_rev->hw_rev_name[tlv.it_len] = '\0';
            LOG_DBG("Found Ruuvi HW revision name TLV: name='%s'", p_hw_rev->hw_rev_name);
        }

        if (('\0' != p_hw_rev->hw_rev_name[0]) && (0 != p_hw_rev->hw_rev_num))
        {
            LOG_DBG(
                "Found Ruuvi HW revision TLVs: ID=%" PRIu32 ", name='%s'",
                p_hw_rev->hw_rev_num,
                p_hw_rev->hw_rev_name);
            return true;
        }

        data_off += sizeof(tlv) + tlv.it_len;
    }
    LOG_ERR("Ruuvi HW revision TLVs not found");
    return false;
}

static bool
get_flash_area_address_and_size(const int fa_id, uint32_t* const p_fa_addr, uint32_t* const p_fa_size)
{
    const struct flash_area* p_fa = NULL;
    int                      rc   = flash_area_open(fa_id, &p_fa);
    if (0 != rc)
    {
        LOG_ERR("Failed to open flash area %d, rc=%d", fa_id, rc);
        return false;
    }
    *p_fa_addr = flash_area_get_off(p_fa);
    *p_fa_size = flash_area_get_size(p_fa);
    flash_area_close(p_fa);
    return true;
}

static bool
load_image_header(const int fa_id, struct image_header* const p_img_hdr)
{
    bool is_success = false;

    const struct flash_area* p_fa = NULL;
    int                      rc   = flash_area_open(fa_id, &p_fa);
    if (0 != rc)
    {
        return false;
    }

    rc = boot_image_load_header(p_fa, p_img_hdr);
    if (0 != rc)
    {
        goto func_exit;
    }

    is_success = true;

func_exit:
    flash_area_close(p_fa);
    return is_success;
}

static int
fa_id_from_fw_img_id(const fw_img_id_e fw_img_id)
{
    switch (fw_img_id)
    {
        case FW_IMG_ID_APP:
            return PM_ID(mcuboot_primary);
        case FW_IMG_ID_FWLOADER:
            return PM_ID(mcuboot_secondary);
        case FW_IMG_ID_MCUBOOT0:
            return PM_ID(s0);
        case FW_IMG_ID_MCUBOOT1:
            return PM_ID(s1);
        default:
            LOG_ERR("Invalid fw_img_id %d", fw_img_id);
            return -1;
    }
}

bool
fw_img_get_image_info(
    const fw_img_id_e           fw_img_id,
    struct image_version* const p_fw_ver,
    const struct fw_info**      p_p_fw_info,
    fw_image_hw_rev_t* const    p_hw_rev)
{
    uint32_t  fa_addr = 0;
    uint32_t  fa_size = 0;
    const int fa_id   = fa_id_from_fw_img_id(fw_img_id);
    if (fa_id < 0)
    {
        return false;
    }
    if (!get_flash_area_address_and_size(fa_id, &fa_addr, &fa_size))
    {
        LOG_ERR("Failed to get flash area address and size for %d", fa_id);
        return false;
    }

    const struct fw_info* const p_fw_info = fw_info_find(fa_addr);
    if (NULL == p_fw_info)
    {
        LOG_ERR("Failed to find fw_info for flash area %d", fa_id);
        return false;
    }

    struct image_header img_hdr = { 0 };
    if (!load_image_header(fa_id, &img_hdr))
    {
        LOG_ERR("Failed to load image header for flash area %d", fa_id);
        return false;
    }
    fw_image_hw_rev_t hw_rev = { 0 };
    fw_img_hw_rev_find_in_flash_area(fa_id, &hw_rev);
    if (NULL != p_fw_ver)
    {
        *p_fw_ver = img_hdr.ih_ver;
    }
    if (NULL != p_p_fw_info)
    {
        *p_p_fw_info = p_fw_info;
    }
    if (NULL != p_hw_rev)
    {
        *p_hw_rev = hw_rev;
    }
    return true;
}

void
fw_img_print_image_info(
    const fw_img_id_e           fw_img_id,
    struct image_version* const p_fw_ver,
    fw_image_hw_rev_t* const    p_hw_rev)
{
    struct image_version  fw_ver    = { 0 };
    const struct fw_info* p_fw_info = NULL;
    fw_image_hw_rev_t     hw_rev    = { 0 };

    if (!fw_img_get_image_info(fw_img_id, &fw_ver, &p_fw_info, &hw_rev))
    {
        return;
    }

    LOG_INF(
        "### Flash area %d: Image version: v%u.%u.%u+%u, FwInfoVer: %u, HwRev: ID=%" PRIu32 ", name='%s' ###",
        fa_id_from_fw_img_id(fw_img_id),
        fw_ver.iv_major,
        fw_ver.iv_minor,
        fw_ver.iv_revision,
        fw_ver.iv_build_num,
        p_fw_info->version,
        hw_rev.hw_rev_num,
        hw_rev.hw_rev_name);
    if (NULL != p_fw_ver)
    {
        *p_fw_ver = fw_ver;
    }
    if (NULL != p_hw_rev)
    {
        *p_hw_rev = hw_rev;
    }
}

#endif // CONFIG_BOOTLOADER_MCUBOOT
