/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_fw_ver.h"
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <zephyr/sys/__assert.h>
#include "app_version.h"

static char g_fw_ver_buf[sizeof(APP_VERSION_EXTENDED_STRING)];

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
    // Rearrange version string to put build before extra, if both exist.
    if (!semver_move_build_before_extra(APP_VERSION_EXTENDED_STRING, g_fw_ver_buf, sizeof(g_fw_ver_buf)))
    {
        __ASSERT(false, "app_fw_ver_init failed: %s, %d", __FILE__, __LINE__);
    }
}

const char*
app_fw_ver_get(void)
{
    return g_fw_ver_buf;
}
