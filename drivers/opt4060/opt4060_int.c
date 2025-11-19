/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "opt4060.h"
#include "opt4060_internal.h"
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(opt4060, CONFIG_SENSOR_LOG_LEVEL);

static void opt4060_thread_cb(const struct device *const p_dev)
{
	opt4060_read_all_channels(p_dev);

#if CONFIG_OPT4060_TRIGGER
	opt4060_data_t *const p_data = p_dev->data;
	if (NULL != p_data->handler_drdy) {
		p_data->handler_drdy(p_dev, p_data->p_trig_drdy);
	}
#endif
}

#ifdef CONFIG_OPT4060_INT_OWN_THREAD
static void opt4060_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	opt4060_data_t *const p_data = p1;

	while (1) {
		k_sem_take(&p_data->gpio_sem, K_FOREVER);
		opt4060_thread_cb(p_data->p_dev);
	}
}
#endif

#ifdef CONFIG_OPT4060_INT_GLOBAL_THREAD
static void opt4060_work_cb(struct k_work *const p_work)
{
	opt4060_data_t *const p_data = CONTAINER_OF(p_work, opt4060_data_t, work);

	opt4060_thread_cb(p_data->p_dev);
}
#endif

static void opt4060_gpio_int_callback(const struct device *const p_dev,
				      struct gpio_callback *const p_cb, const uint32_t pins)
{
	opt4060_data_t *const p_data = CONTAINER_OF(p_cb, opt4060_data_t, gpio_int_cb);

	ARG_UNUSED(pins);

#if defined(CONFIG_OPT4060_INT_OWN_THREAD)
	k_sem_give(&p_data->gpio_sem);
#elif defined(CONFIG_OPT4060_INT_GLOBAL_THREAD)
	k_work_submit(&p_data->work);
#endif
}

opt4060_ret_t opt4060_init_interrupt(const struct device *p_dev)
{
	opt4060_data_t *const p_data = p_dev->data;
	const opt4060_config_t *const p_cfg = p_dev->config;

	p_data->p_dev = p_dev;

#if defined(CONFIG_OPT4060_INT_OWN_THREAD)
	k_sem_init(&p_data->gpio_sem, 0, K_SEM_MAX_LIMIT);

	k_thread_create(&p_data->thread, p_data->thread_stack, CONFIG_OPT4060_THREAD_STACK_SIZE,
			&opt4060_thread, p_data, NULL, NULL,
			K_PRIO_COOP(CONFIG_OPT4060_THREAD_PRIORITY), 0, K_NO_WAIT);
#elif defined(CONFIG_OPT4060_INT_GLOBAL_THREAD)
	p_data->work.handler = &opt4060_work_cb;
#endif

	/* setup gpio interrupt */
	if (!gpio_is_ready_dt(&p_cfg->gpio_int)) {
		/* API may return false even when ptr is NULL */
		if (p_cfg->gpio_int.port != NULL) {
			LOG_ERR("device %s is not ready", p_cfg->gpio_int.port->name);
			return -ENODEV;
		}

		LOG_DBG("gpio_int not defined in DT");
		return 0;
	}

	int status = gpio_pin_configure_dt(&p_cfg->gpio_int, GPIO_INPUT);
	if (status < 0) {
		LOG_ERR("Could not configure %s.%02u, err=%d", p_cfg->gpio_int.port->name,
			p_cfg->gpio_int.pin, status);
		return status;
	}

	gpio_init_callback(&p_data->gpio_int_cb, opt4060_gpio_int_callback,
			   BIT(p_cfg->gpio_int.pin));

	status = gpio_add_callback(p_cfg->gpio_int.port, &p_data->gpio_int_cb);
	if (status < 0) {
		LOG_ERR("Could not add gpio int callback, err=%d", status);
		return status;
	}

	LOG_INF("%s: int on %s.%02u", p_dev->name, p_cfg->gpio_int.port->name, p_cfg->gpio_int.pin);

	status = gpio_pin_interrupt_configure_dt(&p_cfg->gpio_int,
						 GPIO_INT_ENABLE | GPIO_INT_EDGE_FALLING);
	if (status < 0) {
		LOG_ERR("Could not configure interrupt, err=%d", status);
		return status;
	}

	return status;
}
