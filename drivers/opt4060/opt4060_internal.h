/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef OPT4060_INTERNAL_H_
#define OPT4060_INTERNAL_H_

#include "include/opt4060.h"
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

typedef struct opt4060_ch_data {
	uint8_t exponent;
	uint8_t cnt;
	bool is_valid;
	uint32_t mantissa;
} opt4060_ch_data_t;

typedef struct opt4060_data {
	struct opt4060_ch_data ch_data[4];
	uint16_t cfg_reg;
#ifdef CONFIG_OPT4060_INT
	const struct device *p_dev;
	struct gpio_callback gpio_int_cb;
#if CONFIG_OPT4060_TRIGGER_DATA_READY
	sensor_trigger_handler_t handler_drdy;
	const struct sensor_trigger *p_trig_drdy;
#endif
#if defined(CONFIG_OPT4060_INT_OWN_THREAD)
	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_OPT4060_THREAD_STACK_SIZE);
	struct k_thread thread;
	struct k_sem gpio_sem;
#elif defined(CONFIG_OPT4060_INT_GLOBAL_THREAD)
	struct k_work work;
#endif
#endif /* CONFIG_OPT4060_INT */
#if CONFIG_OPT4060_OP_MODE_ONESHOT
	bool flag_one_shot_started;
#else
	int32_t one_measurement_duration_ticks;
	int64_t sensor_channel_get_accum_time;
	int32_t sensor_channel_get_cnt;
	int32_t sensor_channel_get_duration_ticks;
#endif
} opt4060_data_t;

typedef struct opt4060_config {
	struct i2c_dt_spec i2c;
#if CONFIG_OPT4060_INT
	const struct gpio_dt_spec gpio_int;
#endif
} opt4060_config_t;

int opt4060_init_interrupt(const struct device *dev);

bool opt4060_read_all_channels(const struct device *const p_dev);

#endif /* OPT4060_INTERNAL_H_ */
