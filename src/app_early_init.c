/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <pm_config.h>
#include <fprotect.h>
#include "app_supercap.h"
#include "app_ext_flash_and_sensors_power.h"
#include "app_button_cb.h"
#include "app_led.h"
#include "zephyr_api.h"

LOG_MODULE_DECLARE(main, LOG_LEVEL_INF);

#define EARLY_INIT_PERIPHERAL_POWER_OFF_DELAY_MS (100)
#define EARLY_INIT_PERIPHERAL_POWER_ON_DELAY_MS  (100)

#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)
#elif defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_2)
#else
#error "Unsupported board configuration. CONFIG_BOARD_RUUVI_RUUVIAIR_REV_<X> must be defined."
#endif

#define CONFIG_RUUVI_AIR_GPIO_SENSORS_POWER_ON_PRIORITY 41
_Static_assert(CONFIG_RUUVI_AIR_GPIO_SENSORS_POWER_ON_PRIORITY > CONFIG_GPIO_INIT_PRIORITY);
_Static_assert(CONFIG_RUUVI_AIR_GPIO_SENSORS_POWER_ON_PRIORITY < CONFIG_NORDIC_QSPI_NOR_INIT_PRIORITY);

static int // NOSONAR: Zephyr init functions must return int
app_early_init_post_kernel(void)
{
    printk("\r\n*** %s ***\r\n", CONFIG_NCS_APPLICATION_BOOT_BANNER_STRING);
#if defined(CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1)
    app_supercap_init();
#endif // CONFIG_BOARD_RUUVI_RUUVIAIR_REV_1
    app_button_cb_init();
    app_led_early_init();
    app_ext_flash_and_sensors_power_off();
    app_led_red_set(true);
    k_msleep(EARLY_INIT_PERIPHERAL_POWER_OFF_DELAY_MS);
    app_ext_flash_and_sensors_power_on();
    app_led_red_set(false);
    k_msleep(EARLY_INIT_PERIPHERAL_POWER_ON_DELAY_MS);
    return 0;
}

SYS_INIT(app_early_init_post_kernel, POST_KERNEL, CONFIG_RUUVI_AIR_GPIO_SENSORS_POWER_ON_PRIORITY);

#if defined(PM_MCUBOOT_PRIMARY_ADDRESS)
static int // NOSONAR: Zephyr init functions must return int
fprotect_self(void)
{
    LOG_INF(
        "Protecting app area: address 0x%08" PRIx32 ", size %" PRIx32,
        PM_MCUBOOT_PRIMARY_ADDRESS,
        PM_MCUBOOT_PRIMARY_SIZE);
    zephyr_api_ret_t err = fprotect_area(PM_MCUBOOT_PRIMARY_ADDRESS, PM_MCUBOOT_PRIMARY_SIZE);
    if (0 != err)
    {
        __ASSERT(
            0,
            "Unable to lock required area. Check address and "
            "size against locking granularity.");
    }
    return 0;
}

SYS_INIT(fprotect_self, APPLICATION, 0);
#endif // PM_MCUBOOT_PRIMARY_ADDRESS
