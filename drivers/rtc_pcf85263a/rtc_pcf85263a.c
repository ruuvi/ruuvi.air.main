/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/spinlock.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/logging/log.h>
#include "rtc_utils.h"
#include "rtc_pcf85263a_regs.h"

LOG_MODULE_REGISTER(RTC_PCF85263A, CONFIG_RTC_LOG_LEVEL);

#define BOOT_MODE_TYPE_FACTORY_RESET (0xAC)

#define DT_DRV_COMPAT nxp_pcf85263a

#define PCF85263A_BASE_YEAR      (2000U)
#define PCF85263A_MIN_VALID_YEAR (2020U)

#define RTC_PCF85263A_NUM_I2C_RETRIES (3)

#define RTC_PCF85263A_MAX_RTC_INTA_OFFSET_MS (Z_HZ_ms + 50)

struct pcf85263a_config {
	const struct i2c_dt_spec i2c;
#if CONFIG_RTC_PCF85263A_INT
	const struct gpio_dt_spec gpio_inta;
#endif /* CONFIG_RTC_PCF85263A_INT */
};

struct pcf85263a_data {
	struct k_spinlock lock;
	const struct device *dev;

#if CONFIG_RTC_PCF85263A_INT
	struct gpio_callback inta_callback;
	uint32_t rtc_unix_time;
	int64_t rtc_inta_generated_at_tick;
	uint32_t offset_nsec;
#endif /* CONFIG_RTC_PCF85263A_INT */
};

static time_t rtc_utils_time_to_sec(const struct rtc_time *const p_time_rtc)
{
	struct tm tm_conv = {
		.tm_sec = p_time_rtc->tm_sec,
		.tm_min = p_time_rtc->tm_min,
		.tm_hour = p_time_rtc->tm_hour,
		.tm_mday = p_time_rtc->tm_mday,
		.tm_mon = p_time_rtc->tm_mon,
		.tm_year = p_time_rtc->tm_year,
		.tm_wday = p_time_rtc->tm_wday,
		.tm_yday = p_time_rtc->tm_yday,
		.tm_isdst = p_time_rtc->tm_isdst,
	};

	return timeutil_timegm(&tm_conv);
}

static void pcf85263a_log_time_warn(const char *const p_prefix,
				    const struct rtc_time *const p_time_rtc)
{
	LOG_WRN("%s: %04d-%02d-%02d %02d:%02d:%02d", p_prefix,
		p_time_rtc->tm_year + TIME_UTILS_BASE_YEAR, p_time_rtc->tm_mon + 1,
		p_time_rtc->tm_mday, p_time_rtc->tm_hour, p_time_rtc->tm_min, p_time_rtc->tm_sec);
}

#if CONFIG_RTC_PCF85263A_INT
static void pcf85263a_log_time_info_with_counter(const char *const p_prefix,
						 const struct rtc_time *const p_time_rtc,
						 const uint32_t rtc_counter,
						 const uint32_t clock_time)
{
	const time_t unix_time = rtc_utils_time_to_sec(p_time_rtc);
	LOG_INF("%s: %04d-%02d-%02d %02d:%02d:%02d, unix_time=%" PRIu32 ", "
		"rtc_counter=%" PRIu32 ", clock=%" PRIu32,
		p_prefix, p_time_rtc->tm_year + TIME_UTILS_BASE_YEAR, p_time_rtc->tm_mon + 1,
		p_time_rtc->tm_mday, p_time_rtc->tm_hour, p_time_rtc->tm_min, p_time_rtc->tm_sec,
		(uint32_t)unix_time, rtc_counter, clock_time);
}
#else
static void pcf85263a_log_time_info(const char *const p_prefix,
				    const struct rtc_time *const p_time_rtc)
{
	const time_t unix_time = rtc_utils_time_to_sec(p_time_rtc);
	LOG_INF("%s: %04d-%02d-%02d %02d:%02d:%02d, unix_time=%" PRIu32, p_prefix,
		p_time_rtc->tm_year + TIME_UTILS_BASE_YEAR, p_time_rtc->tm_mon + 1,
		p_time_rtc->tm_mday, p_time_rtc->tm_hour, p_time_rtc->tm_min, p_time_rtc->tm_sec,
		(uint32_t)unix_time);
}
#endif

