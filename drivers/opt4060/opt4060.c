/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#define DT_DRV_COMPAT ti_opt4060

#include "opt4060.h"
#include "opt4060_internal.h"
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_twim.h>

LOG_MODULE_REGISTER(opt4060, CONFIG_SENSOR_LOG_LEVEL);

enum opt4060_e {
	OPT4060_REG_MEASUREMENTS = 0x00,
	OPT4060_REG_CH0_MSB = 0x00,
	OPT4060_REG_CH0_LSB = 0x01,
	OPT4060_REG_CH1_MSB = 0x02,
	OPT4060_REG_CH1_LSB = 0x03,
	OPT4060_REG_CH2_MSB = 0x04,
	OPT4060_REG_CH2_LSB = 0x05,
	OPT4060_REG_CH3_MSB = 0x06,
	OPT4060_REG_CH3_LSB = 0x07,
	OPT4060_REG_CONFIG = 0x0A,
	OPT4060_REG_CONFIG2 = 0x0B,
	OPT4060_REG_FLAGS = 0x0C,
	OPT4060_REG_DEVICE_ID = 0x11,
};

#define OPT4060_VAL_DEVICE_ID (0x0821)

#define OPT4060_REG_CONFIG_MASK_QWAKE          (BIT(15))
#define OPT4060_REG_CONFIG_MASK_RANGE          (BIT(13) | BIT(12) | BIT(11) | BIT(10))
#define OPT4060_REG_CONFIG_MASK_CONV_TIME      (BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define OPT4060_REG_CONFIG_MASK_OPERATING_MODE (BIT(5) | BIT(4))
#define OPT4060_REG_CONFIG_MASK_LATCH          (BIT(3))
#define OPT4060_REG_CONFIG_MASK_INT_POL        (BIT(2))
#define OPT4060_REG_CONFIG_MASK_FAULT_CNT      (BIT(1) | BIT(0))

#define OPT4060_REG_CONFIG_VAL_QWAKE_OFF (0)
#define OPT4060_REG_CONFIG_VAL_QWAKE_ON  (BIT(15))

#define OPT4060_REG_CONFIG_SHIFT_RANGE        (10)
#define OPT4060_REG_CONFIG_VAL_RANGE_2_2_KLUX (0 << OPT4060_REG_CONFIG_SHIFT_RANGE)
#define OPT4060_REG_CONFIG_VAL_RANGE_4_5_KLUX (1 << OPT4060_REG_CONFIG_SHIFT_RANGE)
#define OPT4060_REG_CONFIG_VAL_RANGE_9_KLUX   (2 << OPT4060_REG_CONFIG_SHIFT_RANGE)
#define OPT4060_REG_CONFIG_VAL_RANGE_18_KLUX  (3 << OPT4060_REG_CONFIG_SHIFT_RANGE)
#define OPT4060_REG_CONFIG_VAL_RANGE_36_KLUX  (4 << OPT4060_REG_CONFIG_SHIFT_RANGE)
#define OPT4060_REG_CONFIG_VAL_RANGE_72_KLUX  (5 << OPT4060_REG_CONFIG_SHIFT_RANGE)
#define OPT4060_REG_CONFIG_VAL_RANGE_144_KLUX (6 << OPT4060_REG_CONFIG_SHIFT_RANGE)
#define OPT4060_REG_CONFIG_VAL_RANGE_AUTO     (12 << OPT4060_REG_CONFIG_SHIFT_RANGE)

#define OPT4060_REG_CONFIG_SHIFT_OPERATING_MODE (4)
#define OPT4060_REG_CONFIG_VAL_OPERATING_MODE_POWER_DOWN                                           \
	(0 << OPT4060_REG_CONFIG_SHIFT_OPERATING_MODE)
#define OPT4060_REG_CONFIG_VAL_OPERATING_MODE_FORCED_ONESHOT                                       \
	(1 << OPT4060_REG_CONFIG_SHIFT_OPERATING_MODE)
#define OPT4060_REG_CONFIG_VAL_OPERATING_MODE_ONESHOT (2 << OPT4060_REG_CONFIG_SHIFT_OPERATING_MODE)
#define OPT4060_REG_CONFIG_VAL_OPERATING_MODE_CONTINUOUS                                           \
	(3 << OPT4060_REG_CONFIG_SHIFT_OPERATING_MODE)

#define OPT4060_REG_CONFIG_VAL_LATCH (BIT(3))

#define OPT4060_REG_CONFIG_VAL_INT_POL_ACTIVE_LOW  (0)
#define OPT4060_REG_CONFIG_VAL_INT_POL_ACTIVE_HIGH (BIT(2))

