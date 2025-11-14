/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_fw_ver.h"
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <flash_map_pm.h>
#include "fw_img_hw_rev.h"
#endif // CONFIG_BOOTLOADER_MCUBOOT
#include "app_version.h"
#include "ncs_version.h"
#include "version.h"
#if APP_VERSION_NUMBER != 0
#include "app_commit.h"
#endif
#include "ncs_commit.h"
#include "zephyr_commit.h"
#include "tlog.h"

LOG_MODULE_DECLARE(main, LOG_LEVEL_INF);

static char g_fw_ver_buf[sizeof(APP_VERSION_EXTENDED_STRING)];

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
struct image_version g_fw_img_fw_ver;
fw_image_hw_rev_t    g_fw_img_hw_rev;
#endif // CONFIG_BOOTLOADER_MCUBOOT

const int g_cfg_hw_rev =
#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)
    1
#elif defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_2)
    2
#else
    0
#endif
    ;

/**
 * Reorder version string:
 *   "MAJOR.MINOR.PATCH-EXTRA+BUILD" -> "MAJOR.MINOR.PATCH+BUILD-EXTRA"
 * If either EXTRA or BUILD is missing, the string is copied unchanged.
 *
 * Returns 0 on success, non-zero on error (e.g., buffer too small or null args).
 *
 * Notes:
 * - This doesn’t validate full SemVer; it just rearranges if both markers exist.
 * - Output length equals input length for the rearranged case.
 */
bool
semver_move_build_before_extra(const char* const p_orig_ver, char* const p_out_buf, const size_t out_buf_size)
{
    if ((NULL == p_orig_ver) || (NULL == p_out_buf) || (0 == out_buf_size))
    {
        return false;
    }

    const size_t orig_ver_len = strlen(p_orig_ver);

    // Identify the end of the core version (up to the first '-' or '+')
    const size_t core_len = strcspn(p_orig_ver, "-+");

    const char* const p_dash = strchr(p_orig_ver + core_len, '-'); // start of extra (if any)
    const char* const p_plus = strchr(p_orig_ver + core_len, '+'); // start of build (if any)

    // If both extra and build exist, we’ll reorder; otherwise just copy.
    if ((NULL != p_dash) && (NULL != p_plus))
    {
        // Determine which comes first to slice correctly.
        const char* p_ext_start = NULL;
        size_t      ext_len     = 0;
        const char* p_bld_start = NULL;
        size_t      bld_len     = 0;

        if (p_dash < p_plus)
        {
            // "core-extra+build"
            p_ext_start = p_dash + 1;
            ext_len     = (size_t)(p_plus - p_ext_start);
            p_bld_start = p_plus + 1;
            bld_len     = (size_t)((p_orig_ver + orig_ver_len) - p_bld_start);
        }
        else
        {
            // "core+build-extra" (already in desired order)
            p_bld_start = p_plus + 1;
            bld_len     = (size_t)(p_dash - p_bld_start);
            p_ext_start = p_dash + 1;
            ext_len     = (size_t)((p_orig_ver + orig_ver_len) - p_ext_start);
        }

        // Required output length: core + '+' + build + '-' + extra + NUL
        const size_t need = core_len + 1 + bld_len + 1 + ext_len + 1;
        if (out_buf_size < need)
        {
            return false;
        }

        char* p = p_out_buf;
        memcpy(p, p_orig_ver, core_len);
        p += core_len;
        *p++ = '+';
        memcpy(p, p_bld_start, bld_len);
        p += bld_len;
        *p++ = '-';
        memcpy(p, p_ext_start, ext_len);
        p += ext_len;
        *p = '\0';
    }
    else
    {
        // Nothing to reorder; just copy as-is.
        if (out_buf_size < (orig_ver_len + 1))
        {
            return false;
        }
        strcpy(p_out_buf, p_orig_ver);
    }
    return true;
}

