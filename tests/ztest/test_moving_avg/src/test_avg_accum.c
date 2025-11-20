/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include "avg_accum.h"
#include "zassert.h"

static void*
test_setup(void);

static void
test_suite_before(void* f);

static void
test_suite_after(void* f);

static void
test_teardown(void* f);

ZTEST_SUITE(test_suite_avg_accum, NULL, &test_setup, &test_suite_before, &test_suite_after, &test_teardown);

typedef struct test_suite_avg_accum_fixture
{
    int stub;
} test_suite_avg_accum_fixture_t;

static void*
test_setup(void)
{
    test_suite_avg_accum_fixture_t* p_fixture = calloc(1, sizeof(*p_fixture));
    assert(NULL != p_fixture);
    return p_fixture;
}

static void
test_suite_before(void* f)
{
    test_suite_avg_accum_fixture_t* p_fixture = f;
    memset(p_fixture, 0, sizeof(*p_fixture));
}

static void
test_suite_after(void* f)
{
}

static void
test_teardown(void* f)
{
    if (NULL != f)
    {
        free(f);
    }
}

ZTEST_F(test_suite_avg_accum, test_accum_i16)
{
    const int16_t invalid_value = -0x8000;
    avg_accum_t   accum         = avg_accum_init_i16(invalid_value);
    ZASSERT_EQ_INT(invalid_value, avg_accum_calc_avg_i16(&accum));
    ZASSERT_EQ_INT(0, accum.cnt);

    avg_accum_add_i16(&accum, invalid_value);
    ZASSERT_EQ_INT(invalid_value, avg_accum_calc_avg_i16(&accum));
    ZASSERT_EQ_INT(0, accum.cnt);

    avg_accum_add_i16(&accum, 10);
    ZASSERT_EQ_INT(10, avg_accum_calc_avg_i16(&accum));
    ZASSERT_EQ_INT(1, accum.cnt);

    avg_accum_add_i16(&accum, invalid_value);
    ZASSERT_EQ_INT(10, avg_accum_calc_avg_i16(&accum));
    ZASSERT_EQ_INT(1, accum.cnt);

    avg_accum_add_i16(&accum, 20);
    ZASSERT_EQ_INT(15, avg_accum_calc_avg_i16(&accum));
    ZASSERT_EQ_INT(2, accum.cnt);
}

ZTEST_F(test_suite_avg_accum, test_accum_u16_invalid_max)
{
    const uint16_t invalid_value = UINT16_MAX;
    avg_accum_t    accum         = avg_accum_init_u16(invalid_value);
    ZASSERT_EQ_INT(invalid_value, avg_accum_calc_avg_u16(&accum));
    ZASSERT_EQ_INT(0, accum.cnt);

    avg_accum_add_u16(&accum, invalid_value);
    ZASSERT_EQ_INT(invalid_value, avg_accum_calc_avg_u16(&accum));
    ZASSERT_EQ_INT(0, accum.cnt);

    avg_accum_add_u16(&accum, 10);
    ZASSERT_EQ_INT(10, avg_accum_calc_avg_u16(&accum));
    ZASSERT_EQ_INT(1, accum.cnt);

    avg_accum_add_u16(&accum, invalid_value);
    ZASSERT_EQ_INT(10, avg_accum_calc_avg_u16(&accum));
    ZASSERT_EQ_INT(1, accum.cnt);

    avg_accum_add_u16(&accum, 20);
    ZASSERT_EQ_INT(15, avg_accum_calc_avg_u16(&accum));
    ZASSERT_EQ_INT(2, accum.cnt);
}

ZTEST_F(test_suite_avg_accum, test_accum_f32_invalid_max)
{
    avg_accum_t accum = avg_accum_init_f32();
    zassert_true(isnan(avg_accum_calc_avg_f32(&accum)));
    ZASSERT_EQ_INT(0, accum.cnt);

    avg_accum_add_f32(&accum, NAN);
    zassert_true(isnan(avg_accum_calc_avg_f32(&accum)));
    ZASSERT_EQ_INT(0, accum.cnt);

    avg_accum_add_f32(&accum, 10.0f);
    ZASSERT_EQ_FLOAT(10.0f, avg_accum_calc_avg_f32(&accum));
    ZASSERT_EQ_INT(1, accum.cnt);

    avg_accum_add_f32(&accum, NAN);
    ZASSERT_EQ_FLOAT(10.0f, avg_accum_calc_avg_f32(&accum));
    ZASSERT_EQ_INT(1, accum.cnt);

    avg_accum_add_f32(&accum, 20.0f);
    ZASSERT_EQ_FLOAT(15.0f, avg_accum_calc_avg_f32(&accum));
    ZASSERT_EQ_INT(2, accum.cnt);
}