#define OPT4060_REG_CONFIG_SHIFT_FAULT_CNT (0)
#define OPT4060_REG_CONFIG_VAL_FAULT_CNT_1 (0 << OPT4060_REG_CONFIG_SHIFT_FAULT_CNT)
#define OPT4060_REG_CONFIG_VAL_FAULT_CNT_2 (1 << OPT4060_REG_CONFIG_SHIFT_FAULT_CNT)
#define OPT4060_REG_CONFIG_VAL_FAULT_CNT_4 (2 << OPT4060_REG_CONFIG_SHIFT_FAULT_CNT)
#define OPT4060_REG_CONFIG_VAL_FAULT_CNT_8 (3 << OPT4060_REG_CONFIG_SHIFT_FAULT_CNT)

#define OPT4060_REG_CONFIG2_MASK_INT_DIR (BIT(4))
#define OPT4060_REG_CONFIG2_MASK_INT_CFG (BIT(3) | BIT(2))

#define OPT4060_REG_CONFIG2_SHIFT_INT_DIR      (4)
#define OPT4060_REG_CONFIG2_VAL_INT_DIR_INPUT  (0 << OPT4060_REG_CONFIG2_SHIFT_INT_DIR)
#define OPT4060_REG_CONFIG2_VAL_INT_DIR_OUTPUT (1 << OPT4060_REG_CONFIG2_SHIFT_INT_DIR)

#define OPT4060_REG_CONFIG2_SHIFT_INT_CFG (2)
#define OPT4060_REG_CONFIG2_VAL_INT_CFG_DATA_RDY_NEXT_CHANNEL                                      \
	(1 << OPT4060_REG_CONFIG2_SHIFT_INT_CFG)
#define OPT4060_REG_CONFIG2_VAL_INT_CFG_DATA_RDY_ALL_CHANNELS                                      \
	(3 << OPT4060_REG_CONFIG2_SHIFT_INT_CFG)

#define OPT4060_REG_FLAGS_OVERLOAD         (BIT(3))
#define OPT4060_REG_FLAGS_CONVERSION_READY (BIT(2))
#define OPT4060_REG_FLAGS_FLAG_H           (BIT(1))
#define OPT4060_REG_FLAGS_FLAG_L           (BIT(0))

#define OPT4060_OVERFLOW_MANTISSA (0x00FFFFFFU)
#define OPT4060_OVERFLOW_EXPONENT (0x0FU)

#define OPT4060_READ_CHAN_CNT_MAX_RETRIES (3)

int opt4060_i2c_write_read(const struct device *i2c_dev, uint16_t addr, const void *write_buf,
			   size_t num_write, void *read_buf, size_t num_read)
{
	struct i2c_msg msg[2];

	msg[0].buf = (uint8_t *)write_buf;
	msg[0].len = num_write;
	msg[0].flags = I2C_MSG_WRITE | I2C_MSG_RESTART;

	msg[1].buf = (uint8_t *)read_buf;
	msg[1].len = num_read;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer(i2c_dev, msg, 2, addr);
}

static bool opt4060_reg_read(const struct device *const p_dev, const enum opt4060_e reg,
			     uint16_t *const p_val)
{
	const struct opt4060_config *p_config = p_dev->config;
	uint8_t value[2] = {0};

	if (0 != i2c_burst_read_dt(&p_config->i2c, (uint8_t)reg, &value[0], sizeof(value))) {
		return false;
	}

	*p_val = ((uint16_t)value[0] << 8) + value[1];

	return true;
}

static bool opt4060_bulk_read(const struct device *const p_dev, const enum opt4060_e reg,
			      uint16_t *const p_arr, const size_t arr_len)
{
	const struct opt4060_config *p_config = p_dev->config;

	uint8_t *const p_arr_u8 = (uint8_t *)p_arr;
	if (0 != i2c_burst_read_dt(&p_config->i2c, (uint8_t)reg, &p_arr_u8[0],
				   sizeof(uint16_t) * arr_len)) {
		return false;
	}
	for (int i = 0; i < arr_len; ++i) {
		p_arr[i] = ((uint16_t)p_arr_u8[i * 2 + 0] << 8) + p_arr_u8[i * 2 + 1];
	}

	return true;
}

static bool opt4060_reg_write(const struct device *const p_dev, const enum opt4060_e reg,
			      const uint16_t val)
{
	const struct opt4060_config *p_config = p_dev->config;

	const uint8_t tx_buf[] = {(uint8_t)reg, (uint8_t)(val >> 8U), (uint8_t)(val & 0xFFU)};

	if (0 != i2c_write_dt(&p_config->i2c, tx_buf, sizeof(tx_buf))) {
		return false;
	}
	return true;
}

#ifdef CONFIG_OPT4060_INT
static bool opt4060_reg_update(const struct device *const p_dev, const enum opt4060_e reg,
			       const uint16_t mask, const uint16_t val)
{
	uint16_t old_val = 0;

	if (!opt4060_reg_read(p_dev, reg, &old_val)) {
		return false;
	}

	uint16_t new_val = old_val & ~mask;
	new_val |= val & mask;

	return opt4060_reg_write(p_dev, reg, new_val);
}
#endif

