/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "app_segger_rtt.h"
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_USE_SEGGER_RTT)
#include <SEGGER_RTT.h>
#endif

LOG_MODULE_DECLARE(main, LOG_LEVEL_INF);

#define APP_ASSERT(test, fmt, ...) \
    do \
    { \
        if (!(test)) \
        { \
            printk("fw_loader: ASSERTION FAIL @ %s:%d\n", __FILE__, __LINE__); \
            printk("\t" fmt "\n", ##__VA_ARGS__); \
            (void)arch_irq_lock(); \
            CODE_UNREACHABLE; \
        } \
    } while (false)

void
app_segger_rtt_check_data_location_and_size(void)
{
#if defined(CONFIG_USE_SEGGER_RTT)
    extern uint8_t __rtt_buff_data_start[];
    extern uint8_t __rtt_buff_data_end[];
    const size_t   rtt_buff_size = (size_t)(__rtt_buff_data_end - __rtt_buff_data_start);
    LOG_INF("RTT data address: %p", __rtt_buff_data_start);
    LOG_INF("RTT data size: 0x%zx", rtt_buff_size);
    APP_ASSERT(
        (uintptr_t)__rtt_buff_data_start == CONFIG_SRAM_BASE_ADDRESS,
        "__rtt_buff_data_start != CONFIG_SRAM_BASE_ADDRESS, 0x%p != 0x%08" PRIx32,
        __rtt_buff_data_start,
        CONFIG_SRAM_BASE_ADDRESS);
    APP_ASSERT(
        0 == (rtt_buff_size % 0x1000),
        ""
        "RTT buffer size is not aligned to 4kB, size=0x%zx",
        rtt_buff_size);

#define RTT_DATA_SRAM_NODE DT_NODELABEL(rtt_data)
#define RTT_DATA_SRAM_ADDR DT_REG_ADDR(RTT_DATA_SRAM_NODE)
#define RTT_DATA_SRAM_SIZE DT_REG_SIZE(RTT_DATA_SRAM_NODE)

    APP_ASSERT(
        (uintptr_t)__rtt_buff_data_start == RTT_DATA_SRAM_ADDR,
        "__rtt_buff_data_start != RTT_DATA_SRAM_ADDR, 0x%p != 0x%08" PRIx32,
        __rtt_buff_data_start,
        RTT_DATA_SRAM_ADDR);
    APP_ASSERT(
        rtt_buff_size == RTT_DATA_SRAM_SIZE,
        "__rtt_buff_data_start != RTT_DATA_SRAM_ADDR, 0x%08" PRIx32 " != 0x%08" PRIx32,
        rtt_buff_size,
        RTT_DATA_SRAM_SIZE);
#endif // defined(CONFIG_USE_SEGGER_RTT)
}
