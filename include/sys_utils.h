/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef RUUVI_SYS_UTILS_H
#define RUUVI_SYS_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(BITS_PER_BYTE)
#define BITS_PER_BYTE (8U)
#else
_Static_assert(BITS_PER_BYTE == 8U, "BITS_PER_BYTE != 8"); // NOSONAR
#endif

#if !defined(BYTE_MASK)
#define BYTE_MASK (0xFFU)
#else
_Static_assert(BYTE_MASK == 0xFFU, "BYTE_MASK != 0xFFU"); // NOSONAR
#endif

#if !defined(UINT16_MASK)
#define UINT16_MASK (0xFFFFU)
#else
_Static_assert(UINT16_MASK == 0xFFFFU, "UINT16_MASK != 0xFFFFU"); // NOSONAR
#endif

#define BYTE_SHIFT_0 (0U)
#define BYTE_SHIFT_1 (8U)
#define BYTE_SHIFT_2 (16U)
#define BYTE_SHIFT_3 (24U)
#define BYTE_SHIFT_4 (32U)
#define BYTE_SHIFT_5 (40U)
#define BYTE_SHIFT_6 (48U)
#define BYTE_SHIFT_7 (56U)

#define BYTE_IDX_0 (0U)
#define BYTE_IDX_1 (1U)
#define BYTE_IDX_2 (2U)
#define BYTE_IDX_3 (3U)
#define BYTE_IDX_4 (4U)
#define BYTE_IDX_5 (5U)

#define ROUND_HALF_DIVISOR (2U)

#define PERCENT_100 (100U)

#define DECI_PERCENT_PER_PERCENT (10U)

#define BASE_10 (10U)

#ifdef __cplusplus
}
#endif

#endif // RUUVI_SYS_UTILS_H