/**
 * @brief Calculate a Hamming weight of a 32-bit value
 * @param val 32-bit value
 * @return Hamming weight (sum of bits set to 1)
 */
static uint32_t hamming_weight32(const uint32_t val)
{
	uint32_t res = val - ((val >> 1) & 0x55555555U);
	res = (res & 0x33333333U) + ((res >> 2) & 0x33333333U);
	res = (res + (res >> 4U)) & 0x0F0F0F0FU;
	res = res + (res >> 8U);
	return (res + (res >> 16U)) & 0x000000FFU;
}

static uint32_t opt4060_xor(const uint8_t exp, const uint32_t mantissa, const uint8_t cnt)
{
	return (hamming_weight32(mantissa) + hamming_weight32(exp) + hamming_weight32(cnt)) % 2U;
}

/**
 * @brief Calculate a 4-bit CRC for the OPT4060 sensor
 * @param exp 4-bit exponent value
 * @param mantissa 20-bit mantissa value
 * @param cnt 4-bit counter value
 */
static uint8_t opt4060_calc_crc(const uint8_t exp, const uint32_t mantissa, const uint8_t cnt)
{
	uint8_t crc = opt4060_xor(exp, mantissa, cnt);
	crc |= opt4060_xor(exp & 0xAU, mantissa & 0xAAAAAU, cnt & 0xAU) << 1U;
	crc |= opt4060_xor(exp & 0x8U, mantissa & 0x88888U, cnt & 0x8U) << 2U;
	crc |= opt4060_xor(exp & 0x0U, mantissa & 0x80808U, cnt & 0x0U) << 3U;

	return crc;
}

static bool opt4060_decode_raw(const uint16_t raw_msb, const uint16_t raw_lsb,
			       opt4060_ch_data_t *const p_data)
{
	const uint8_t crc = raw_lsb & 0x0FU;
	p_data->exponent = (raw_msb >> 12U) & 0x0FU;
	p_data->cnt = (raw_lsb >> 4U) & 0x0FU;
	p_data->mantissa = ((uint32_t)(raw_msb & 0x0FFFU) << 8U) + (raw_lsb >> 8U);

	const uint8_t calc_crc = opt4060_calc_crc(p_data->exponent, p_data->mantissa, p_data->cnt);
	if (calc_crc != crc) {
		p_data->is_valid = false;
		return false;
	}
	p_data->is_valid = true;
	return true;
}

static void opt4060_set_invalid_for_all_channels(const struct device *const p_dev)
{
	struct opt4060_data *const p_data = p_dev->data;
	for (int chan = 0; chan < OPT4060_CHANNEL_NUM; ++chan) {
		p_data->ch_data[chan].is_valid = false;
	}
}

#if CONFIG_OPT4060_OP_MODE_ONESHOT
static void opt4060_set_overflow_for_all_channels(const struct device *const p_dev)
{
	struct opt4060_data *const p_data = p_dev->data;
	for (int chan = 0; chan < OPT4060_CHANNEL_NUM; ++chan) {
		p_data->ch_data[chan].is_valid = false;
		p_data->ch_data[chan].mantissa = OPT4060_OVERFLOW_MANTISSA;
		p_data->ch_data[chan].exponent = OPT4060_OVERFLOW_EXPONENT;
		p_data->ch_data[chan].cnt = 0;
	}
}
#endif // CONFIG_OPT4060_OP_MODE_ONESHOT

bool opt4060_read_all_channels(const struct device *const p_dev)
{
	struct opt4060_data *const p_data = p_dev->data;

	LOG_DBG("Read all channels");

	uint16_t raw_data[OPT4060_CHANNEL_NUM * 2] = {0};
	if (!opt4060_bulk_read(p_dev, OPT4060_REG_MEASUREMENTS, &raw_data[0],
			       ARRAY_SIZE(raw_data))) {
		LOG_ERR("opt4060_bulk_read failed");
		opt4060_set_invalid_for_all_channels(p_dev);
		return false;
	}
	LOG_HEXDUMP_DBG(raw_data, sizeof(raw_data), "Raw measurements");

	for (int chan = 0; chan < OPT4060_CHANNEL_NUM; ++chan) {
		if (!opt4060_decode_raw(raw_data[chan * 2], raw_data[chan * 2 + 1],
					&p_data->ch_data[chan])) {
			LOG_ERR("OPT4060 channel %d: CRC error", chan);
			continue;
		}
		LOG_DBG("channel %d: exponent %d, mantissa %d, cnt %d", chan,
			p_data->ch_data[chan].exponent, p_data->ch_data[chan].mantissa,
			p_data->ch_data[chan].cnt);
	}

	return true;
}