static int pcf85263a_read_regs_without_retries(const struct device *const dev,
					       const uint8_t reg_addr, void *const p_buf,
					       const size_t buf_len)
{
	const struct pcf85263a_config *const config = dev->config;
	return i2c_write_read_dt(&config->i2c, &reg_addr, sizeof(reg_addr), p_buf, buf_len);
}

static int pcf85263a_write_regs_without_retries(const struct device *const dev,
						const uint8_t reg_addr, uint8_t *const p_buf,
						const size_t buf_len)
{
	const struct pcf85263a_config *config = dev->config;
	uint8_t reg_addr_buf[1] = {reg_addr};
	struct i2c_msg msg[2] = {
		{
			.buf = reg_addr_buf,
			.len = sizeof(reg_addr_buf),
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = p_buf,
			.len = buf_len,
			.flags = I2C_MSG_WRITE | I2C_MSG_STOP,
		},
	};

	return i2c_transfer(config->i2c.bus, msg, sizeof(msg) / sizeof(msg[0]), config->i2c.addr);
}

static int pcf85263a_update_reg_without_retries(const struct device *const dev,
						const uint8_t reg_addr, const uint8_t mask,
						uint8_t val)
{
	const struct pcf85263a_config *config = dev->config;
	return i2c_reg_update_byte_dt(&config->i2c, reg_addr, mask, val);
}

static bool pcf85263a_read_regs(const struct device *const dev, const uint8_t reg_addr,
				void *const p_buf, const ssize_t buf_len)
{
	for (int i = 0; i < RTC_PCF85263A_NUM_I2C_RETRIES; i++) {
		const int err = pcf85263a_read_regs_without_retries(dev, reg_addr, p_buf, buf_len);
		if (0 == err) {
			return true;
		}
		LOG_WRN("Failed to read reg addr 0x%02x, len %d, err %d, retry %u", reg_addr,
			buf_len, err, i);
		k_msleep(10); // Wait before retrying
	}
	LOG_ERR("Failed to read reg addr 0x%02x, len %d", reg_addr, buf_len);
	return false;
}

static bool pcf85263a_read_reg(const struct device *const dev, const uint8_t reg_addr,
			       uint8_t *const p_val)
{
	return pcf85263a_read_regs(dev, reg_addr, p_val, sizeof(*p_val));
}

static bool pcf85263a_write_regs(const struct device *const dev, const uint8_t reg_addr,
				 uint8_t *const p_buf, const size_t buf_len)
{
	for (int i = 0; i < RTC_PCF85263A_NUM_I2C_RETRIES; i++) {
		const int err = pcf85263a_write_regs_without_retries(dev, reg_addr, p_buf, buf_len);
		if (0 == err) {
			return true;
		}
		LOG_WRN("Failed to write reg addr 0x%02x, len %d, err %d, retry %u", reg_addr,
			buf_len, err, i);
		k_msleep(10); // Wait before retrying
	}
	LOG_ERR("Failed to write reg addr 0x%02x, len %d", reg_addr, buf_len);
	return false;
}

static bool pcf85263a_write_reg(const struct device *const dev, const uint8_t reg_addr, uint8_t val)
{
	return pcf85263a_write_regs(dev, reg_addr, &val, sizeof(val));
}

static bool pcf85263a_update_reg(const struct device *const dev, const uint8_t reg_addr,
				 uint8_t mask, uint8_t val)
{
	for (int i = 0; i < RTC_PCF85263A_NUM_I2C_RETRIES; i++) {
		const int err = pcf85263a_update_reg_without_retries(dev, reg_addr, mask, val);
		if (0 == err) {
			return true;
		}
		LOG_WRN("Failed to update reg addr 0x%02x, mask 0x%02x, val 0x%02x, err %d, retry "
			"%d",
			reg_addr, mask, val, err, i);
		k_msleep(10); // Wait before retrying
	}
	LOG_ERR("Failed to update reg addr 0x%02x, mask 0x%02x, val 0x%02x", reg_addr, mask, val);
	return false;
}

