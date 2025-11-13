/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_button_cb.h"
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/atomic_builtin.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/logging/log.h>
#include "app_button.h"
#include "app_settings.h"
#include "app_led.h"
#include "utils.h"
#include "app_watchdog.h"

LOG_MODULE_DECLARE(GPIO, LOG_LEVEL_INF);

#define RUUVI_AIR_BUTTON_DELAY_BEFORE_REBOOT (CONFIG_RUUVI_AIR_BUTTON_DELAY_BEFORE_REBOOT - 500)

#define RUUVI_AIR_BUTTON_DELAY_FLUSH_LOGS_MS (100)

static struct gpio_callback g_button_isr_gpio_cb_data;

static void
button_workq_cb_pressed(struct k_work* item);
static void
button_workq_cb_released(struct k_work* item);
static void
button_workq_cb_timeout(struct k_work* item);
static void
button_workq_cb_reboot(struct k_work* item);
static void
button_workq_cb_changed_led_mode(struct k_work* item);

static K_WORK_DEFINE(g_button_work_pressed, &button_workq_cb_pressed);
static K_WORK_DEFINE(g_button_work_released, &button_workq_cb_released);
static K_WORK_DELAYABLE_DEFINE(g_button_work_delayable_timeout, &button_workq_cb_timeout);
static K_WORK_DELAYABLE_DEFINE(g_button_work_delayable_changed_led_mode, &button_workq_cb_changed_led_mode);
static K_WORK_DELAYABLE_DEFINE(g_button_work_delayable_reboot, &button_workq_cb_reboot);

static bool g_flag_switching_led_mode_in_progress;

static void
button_workq_cb_pressed(struct k_work* item)
{
    app_watchdog_feed();
    app_led_mutex_lock();
    app_button_set_pressed();
    app_led_red_on();
    app_led_green_on();
    app_led_mutex_unlock();
    k_work_reschedule(&g_button_work_delayable_timeout, K_MSEC(RUUVI_AIR_BUTTON_DELAY_BEFORE_REBOOT));
    if (!g_flag_switching_led_mode_in_progress)
    {
        g_flag_switching_led_mode_in_progress = true;
        app_settings_set_next_led_mode();
        app_post_event_refresh_led();
        k_work_reschedule(&g_button_work_delayable_changed_led_mode, K_MSEC(CONFIG_RUUVI_AIR_LED_DIMMING_INTERVAL_MS));
    }
    LOG_WRN("Button pressed");
}

static void
button_workq_cb_released(struct k_work* item)
{
    app_watchdog_feed();
    app_led_mutex_lock();
    app_led_red_off();
    app_led_green_off();
    app_button_clr_pressed();
    app_led_mutex_unlock();
    k_work_cancel_delayable(&g_button_work_delayable_timeout);
    LOG_WRN("Button released");
}

static void
button_workq_cb_changed_led_mode(struct k_work* item)
{
    g_flag_switching_led_mode_in_progress = false;
}

static void
button_workq_cb_timeout(struct k_work* item)
{
    LOG_WRN("Button %d ms timeout - rebooting...", CONFIG_RUUVI_AIR_BUTTON_DELAY_BEFORE_REBOOT);
    k_work_reschedule(&g_button_work_delayable_reboot, K_MSEC(RUUVI_AIR_BUTTON_DELAY_FLUSH_LOGS_MS));
}

static void
button_workq_cb_reboot(struct k_work* item)
{
#if CONFIG_DEBUG
    sys_reboot(SYS_REBOOT_COLD);
#else
    app_watchdog_force_trigger();
#endif
}

static void
app_isr_cb_pinhole_button_pressed_or_released(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
    if (app_button_get())
    {
        k_work_submit(&g_button_work_pressed);
    }
    else
    {
        k_work_submit(&g_button_work_released);
    }
}

void
app_button_cb_init(void)
{
    app_button_init(&g_button_isr_gpio_cb_data, &app_isr_cb_pinhole_button_pressed_or_released, GPIO_INT_EDGE_BOTH);
}

void
app_button_cb_deinit(void)
{
    app_button_deinit(&g_button_isr_gpio_cb_data);
}