static int opt4060_read_one_channel(const struct device *const p_dev,
				    const enum sensor_channel sensor_chan)
{
	struct opt4060_data *const p_data = p_dev->data;

	int chan_idx = 0;
	enum opt4060_e reg = OPT4060_REG_CH0_MSB;
	switch (sensor_chan) {
	case SENSOR_CHAN_RED:
		reg = OPT4060_REG_CH0_MSB;
		chan_idx = 0;
		break;
	case SENSOR_CHAN_GREEN:
		reg = OPT4060_REG_CH1_MSB;
		chan_idx = 1;
		break;
	case SENSOR_CHAN_BLUE:
		reg = OPT4060_REG_CH2_MSB;
		chan_idx = 2;
		break;
	case SENSOR_CHAN_LIGHT:
		reg = OPT4060_REG_CH3_MSB;
		chan_idx = 3;
		break;
	default:
		LOG_ERR("Unsupported sensor channel %d", sensor_chan);
		return -ENOTSUP;
	}
	LOG_DBG("Read one channel %d", chan_idx);

	struct opt4060_ch_data *const p_ch_data = &p_data->ch_data[chan_idx];

	uint16_t raw_data[2] = {0};
	if (!opt4060_bulk_read(p_dev, reg, &raw_data[0], ARRAY_SIZE(raw_data))) {
		LOG_ERR("opt4060_bulk_read failed");
		p_ch_data->is_valid = false;
		return -EIO;
	}
	LOG_HEXDUMP_DBG(raw_data, sizeof(raw_data), "Raw measurements");

	if (!opt4060_decode_raw(raw_data[0], raw_data[1], p_ch_data)) {
		LOG_DBG("OPT4060 channel %d: CRC error", chan_idx);
		p_ch_data->is_valid = false;
		return -EAGAIN;
	}
	LOG_DBG("channel %d: exponent %d, mantissa %d, cnt %d", chan_idx, p_ch_data->exponent,
		p_ch_data->mantissa, p_ch_data->cnt);

	return 0;
}

#if CONFIG_OPT4060_TRIGGER
static int opt4060_trigger_drdy_set(const struct device *const p_dev,
				    const struct sensor_trigger *const p_trig,
				    sensor_trigger_handler_t handler)
{
	struct opt4060_data *const p_data = p_dev->data;

	if (handler == NULL) {
		return -EINVAL;
	}

	p_data->handler_drdy = handler;
	p_data->p_trig_drdy = p_trig;

	return 0;
}

static int opt4060_trigger_set(const struct device *const p_dev,
			       const struct sensor_trigger *const p_trig,
			       sensor_trigger_handler_t handler)
{
	if (p_trig->type == SENSOR_TRIG_DATA_READY && p_trig->chan == SENSOR_CHAN_LIGHT) {
		return opt4060_trigger_drdy_set(p_dev, p_trig, handler);
	}

	return -ENOTSUP;
}
#endif

static int opt4060_sample_fetch(const struct device *const p_dev, const enum sensor_channel chan)
{
	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL);

#if CONFIG_OPT4060_OP_MODE_ONESHOT
	struct opt4060_data *const p_data = p_dev->data;
	if (p_data->flag_one_shot_started) {
		uint16_t flags = 0;
		if (!opt4060_reg_read(p_dev, OPT4060_REG_FLAGS, &flags)) {
			LOG_ERR("Failed to read REG_FLAGS");
			return -EIO;
		}
		LOG_DBG("REG_FLAGS: 0x%04x", flags);
		if (0 == (flags & OPT4060_REG_FLAGS_CONVERSION_READY)) {
			LOG_ERR("Conversion is in progress");
			return -EBUSY;
		}
		p_data->flag_one_shot_started = false;
	}
	LOG_DBG("Start one-shot conversion");
	p_data->flag_one_shot_started = true;
	if (!opt4060_reg_write(p_dev, OPT4060_REG_CONFIG, p_data->cfg_reg)) {
		LOG_ERR("Failed to start one-shot conversion");
		p_data->flag_one_shot_started = false;
		return -EIO;
	}
#else
	LOG_DBG("Continuous mode is enabled, no need to start conversion");
	return 0;
#endif
}