void
app_fw_ver_init(void)
{
    __ASSERT(0 != g_cfg_hw_rev, "g_cfg_hw_rev not set, check Kconfig");

    // Rearrange version string to put build before extra, if both exist.
    if (!semver_move_build_before_extra(APP_VERSION_EXTENDED_STRING, g_fw_ver_buf, sizeof(g_fw_ver_buf)))
    {
        __ASSERT(false, "app_fw_ver_init failed: %s, %d", __FILE__, __LINE__);
    }

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    fw_img_print_image_info(FW_IMG_ID_APP, &g_fw_img_fw_ver, &g_fw_img_hw_rev);
    if (g_fw_img_hw_rev.hw_rev_num != g_cfg_hw_rev)
    {
        LOG_ERR(
            "Hardware revision mismatch: fw image hw_rev_id: %d, Kconfig: %d",
            g_fw_img_hw_rev.hw_rev_num,
            g_cfg_hw_rev);
    }
    __ASSERT(
        g_fw_img_hw_rev.hw_rev_num == g_cfg_hw_rev,
        "Hardware revision mismatch: fw image hw_rev_id: %d, Kconfig: %d",
        g_fw_img_hw_rev.hw_rev_id,
        g_cfg_hw_rev);

    char expected_version_str[32];
    snprintf(
        expected_version_str,
        sizeof(expected_version_str),
        "%u.%u.%u+%" PRIu32,
        g_fw_img_fw_ver.iv_major,
        g_fw_img_fw_ver.iv_minor,
        g_fw_img_fw_ver.iv_revision,
        g_fw_img_fw_ver.iv_build_num);
#endif // CONFIG_BOOTLOADER_MCUBOOT

#if defined(CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION)
    TLOG_INF(
        "### RuuviAir: Image version: %s (FwInfoCnt: %u)",
        CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION,
        CONFIG_FW_INFO_FIRMWARE_VERSION);
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    if (0 != strcmp(expected_version_str, CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION))
    {
        LOG_ERR(
            "Image version mismatch: fw image: %s, Kconfig: %s",
            expected_version_str,
            CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION);
    }
    __ASSERT(
        0 == strcmp(expected_version_str, CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION),
        "Image version mismatch: fw image: %s, Kconfig: %s",
        expected_version_str,
        CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION);
#endif // CONFIG_BOOTLOADER_MCUBOOT
#endif // CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION

#if APP_VERSION_NUMBER != 0
    TLOG_INF(
        "### RuuviAir: Version: %s, build: %s, APP_VERSION_NUMBER: %s",
        app_fw_ver_get(),
        STRINGIFY(APP_BUILD_VERSION),
        STRINGIFY(APP_VERSION_NUMBER));
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    if (0 != strcmp(expected_version_str, APP_VERSION_TWEAK_STRING))
    {
        LOG_ERR(
            "Image version mismatch: fw image: %s, App Version: %s",
            expected_version_str,
            APP_VERSION_TWEAK_STRING);
    }
    __ASSERT(
        0 == strcmp(expected_version_str, APP_VERSION_TWEAK_STRING),
        "Image version mismatch: fw image: %s, App Version: %s",
        expected_version_str,
        APP_VERSION_TWEAK_STRING);
#endif // CONFIG_BOOTLOADER_MCUBOOT

    TLOG_INF(
        "### RuuviAir: Version: %s, build: %s, commit: %s",
        app_fw_ver_get(),
        STRINGIFY(APP_BUILD_VERSION),
        APP_COMMIT_STRING);
#else
    TLOG_INF("### RuuviAir: Version: %s, build: %s", app_fw_ver_get(), STRINGIFY(APP_BUILD_VERSION));
#endif
    TLOG_INF(
        "### RuuviAir: NCS version: %s, build: %s, commit: %s",
        NCS_VERSION_STRING,
        STRINGIFY(NCS_BUILD_VERSION),
        NCS_COMMIT_STRING);
    TLOG_INF(
        "### RuuviAir: Kernel version: %s, build: %s, commit: %s",
        KERNEL_VERSION_EXTENDED_STRING,
        STRINGIFY(BUILD_VERSION),
        ZEPHYR_COMMIT_STRING);
}

const char*
app_fw_ver_get(void)
{
    return g_fw_ver_buf;
}

const char*
app_hw_rev_get(void)
{
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    if ('\0' == g_fw_img_hw_rev.hw_rev_name[0])
    {
        return CONFIG_BT_DIS_HW_REV_STR;
    }
    return g_fw_img_hw_rev.hw_rev_name;
#else
    return CONFIG_BT_DIS_HW_REV_STR;
#endif // CONFIG_BOOTLOADER_MCUBOOT
}