#if CONFIG_RTC_PCF85263A_INT
static bool pcf85263a_get_seconds(const struct device *const dev, uint8_t *const p_seconds)
{
	uint8_t raw_seconds = 0;

	if (!pcf85263a_read_regs(dev, PCF85263A_REG_SECONDS, &raw_seconds, sizeof(raw_seconds))) {
		LOG_ERR("Failed to read time from RTC");
		return false;
	}
	*p_seconds = bcd2bin(raw_seconds & PCF85263A_REG_SECONDS_MASK);
	return true;
}
#endif /* CONFIG_RTC_PCF85263A_INT */

static int pcf85263a_get_time_from_hw(const struct device *dev, struct rtc_time *p_time_rtc,
				      const bool flag_print_log)
{
	uint8_t raw_data[PCF85263A_CALC_NUM_REGS(PCF85263A_REG_STOP_ENABLE, PCF85263A_REG_YEARS)] =
		{0};

	if (!pcf85263a_read_regs(dev, PCF85263A_REG_STOP_ENABLE, raw_data, sizeof(raw_data))) {
		LOG_ERR("Failed to read time from RTC");
		return -EIO;
	}

	const uint8_t *const p_raw_time =
		&raw_data[PCF85263A_CALC_NUM_REGS(PCF85263A_REG_STOP_ENABLE,
						  PCF85263A_REG_100TH_SECONDS) -
			  1];
	p_time_rtc->tm_nsec = bcd2bin(p_raw_time[PCF85263A_REG_100TH_SECONDS]) * 10U * 1000U *
			      1000U; // Convert 100th seconds to nanoseconds
	p_time_rtc->tm_sec =
		bcd2bin(p_raw_time[PCF85263A_REG_SECONDS] & PCF85263A_REG_SECONDS_MASK);
	p_time_rtc->tm_min =
		bcd2bin(p_raw_time[PCF85263A_REG_MINUTES] & PCF85263A_REG_MINUTES_MASK);
	p_time_rtc->tm_hour = bcd2bin(p_raw_time[PCF85263A_REG_HOURS] & PCF85263A_REG_HOURS_MASK);
	p_time_rtc->tm_mday = bcd2bin(p_raw_time[PCF85263A_REG_DAYS] & PCF85263A_REG_DAYS_MASK);
	p_time_rtc->tm_wday =
		bcd2bin(p_raw_time[PCF85263A_REG_WEEKDAYS] & PCF85263A_REG_WEEKDAYS_MASK);
	p_time_rtc->tm_mon = bcd2bin(p_raw_time[PCF85263A_REG_MONTH] & PCF85263A_REG_MONTHS_MASK);
	p_time_rtc->tm_mon -= 1;
	p_time_rtc->tm_year = bcd2bin(p_raw_time[PCF85263A_REG_YEARS]);
	p_time_rtc->tm_year += PCF85263A_BASE_YEAR - TIME_UTILS_BASE_YEAR;
	p_time_rtc->tm_yday = -1;
	p_time_rtc->tm_isdst = -1;

	if (0 != (raw_data[0] & PCF85263A_REG_STOP_ENABLE_STOP_BIT_SET)) {
		LOG_WRN("RTC is stopped");
		pcf85263a_log_time_warn("Time read from RTC", p_time_rtc);
		return -ENODATA;
	}

	if (0 != (p_raw_time[0] & PCF85263A_REG_SECONDS_OSC_STOP_MASK)) {
		LOG_WRN("Oscillator stop detected, time may be invalid");
		pcf85263a_log_time_warn("Time read from RTC", p_time_rtc);
		return -ENODATA;
	}

	if (!rtc_utils_validate_rtc_time(p_time_rtc, PCF85263A_RTC_TIME_MASK)) {
		LOG_WRN("Time is not valid");
		pcf85263a_log_time_warn("Time read from RTC", p_time_rtc);
		return -ENODATA;
	}
	if (flag_print_log) {
#if CONFIG_RTC_PCF85263A_INT
		struct pcf85263a_data *data = dev->data;
		pcf85263a_log_time_info_with_counter("Time read from RTC", p_time_rtc,
						     data->rtc_unix_time, time(NULL));
#else
		pcf85263a_log_time_info("Time read from RTC", p_time_rtc);
#endif /* CONFIG_RTC_PCF85263A_INT */
	}
	return 0;
}