static int opt4060_channel_get(const struct device *const p_dev, const enum sensor_channel chan,
			       struct sensor_value *const p_val)
{
	struct opt4060_data *const p_data = p_dev->data;

#if CONFIG_OPT4060_OP_MODE_ONESHOT
	if (p_data->flag_one_shot_started) {
		uint16_t flags = 0;
		if (!opt4060_reg_read(p_dev, OPT4060_REG_FLAGS, &flags)) {
			LOG_ERR("Failed to read REG_FLAGS");
			opt4060_set_invalid_for_all_channels(p_dev);
			return -EIO;
		}
		LOG_DBG("REG_FLAGS: 0x%04x", flags);
		if (0 == (flags & OPT4060_REG_FLAGS_CONVERSION_READY)) {
			LOG_DBG("Conversion is not ready");
			return -EBUSY;
		}
		p_data->flag_one_shot_started = false;

		if (0 != (flags & OPT4060_REG_FLAGS_OVERLOAD)) {
			LOG_DBG("Overload detected");
			opt4060_set_overflow_for_all_channels(p_dev);
		} else {
			LOG_DBG("No overload");
			if (!opt4060_read_all_channels(p_dev)) {
				LOG_ERR("Failed to read last data");
				opt4060_set_invalid_for_all_channels(p_dev);
			}
		}
	}
#else
	int res = opt4060_read_one_channel(p_dev, chan);
	if (0 != res) {
		LOG_DBG("Failed to read last data, res=%d", res);
		return res;
	}
#endif

	int chan_idx = 0;
	uint32_t k = 0;
	switch (chan) {
	case SENSOR_CHAN_RED:
		chan_idx = OPT4060_CHANNEL_RED;
		k = 24;
		break;
	case SENSOR_CHAN_GREEN:
		chan_idx = OPT4060_CHANNEL_GREEN;
		k = 10;
		break;
	case SENSOR_CHAN_BLUE:
		chan_idx = OPT4060_CHANNEL_BLUE;
		k = 13;
		break;
	case SENSOR_CHAN_LIGHT:
		chan_idx = OPT4060_CHANNEL_LUMINOSITY;
		k = 0;
		break;

	default:
		return -ENOTSUP;
	}

	const opt4060_ch_data_t *const p_ch_data = &p_data->ch_data[chan_idx];
	if (!p_ch_data->is_valid) {
		if ((p_ch_data->mantissa == OPT4060_OVERFLOW_MANTISSA) &&
		    (p_ch_data->exponent == OPT4060_OVERFLOW_EXPONENT)) {
			LOG_DBG("Channel %d: overflow", chan);
			return -ERANGE;
		}
		LOG_DBG("Channel %d: no valid data", chan);
		return -EIO;
	}

	uint64_t uval = p_ch_data->mantissa;
	uval <<= p_ch_data->exponent;
	if (0 != k) {
		const uint64_t tmp_val = uval * k;
		p_val->val1 = tmp_val / 10;
		p_val->val2 = (tmp_val % 10) * 100000;
		p_val->val2 &= ~OPT4060_CHAN_CNT_MASK;
		p_val->val2 |= p_ch_data->cnt & OPT4060_CHAN_CNT_MASK;
	} else {
		// k = 0.00215 = 43 / 20000
		const uint64_t tmp_val = uval * 43;
		p_val->val1 = tmp_val / 20000;
		p_val->val2 = (tmp_val % 20000) * 50;
	}
	// Encode cnt in the least significant 4 bits of val2
	p_val->val2 &= ~OPT4060_CHAN_CNT_MASK;
	p_val->val2 |= p_ch_data->cnt & OPT4060_CHAN_CNT_MASK;
	LOG_DBG("Fetch channel %d: exponent %d, mantissa %d, cnt %d, uval %lld, val: %d.%06d", chan,
		p_ch_data->exponent, p_ch_data->mantissa, p_ch_data->cnt, uval, p_val->val1,
		p_val->val2);

	return 0;
}

static const struct sensor_driver_api opt4060_driver_api = {
#if CONFIG_OPT4060_TRIGGER
	.trigger_set = &opt4060_trigger_set,
#endif
	.sample_fetch = &opt4060_sample_fetch,
	.channel_get = &opt4060_channel_get,
};

uint32_t opt4060_get_conv_time_us(void)
{
#if CONFIG_OPT4060_CONV_TIME_600_US
	return 600;
#elif CONFIG_OPT4060_CONV_TIME_1_MS
	return 1000;
#elif CONFIG_OPT4060_CONV_TIME_1_8_MS
	return 1800;
#elif CONFIG_OPT4060_CONV_TIME_3_4_MS
	return 3400;
#elif CONFIG_OPT4060_CONV_TIME_6_5_MS
	return 6500;
#elif CONFIG_OPT4060_CONV_TIME_12_7_MS
	return 12700;
#elif CONFIG_OPT4060_CONV_TIME_25_MS
	return 25000;
#elif CONFIG_OPT4060_CONV_TIME_50_MS
	return 50000;
#elif CONFIG_OPT4060_CONV_TIME_100_MS
	return 100000;
#elif CONFIG_OPT4060_CONV_TIME_200_MS
	return 200000;
#elif CONFIG_OPT4060_CONV_TIME_400_MS
	return 400000;
#elif CONFIG_OPT4060_CONV_TIME_800_MS
	return 800000;
#else
#error "Unsupported conversion time"
#endif
}

