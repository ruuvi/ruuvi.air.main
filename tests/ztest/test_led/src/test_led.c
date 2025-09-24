/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <zephyr/ztest.h>
#include "app_led.h"

ZTEST_SUITE(led_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(led_tests, test_blinking)
{
    app_led_early_init();

    // LED works in GPIO mode
    for (int i = 0; i < 2; ++i)
    {
        k_msleep(400);
        app_led_green_set(true);
        k_msleep(400);
        app_led_green_set(false);

        k_msleep(400);
        app_led_red_set(true);
        k_msleep(400);
        app_led_red_set(false);
    }

    app_led_late_init_pwm();
    // LED works in PWM mode
    for (int i = 0; i < 2; ++i)
    {
        k_msleep(400);
        app_led_green_set(true);
        k_msleep(400);
        app_led_green_set(false);

        k_msleep(400);
        app_led_red_set(true);
        k_msleep(400);
        app_led_red_set(false);
    }
}