static int pcf85263a_set_time(const struct device *dev, const struct rtc_time *p_time_rtc)
{
	const struct pcf85263a_config *config = dev->config;
	const time_t new_secs = rtc_utils_time_to_sec(p_time_rtc);

	if (!device_is_ready(dev)) {
		LOG_ERR("%s device not ready", dev->name);
		struct timespec ts = {
			.tv_sec = new_secs,
			.tv_nsec = 0,
		};
		clock_settime(CLOCK_REALTIME, &ts);
		LOG_INF("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec,
			(int32_t)ts.tv_nsec);
		return -ENODEV;
	}

	if (!rtc_utils_validate_rtc_time(p_time_rtc, PCF85263A_RTC_TIME_MASK)) {
		LOG_ERR("Invalid time provided");
		return -EINVAL;
	}
	if (0 != p_time_rtc->tm_nsec) {
		LOG_ERR("Setting nanoseconds is not supported, got %d", p_time_rtc->tm_nsec);
		return -EINVAL;
	}
	LOG_INF("Setting time to %04d-%02d-%02d %02d:%02d:%02d, unix time: %" PRIu32,
		p_time_rtc->tm_year + TIME_UTILS_BASE_YEAR, p_time_rtc->tm_mon + 1,
		p_time_rtc->tm_mday, p_time_rtc->tm_hour, p_time_rtc->tm_min, p_time_rtc->tm_sec,
		(uint32_t)new_secs);

	uint8_t buf1[1 +
		     PCF85263A_CALC_NUM_REGS(PCF85263A_REG_STOP_ENABLE, PCF85263A_REG_YEARS)] = {
		PCF85263A_REG_STOP_ENABLE,              // Address of the stop_enable register
		PCF85263A_REG_STOP_ENABLE_STOP_BIT_SET, // Stop_enable register:
							// Send STOP bit to stop the oscillator
		PCF85263A_REG_RESET_CMD_CPR,            // Reset register: Clear prescaler
		// Register address is reset to 0x00 after RESET_REG address (0x2F)
		bin2bcd(0), // 100th seconds
		bin2bcd(p_time_rtc->tm_sec),
		bin2bcd(p_time_rtc->tm_min),
		bin2bcd(p_time_rtc->tm_hour),
		bin2bcd(p_time_rtc->tm_mday),
		bin2bcd(p_time_rtc->tm_wday),
		bin2bcd(p_time_rtc->tm_mon + 1),
		bin2bcd(p_time_rtc->tm_year + TIME_UTILS_BASE_YEAR - PCF85263A_BASE_YEAR),
	};

	uint8_t buf2[2] = {
		PCF85263A_REG_STOP_ENABLE,                // Address of the stop_enable register
		PCF85263A_REG_STOP_ENABLE_STOP_BIT_CLEAR, // Stop_enable register:
							  // Clear STOP bit to start the oscillator
	};

	struct i2c_msg msg[2] = {
		{
			.buf = buf1,
			.len = sizeof(buf1),
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = buf2,
			.len = sizeof(buf2),
			.flags = I2C_MSG_WRITE | I2C_MSG_RESTART | I2C_MSG_STOP,
		},
	};
	int ret =
		i2c_transfer(config->i2c.bus, msg, sizeof(msg) / sizeof(msg[0]), config->i2c.addr);
	if (0 != ret) {
		LOG_ERR("Failed to set time: %d", ret);
		struct timespec ts = {
			.tv_sec = new_secs,
			.tv_nsec = 0,
		};
		clock_settime(CLOCK_REALTIME, &ts);
		LOG_INF("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec,
			(int32_t)ts.tv_nsec);
		return ret;
	}

#if CONFIG_RTC_PCF85263A_INT
	/* Update software counters to match the new time */
	struct pcf85263a_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	const uint32_t delay_since_inta_generated_ticks =
		(uint32_t)(k_uptime_ticks() - data->rtc_inta_generated_at_tick);
	uint32_t delay_since_inta_generated_nsec =
		k_ticks_to_ns_floor32(delay_since_inta_generated_ticks);
	data->rtc_unix_time = new_secs - 1 + (delay_since_inta_generated_nsec / Z_HZ_ns);
	delay_since_inta_generated_nsec %= Z_HZ_ns;
	data->offset_nsec = Z_HZ_ns - delay_since_inta_generated_nsec;
	struct timespec ts = {
		.tv_sec = new_secs,
		.tv_nsec = 0,
	};
	clock_settime(CLOCK_REALTIME, &ts);
	k_spin_unlock(&data->lock, key);

	LOG_INF("Delay between RTC and INTA: %" PRIu32 " ns", data->offset_nsec);
	LOG_INF("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec, (int32_t)ts.tv_nsec);
#endif /* CONFIG_RTC_PCF85263A_INT */

	return 0;
}

static int pcf85263a_get_time(const struct device *dev, struct rtc_time *p_time_rtc)
{
	if (!device_is_ready(dev)) {
		LOG_ERR("%s device not ready", dev->name);
		return -ENODEV;
	}
	return pcf85263a_get_time_from_hw(dev, p_time_rtc, false);
}

static bool pcf85263a_read_flag_clock_stopped(const struct device *dev,
					      bool *const p_flag_clock_stopped)
{
	uint8_t stop_en = 0;
	if (!pcf85263a_read_reg(dev, PCF85263A_REG_STOP_ENABLE, &stop_en)) {
		LOG_ERR("Failed to read STOP_ENABLE register");
		return false;
	}
	*p_flag_clock_stopped = (stop_en & PCF85263A_REG_STOP_ENABLE_STOP_MASK) != 0;
	return true;
}

static bool pcf85263a_clear_flag_oscillator_stopped(const struct device *dev)
{
	if (!pcf85263a_update_reg(dev, PCF85263A_REG_SECONDS, PCF85263A_REG_SECONDS_OSC_STOP_MASK,
				  0)) {
		LOG_ERR("Failed to clear oscillator stopped flag");
		return false;
	}
	LOG_INF("Oscillator stopped flag cleared successfully");
	return true;
}

static bool pcf85263a_software_reset(const struct device *dev)
{
	if (!pcf85263a_write_reg(dev, PCF85263A_REG_RESET, PCF85263A_REG_RESET_CMD_SR)) {
		LOG_ERR("Failed to write software reset command");
		return false;
	}
	LOG_INF("RTC software reset completed successfully");
	if (!pcf85263a_clear_flag_oscillator_stopped(dev)) {
		LOG_ERR("Failed to clear oscillator stopped flag");
		return false;
	}
	LOG_INF("Oscillator stopped flag cleared successfully after reset");
	return true;
}

#if CONFIG_RTC_PCF85263A_INT
static bool pcf85263a_configure_inta(const struct device *const dev)
{
	/* Configure INTA pin as interrupt output */
	LOG_DBG("Configuring INTA pin as interrupt output");
	if (!pcf85263a_update_reg(dev, PCF85263A_REG_PIN_IO, PCF85263A_REG_PIN_IO_INTAPM_MASK,
				  PCF85263A_REG_PIN_IO_INTAPM_INTA)) {
		LOG_ERR("Failed to configure INTA pin mode");
		return false;
	}

	/* Configure periodic interrupt for every second */
	LOG_DBG("Configuring periodic interrupt for every second");
	if (!pcf85263a_update_reg(dev, PCF85263A_REG_FUNCTION, PCF85263A_REG_FUNC_PI_MASK,
				  PCF85263A_REG_FUNC_PI_ONCE_PER_SECOND)) {
		LOG_ERR("Failed to configure periodic interrupt");
		return false;
	}

	/* Enable periodic interrupt on INTA */
	if (!pcf85263a_write_reg(dev, PCF85263A_REG_INTA_ENABLE, PCF85263A_REG_INTA_ENABLE_PIEA)) {
		LOG_ERR("Failed to enable periodic interrupt on INTA");
		return false;
	}
	return true;
}

static void inta_callback_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct pcf85263a_data *data = CONTAINER_OF(cb, struct pcf85263a_data, inta_callback);

	k_spinlock_key_t key = k_spin_lock(&data->lock);
	data->rtc_inta_generated_at_tick = k_uptime_ticks();
	data->rtc_unix_time += 1;
	const struct timespec ts = {.tv_sec = data->rtc_unix_time, .tv_nsec = data->offset_nsec};
	clock_settime(CLOCK_REALTIME, &ts);
	k_spin_unlock(&data->lock, key);
}