static int opt4060_read_chan_cnt(const struct device *const p_dev, const enum sensor_channel chan)
{
	struct opt4060_data *const p_data = p_dev->data;
	for (int i = 0; i < OPT4060_READ_CHAN_CNT_MAX_RETRIES; ++i) {
		struct sensor_value val = {0};
		const int64_t t1 = k_uptime_ticks();
		int res = sensor_channel_get(p_dev, chan, &val);
		const int64_t t2 = k_uptime_ticks();
		if (0 == res) {
			const int cnt = val.val2 & OPT4060_CHAN_CNT_MASK;
			val.val2 &= ~OPT4060_CHAN_CNT_MASK;
			// LOG_INF("Measured: %d.%06u, cnt=%d", val.val1, val.val2, cnt);
			if (p_data->sensor_channel_get_cnt < 10) {
				p_data->sensor_channel_get_cnt += 1;
				p_data->sensor_channel_get_accum_time += t2 - t1;
			}
			return cnt;
		}
		if (-EAGAIN != res) {
			LOG_ERR("%s: sensor_channel_get failed: %d", __func__, res);
			return res;
		}
	}
	LOG_ERR("%s:%d", __FILE__, __LINE__);
	return -EIO;
}

static bool opt4060_wait_for_next_chan_cnt(const struct device *const p_dev,
					   const enum sensor_channel chan, const int cur_cnt)
{
	const int next_cnt = (cur_cnt + 1) & OPT4060_CHAN_CNT_MASK;
	const uint32_t timeout_us = (opt4060_get_conv_time_us() * 4 * 3 / 2 + 1000) * 10;
	const uint64_t timeout_ticks = k_us_to_ticks_ceil32(timeout_us);
	const int64_t time_start = k_uptime_ticks();
	while (1) {
		const int res = opt4060_read_chan_cnt(p_dev, chan);
		if (res < 0) {
			LOG_ERR("%s:%d", __FILE__, __LINE__);
			return false;
		}
		if (res == next_cnt) {
			break;
		}
		if (res != cur_cnt) {
			LOG_ERR("%s:%d: res=%d, cur_cnt=%d, next_cnt=%d", __FILE__, __LINE__, res,
				cur_cnt, next_cnt);
			return false;
		}
		const int64_t time_end = k_uptime_ticks();
		if ((time_end - time_start) > timeout_ticks) {
			LOG_ERR("%s:%d: time_start=%" PRId64 ", time_end=%" PRId64
				", delta=%" PRId64 ", timeout_us=%" PRIu32
				", timeout_ticks=%" PRId64,
				__FILE__, __LINE__, time_start, time_end, time_end - time_start,
				timeout_us, timeout_ticks);
			return false;
		}
	}
	return true;
}

static bool opt4060_measure_period(const struct device *const p_dev)
{
	struct opt4060_data *const p_data = p_dev->data;
	const enum sensor_channel chan = SENSOR_CHAN_GREEN;
	const uint32_t conv_time_us = opt4060_get_conv_time_us();
	const uint32_t max_wait_time_us =
		(10000000 < (conv_time_us * 16)) ? 10000000 : (conv_time_us * 16);
	const int64_t max_wait_time_ticks = k_us_to_ticks_ceil32(max_wait_time_us);
	uint32_t cycle_cnt = 0;

	int cur_cnt = opt4060_read_chan_cnt(p_dev, chan);
	if (cur_cnt < 0) {
		LOG_ERR("%s:%d", __FILE__, __LINE__);
		return false;
	}
	if (!opt4060_wait_for_next_chan_cnt(p_dev, chan, cur_cnt)) {
		LOG_ERR("%s:%d", __FILE__, __LINE__);
		return false;
	}
	const int64_t time_start = k_uptime_ticks();
	while (1) {
		cycle_cnt += 1;
		cur_cnt += 1;
		cur_cnt &= OPT4060_CHAN_CNT_MASK;
		if (!opt4060_wait_for_next_chan_cnt(p_dev, chan, cur_cnt)) {
			LOG_ERR("%s:%d", __FILE__, __LINE__);
			return false;
		}
		const int64_t delta_time_ticks = k_uptime_ticks() - time_start;
		if (delta_time_ticks > max_wait_time_ticks) {
			p_data->one_measurement_duration_ticks =
				(int32_t)((delta_time_ticks + (cycle_cnt * 4 / 2)) /
					  (cycle_cnt * 4));
			p_data->sensor_channel_get_duration_ticks =
				(int32_t)((p_data->sensor_channel_get_accum_time +
					   (p_data->sensor_channel_get_cnt / 2)) /
					  p_data->sensor_channel_get_cnt);
			LOG_INF("Configured conv time: %" PRIu32 " us", conv_time_us);
			LOG_INF("Max wait time: %" PRIu32 " us, %" PRId64 " ticks",
				max_wait_time_us, max_wait_time_ticks);
			LOG_INF("Delta time: %" PRId64 " ticks, cycle_cnt=%" PRIu32,
				delta_time_ticks, cycle_cnt);
			LOG_INF("Measured duration of one channel measurement: %" PRId32 " ticks",
				p_data->one_measurement_duration_ticks);
			LOG_INF("Measured duration of one sensor_channel_get: %" PRId32 " ticks",
				p_data->sensor_channel_get_duration_ticks);
			return true;
		}
	}
}

