/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef ZASSERT_H
#define ZASSERT_H

#include <zephyr/ztest.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZASSERT_EQ_INT(expected, actual) zassert_equal(expected, actual, "expected=%d, actual=%d", expected, actual)

#define ZASSERT_EQ_FLOAT4(expected, actual) \
    zassert_equal( \
        lrintf(expected * 10000), \
        lrintf(actual * 10000), \
        "expected=%f, actual=%f", \
        (double)expected, \
        (double)actual)

#ifdef __cplusplus
}
#endif

#endif // ZASSERT_H