static bool configure_gpio_inta(const struct device *const dev)
{
	const struct pcf85263a_config *config = dev->config;
	struct pcf85263a_data *data = dev->data;

	int ret = gpio_pin_configure_dt(&config->gpio_inta, GPIO_INPUT);
	if (0 != ret) {
		LOG_ERR("Failed to configure INTA GPIO, error: %d", ret);
		return false;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->gpio_inta, GPIO_INT_EDGE_TO_ACTIVE);
	if (0 != ret) {
		LOG_ERR("Failed to configure INTA GPIO interrupt, error: %d", ret);
		return false;
	}

	gpio_init_callback(&data->inta_callback, &inta_callback_handler,
			   BIT(config->gpio_inta.pin));
	gpio_add_callback(config->gpio_inta.port, &data->inta_callback);

	return true;
}

static bool measure_delay_between_rtc_inta_and_set_time(
	const struct device *const dev, const struct rtc_time *const p_initial_rtc_time,
	const uint32_t initial_unix_time, uint32_t *const p_offset_nsec)
{
	struct pcf85263a_data *data = dev->data;
	/* Measure the shift between the RTC seconds counter change and
	   the interrupt generation on INTA. This is done by reading the
	   current RTC seconds and comparing it with the start time.
	   The offset is calculated based on the difference between the
	   current RTC seconds and the start time, taking into account
	   the tick at which the INTA was generated. */

	bool flag_inta_req_detected = false;
	bool flag_rtc_seconds_switched = false;
	uint8_t cur_rtc_seconds = 0;
	int64_t rtc_time_incremented_at_tick = 0;
	int64_t rtc_inta_generated_at_tick = 0;
	const int64_t start_at_tick = k_uptime_ticks();
	while (!flag_inta_req_detected || !flag_rtc_seconds_switched) {
		if (!flag_rtc_seconds_switched) {
			if (!pcf85263a_get_seconds(dev, &cur_rtc_seconds)) {
				LOG_ERR("Failed to read current RTC seconds");
				return false;
			}
			if (cur_rtc_seconds != p_initial_rtc_time->tm_sec) {
				rtc_time_incremented_at_tick = k_uptime_ticks();
				flag_rtc_seconds_switched = true;
			}
		}
		k_spinlock_key_t key = k_spin_lock(&data->lock);
		if ((!flag_inta_req_detected) && (0 != data->rtc_inta_generated_at_tick)) {
			flag_inta_req_detected = true;
			rtc_inta_generated_at_tick = data->rtc_inta_generated_at_tick;
			if (!flag_rtc_seconds_switched) {
				data->rtc_unix_time -= 1;
			}
		}
		k_spin_unlock(&data->lock, key);
		if ((k_uptime_ticks() - start_at_tick) > k_ms_to_ticks_floor32(2 * Z_HZ_ms)) {
			LOG_ERR("Timeout waiting for RTC seconds switch or INTA request");
			return false;
		}
	}
	const int32_t offset_ticks =
		(int32_t)(rtc_inta_generated_at_tick - rtc_time_incremented_at_tick);
	if (offset_ticks >= 0) {
		if (offset_ticks > k_ms_to_ticks_floor32(RTC_PCF85263A_MAX_RTC_INTA_OFFSET_MS)) {
			LOG_ERR("RTC time incremented at tick %" PRId64 ", "
				"but INTA generated at tick %" PRId64 ", "
				"offset is too large: %" PRId32 " ticks",
				rtc_time_incremented_at_tick, rtc_inta_generated_at_tick,
				offset_ticks);
			return false;
		}
		const uint32_t offset_ticks_safe = (offset_ticks >= k_ms_to_ticks_floor32(Z_HZ_ms))
							   ? k_ms_to_ticks_floor32(Z_HZ_ms - 1)
							   : offset_ticks;
		*p_offset_nsec = k_ticks_to_ns_floor32(offset_ticks_safe);
	} else {
		if (offset_ticks <
		    (-1 * k_ms_to_ticks_floor32(RTC_PCF85263A_MAX_RTC_INTA_OFFSET_MS))) {
			LOG_ERR("RTC time incremented at tick %" PRId64 ", "
				"but INTA generated at tick %" PRId64 ", "
				"offset is too large: %" PRId32 " ticks",
				rtc_time_incremented_at_tick, rtc_inta_generated_at_tick,
				offset_ticks);
			return false;
		}
		const uint32_t offset_ticks_safe =
			(offset_ticks < (-1 * k_ms_to_ticks_floor32(Z_HZ_ms)))
				? 0
				: k_ms_to_ticks_floor32(Z_HZ_ms) + offset_ticks;
		*p_offset_nsec = k_ticks_to_ns_floor32(offset_ticks_safe);
	}

	LOG_INF("RTC seconds switched at tick  %" PRId64, rtc_time_incremented_at_tick);
	LOG_INF("INTA request detected at tick %" PRId64, rtc_inta_generated_at_tick);
	LOG_INF("Delay between RTC and INTA: %" PRId32 " ticks, %" PRIu32 " ns", offset_ticks,
		*p_offset_nsec);

	k_spinlock_key_t key = k_spin_lock(&data->lock);
	const uint32_t delay_since_inta_generated_ticks =
		(uint32_t)(k_uptime_ticks() - rtc_inta_generated_at_tick);
	const uint32_t delay_since_inta_generated_nsec =
		k_ticks_to_ns_floor32(delay_since_inta_generated_ticks);
	struct timespec ts = {
		.tv_sec = data->rtc_unix_time +
			  (data->offset_nsec + delay_since_inta_generated_nsec) / Z_HZ_ns,
		.tv_nsec = (data->offset_nsec + delay_since_inta_generated_nsec) % Z_HZ_ns,
	};
	clock_settime(CLOCK_REALTIME, &ts);
	k_spin_unlock(&data->lock, key);

	LOG_INF("Set clock to %" PRIu32 ".%" PRId32, (uint32_t)ts.tv_sec, (int32_t)ts.tv_nsec);

	return true;
}
#endif /* CONFIG_RTC_PCF85263A_INT */