int32_t opt4060_get_one_measurement_duration_ticks(const struct device *const p_dev)
{
	struct opt4060_data *const p_data = p_dev->data;
	return p_data->one_measurement_duration_ticks;
}

static void opt4060_set_fast_speed_i2c(void)
{
	static NRF_TWIM_Type *const g_p_twim = (NRF_TWIM_Type *)0x40003000;
	/*
		https://docs.nordicsemi.com/bundle/errata_nRF52840_Rev3/page/ERR/nRF52840/Rev3/latest/anomaly_840_219.html#anomaly_840_219

		Symptom:
		The low period of the SCL clock is too short to meet the I2C specification at 400
	   kHz. The actual low period of the SCL clock is 1.25 µs while the I2C specification
	   requires the SCL clock to have a minimum low period of 1.3 µs.

		Workaround:
		If communication does not work at 400 kHz with an I2C compatible device that
	   requires the SCL clock to have a minimum low period of 1.3 µs, use 390 kHz instead of
	   400kHz by writing 0x06200000 to the FREQUENCY register. With this setting, the SCL low
	   period is greater than 1.3 µs.
	*/
	// nrf_twim_frequency_set(g_p_twim, NRF_TWIM_FREQ_400K); // Set TWI frequency to 400 kHz
	// (FREQ=0x06400000UL)
	nrf_twim_frequency_set(g_p_twim, 0x06200000); // Set TWI frequency to 390 kHz
}

