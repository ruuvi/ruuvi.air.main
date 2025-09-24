/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <zephyr/ztest.h>
#include <stdbool.h>
#include <stdlib.h>
#include "./uut/uut.h"

extern int
__real_foo_init(void);

static bool g_flag_use_real_foo_init;
static int  g_flag_simulate_foo_init_result;

int
__wrap_foo_init(void)
{
    if (g_flag_use_real_foo_init)
    {
        return __real_foo_init();
    }
    return g_flag_simulate_foo_init_result;
}

typedef struct test_state
{
    int x;
} test_state;

static bool
predicate(const void* global_state)
{
    return true;
    // return ((const struct test_state*)global_state)->x == 5;
}

ZTEST_SUITE(alternating_suite, predicate, NULL, NULL, NULL, NULL);

struct my_suite_fixture
{
    size_t  max_size;
    size_t  size;
    uint8_t buff[1];
};

static void*
my_suite_setup(void)
{
    /* Allocate the fixture with 256 byte buffer */
    struct my_suite_fixture* fixture = malloc(sizeof(struct my_suite_fixture) + 255);

    zassume_not_null(fixture, NULL);
    fixture->max_size = 256;

    return fixture;
}

static void
my_suite_before(void* f)
{
    struct my_suite_fixture* fixture = (struct my_suite_fixture*)f;
    memset(fixture->buff, 0, fixture->max_size);
    fixture->size                   = 0;
    g_flag_use_real_foo_init        = true;
    g_flag_simulate_foo_init_result = 0;
}

static void
my_suite_teardown(void* f)
{
    free(f);
}

ZTEST_SUITE(my_suite, NULL, my_suite_setup, my_suite_before, NULL, my_suite_teardown);

ZTEST_F(my_suite, test_feature_x1)
{
    zassert_equal(0, fixture->size);
    zassert_equal(256, fixture->max_size);
}

ZTEST_F(my_suite, test_uut_init_with_NULL)
{
    const int err = uut_init(NULL);
    zassert_equal(-1, err);
}

ZTEST_F(my_suite, test_uut_init_real)
{
    int       x   = 0;
    const int err = uut_init(&x);
    zassert_equal(0, err);
}

ZTEST_F(my_suite, test_uut_init_simulate_error)
{
    int x                           = 0;
    g_flag_use_real_foo_init        = false;
    g_flag_simulate_foo_init_result = 2;
    const int err                   = uut_init(&x);
    zassert_equal(2, err);
}