static int pcf85263a_init(const struct device *dev)
{
	const struct pcf85263a_config *config = dev->config;
	struct pcf85263a_data *data = dev->data;

	data->dev = dev;

	if (!device_is_ready(config->i2c.bus)) {
		LOG_ERR("I2C bus device not ready");
		return -ENODEV;
	}

	bool flag_clock_stopped = false;
	if (!pcf85263a_read_flag_clock_stopped(dev, &flag_clock_stopped)) {
		LOG_ERR("Failed to read STOP_ENABLE register");
		return -EIO;
	}

	bool need_reset = false;
	struct rtc_time initial_rtc_time = {0};
	if (flag_clock_stopped) {
		LOG_WRN("RTC is stopped, will reset RTC to start it");
		need_reset = true;
	} else {
		int ret = pcf85263a_get_time_from_hw(dev, &initial_rtc_time, true);
		if (0 != ret) {
			if (-ENODATA != ret) {
				LOG_ERR("Failed to get initial time from RTC hardware");
				return ret;
			}
			LOG_WRN("Initial time from RTC hardware is invalid, "
				"need to perform RTC software reset");
			need_reset = true;
		}
		if ((initial_rtc_time.tm_year + TIME_UTILS_BASE_YEAR) < PCF85263A_MIN_VALID_YEAR) {
			LOG_WRN("Initial time from RTC hardware is out of range, "
				"need to perform RTC software reset");
			need_reset = true;
		}
	}
	if (bootmode_check(BOOT_MODE_TYPE_FACTORY_RESET)) {
		LOG_WRN("Factory reset was performed - need to reset RTC");
		need_reset = true;
	}
	if (need_reset) {
		k_msleep(500);
		LOG_INF("Performing RTC software reset");
		if (!pcf85263a_software_reset(dev)) {
			LOG_ERR("Failed to reset RTC hardware");
			return -EIO;
		}
		int ret = pcf85263a_get_time_from_hw(dev, &initial_rtc_time, true);
		if (0 != ret) {
			LOG_ERR("Failed to get initial time from RTC hardware");
			return ret;
		}
	}

#if CONFIG_RTC_PCF85263A_INT
	const uint32_t initial_unix_time = rtc_utils_time_to_sec(&initial_rtc_time);
	LOG_WRN("Initial time read from RTC: %04d-%02d-%02d %02d:%02d:%02d, "
		"Unix time: %" PRIu32,
		initial_rtc_time.tm_year + TIME_UTILS_BASE_YEAR, initial_rtc_time.tm_mon + 1,
		initial_rtc_time.tm_mday, initial_rtc_time.tm_hour, initial_rtc_time.tm_min,
		initial_rtc_time.tm_sec, initial_unix_time);
	data->rtc_inta_generated_at_tick = 0;
	data->rtc_unix_time = initial_unix_time;
	data->offset_nsec = 0;

	if (!pcf85263a_configure_inta(dev)) {
		LOG_ERR("Failed to configure INTA pin");
		return -EIO;
	}
	if (!configure_gpio_inta(dev)) {
		LOG_ERR("Failed to configure GPIO INTA");
		return -EIO;
	}

	/* The INTA interrupt is generated independently of the RTC seconds counter
	   change and the generation of the INTA interrupt depends only on when
	   the RTC software reset is executed. Thus, the moment of seconds change
	   and the generation of this interrupt are always shifted relative
	   to each other by a fixed value depending on the moment of time
	   synchronization. */

	if (!measure_delay_between_rtc_inta_and_set_time(dev, &initial_rtc_time, initial_unix_time,
							 &data->offset_nsec)) {
		LOG_ERR("Failed to calculate shift between RTC seconds and INTA");
		return -EIO;
	}
#endif /* CONFIG_RTC_PCF85263A_INT */

#if CONFIG_RTC_PCF85263A_INT
	LOG_INF("%s initialized with interrupt-driven timekeeping", dev->name);
#else
	LOG_INF("%s initialized", dev->name);
#endif /* CONFIG_RTC_PCF85263A_INT */

#if 0
	// This is a test loop for debugging purposes.
	// It will continuously read the time from the RTC hardware and log it.
	LOG_INF("Starting test loop to read time from RTC hardware");
	while (1) {
		struct rtc_time time_rtc = {0};
		int ret = pcf85263a_get_time_from_hw(dev, &time_rtc, true);
		if (0 != ret) {
			LOG_ERR("Failed to get time from RTC hardware, error: %d", ret);
			return ret;
		}
		k_msleep(10);
	}
#endif

	return 0;
}

static const struct rtc_driver_api pcf85263a_driver_api = {
	.set_time = pcf85263a_set_time,
	.get_time = pcf85263a_get_time,
};

#if CONFIG_RTC_PCF85263A_INT
#define PCF85263A_CFG_INT(inst) .gpio_inta = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, inta_gpios, 0),
#else
#define PCF85263A_CFG_INT(inst)
#endif

#define PCF85263A_INIT(inst)                                                                       \
	static const struct pcf85263a_config pcf85263a_config_##inst = {                           \
		.i2c = I2C_DT_SPEC_INST_GET(inst), PCF85263A_CFG_INT(inst)};                       \
                                                                                                   \
	static struct pcf85263a_data pcf85263a_data_##inst;                                        \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &pcf85263a_init, NULL, &pcf85263a_data_##inst,                 \
			      &pcf85263a_config_##inst, POST_KERNEL, CONFIG_RTC_INIT_PRIORITY,     \
			      &pcf85263a_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PCF85263A_INIT)