static int opt4060_init(const struct device *const p_dev)
{
	const struct opt4060_config *const p_config = p_dev->config;
	struct opt4060_data *const p_data = p_dev->data;

	LOG_DBG("Init OPT4060, addr=0x%02x", p_config->i2c.addr);

	if (!device_is_ready(p_config->i2c.bus)) {
		LOG_ERR("I2C bus is not ready");
		return -ENODEV;
	}

	uint16_t value = 0;
	if (!opt4060_reg_read(p_dev, OPT4060_REG_DEVICE_ID, &value)) {
		LOG_ERR("Failed to read device id");
		return -EIO;
	}

	if (OPT4060_VAL_DEVICE_ID != value) {
		LOG_ERR("Bad device id 0x%x", value);
		return -ENOTSUP;
	}

#ifdef CONFIG_OPT4060_INT
	if (p_config->gpio_int.port != NULL) {
		const int res = opt4060_init_interrupt(p_dev);
		if (0 != res) {
			LOG_ERR("Failed to initialize interrupts.");
			return res;
		}
	}
#endif

	p_data->cfg_reg = OPT4060_REG_CONFIG_VAL_LATCH | OPT4060_REG_CONFIG_VAL_INT_POL_ACTIVE_LOW;

#if CONFIG_OPT4060_QUICK_WAKEUP
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_QWAKE_ON;
#endif

#if CONFIG_OPT4060_OP_MODE_ONESHOT
#if CONFIG_OPT4060_OP_MODE_ONE_SHOT_FORCED_AUTO_RANGE
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_OPERATING_MODE_FORCED_ONESHOT;
#else
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_OPERATING_MODE_ONESHOT;
#endif
#else
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_OPERATING_MODE_CONTINUOUS;
#endif

	p_data->cfg_reg |=
#if CONFIG_OPT4060_RANGE_AUTO
		OPT4060_REG_CONFIG_VAL_RANGE_AUTO;
#elif CONFIG_OPT4060_RANGE_2254_LUX
		OPT4060_REG_CONFIG_VAL_RANGE_2_2_KLUX;
#elif CONFIG_OPT4060_RANGE_4509_LUX
		OPT4060_REG_CONFIG_VAL_RANGE_4_5_KLUX;
#elif CONFIG_OPT4060_RANGE_9018_LUX
		OPT4060_REG_CONFIG_VAL_RANGE_9_KLUX;
#elif CONFIG_OPT4060_RANGE_18036_LUX
		OPT4060_REG_CONFIG_VAL_RANGE_18_KLUX;
#elif CONFIG_OPT4060_RANGE_36071_LUX
		OPT4060_REG_CONFIG_VAL_RANGE_36_KLUX;
#elif CONFIG_OPT4060_RANGE_72142_LUX
		OPT4060_REG_CONFIG_VAL_RANGE_72_KLUX;
#elif CONFIG_OPT4060_RANGE_144284_LUX
		OPT4060_REG_CONFIG_VAL_RANGE_144_KLUX;
#else
#error "Unsupported range"
#endif

	p_data->cfg_reg |=
#if CONFIG_OPT4060_CONV_TIME_600_US
		OPT4060_REG_CONFIG_VAL_CONV_TIME_600_US;
#elif CONFIG_OPT4060_CONV_TIME_1_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_1_MS;
#elif CONFIG_OPT4060_CONV_TIME_1_8_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_1_8_MS;
#elif CONFIG_OPT4060_CONV_TIME_3_4_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_3_4_MS;
#elif CONFIG_OPT4060_CONV_TIME_6_5_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_6_5_MS;
#elif CONFIG_OPT4060_CONV_TIME_12_7_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_12_7_MS;
#elif CONFIG_OPT4060_CONV_TIME_25_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_25_MS;
#elif CONFIG_OPT4060_CONV_TIME_50_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_50_MS;
#elif CONFIG_OPT4060_CONV_TIME_100_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_100_MS;
#elif CONFIG_OPT4060_CONV_TIME_200_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_200_MS;
#elif CONFIG_OPT4060_CONV_TIME_400_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_400_MS;
#elif CONFIG_OPT4060_CONV_TIME_800_MS
		OPT4060_REG_CONFIG_VAL_CONV_TIME_800_MS;
#else
#error "Unsupported conversion time"
#endif

#ifdef CONFIG_OPT4060_INT
	uint16_t int_flags = OPT4060_REG_CONFIG2_VAL_INT_DIR_OUTPUT;
#if CONFIG_OPT4060_INT_DATA_READY_FOR_ALL_CHANNELS
	int_flags |= OPT4060_REG_CONFIG2_VAL_INT_CFG_DATA_RDY_ALL_CHANNELS;
#else
	int_flags |= OPT4060_REG_CONFIG2_VAL_INT_CFG_DATA_RDY_NEXT_CHANNEL;
#endif
	if (!opt4060_reg_update(p_dev, OPT4060_REG_CONFIG2,
				OPT4060_REG_CONFIG2_MASK_INT_DIR | OPT4060_REG_CONFIG2_MASK_INT_CFG,
				int_flags)) {
		LOG_ERR("Failed to configure interrupt mode");
		return -EIO;
	}
#endif

	LOG_DBG("REG_CONFIG: 0x%04x", p_data->cfg_reg);

#if CONFIG_OPT4060_OP_MODE_ONESHOT
	p_data->flag_one_shot_started = false;
#else
	if (!opt4060_reg_write(p_dev, OPT4060_REG_CONFIG, p_data->cfg_reg)) {
		LOG_ERR("Failed to configure sensor");
		return -EIO;
	}

	int res = 0;

#if 0
	res = i2c_configure(p_config->i2c.bus, I2C_SPEED_SET(I2C_SPEED_FAST));
	if (0 != res) {
		LOG_ERR("Failed to set I2C_SPEED_FAST");
		return -EIO;
	}
#else
	opt4060_set_fast_speed_i2c();
#endif

	const bool res_period_measurement = opt4060_measure_period(p_dev);

	res = i2c_configure(p_config->i2c.bus, I2C_SPEED_SET(I2C_SPEED_STANDARD));
	if (0 != res) {
		LOG_ERR("Failed to set I2C_SPEED_STANDARD");
		return -EIO;
	}

	if (!res_period_measurement) {
		LOG_ERR("Failed to measure period of one channel measurement");
		return -EIO;
	}
#endif

	return 0;
}

int opt4060_configure_conv_time(const struct device *const p_dev, const uint16_t conv_time)
{
	struct opt4060_data *const p_data = p_dev->data;

	p_data->cfg_reg &= ~OPT4060_REG_CONFIG_VAL_CONV_TIME_MASK;
	p_data->cfg_reg |= conv_time;

	if (!opt4060_reg_write(p_dev, OPT4060_REG_CONFIG, p_data->cfg_reg)) {
		LOG_ERR("Failed to configure sensor");
		return -EIO;
	}
	return 0;
}

#if CONFIG_OPT4060_INT
#define OPT4060_CFG_INT(inst) .gpio_int = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, irq_gpios, 0),
#else
#define OPT4060_CFG_INT(inst)
#endif

#define OPT4060_DEFINE(inst)                                                                       \
	static struct opt4060_data opt4060_data_##inst;                                            \
                                                                                                   \
	static const struct opt4060_config opt4060_config_##inst = {                               \
		.i2c = I2C_DT_SPEC_INST_GET(inst), OPT4060_CFG_INT(inst)};                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, opt4060_init, NULL, &opt4060_data_##inst,               \
				     &opt4060_config_##inst, POST_KERNEL,                          \
				     CONFIG_SENSOR_INIT_PRIORITY, &opt4060_driver_api);

DT_INST_FOREACH_STATUS_OKAY(OPT4060_DEFINE)
