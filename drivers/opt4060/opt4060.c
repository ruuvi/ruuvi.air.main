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

#define NRF_TWIM_FREQ_390K 0x06200000UL

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

#define OPT4060_I2C_MSG_RX_TX_ARRAY_SIZE (2)

#if !defined(BITS_PER_BYTE)
#define BITS_PER_BYTE (8U)
#else
_Static_assert(BITS_PER_BYTE == 8U, "BITS_PER_BYTE != 8"); // NOSONAR
#endif

#if !defined(BYTE_MASK)
#define BYTE_MASK (0xFFU)
#else
_Static_assert(BYTE_MASK == 0xFFU, "BYTE_MASK != 0xFFU"); // NOSONAR
#endif

#if !defined(UINT16_NUM_BYTES)
#define UINT16_NUM_BYTES (2U)
#else
_Static_assert(UINT16_NUM_BYTES == 2U, "UINT16_NUM_BYTES != 2"); // NOSONAR
#endif

#define HAMMING_WEIGHT32_MASK_1  0x55555555U /* 0101... */
#define HAMMING_WEIGHT32_MASK_2  0x33333333U /* 0011... */
#define HAMMING_WEIGHT32_MASK_4  0x0F0F0F0FU /* 00001111... */
#define HAMMING_WEIGHT32_MASK_16 0x0000FFFFU

#define HAMMING_WEIGHT32_SHIFT_1  1U
#define HAMMING_WEIGHT32_SHIFT_2  2U
#define HAMMING_WEIGHT32_SHIFT_4  4U
#define HAMMING_WEIGHT32_SHIFT_8  8U
#define HAMMING_WEIGHT32_SHIFT_16 16U

#define PARITY_MODULUS 2U

#define SENSOR_VALUE_FRACTIONAL_MULTIPLIER 1000000UL

#define OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_RED          24U
#define OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_GREEN        10U
#define OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_BLUE         13U
#define OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_RGB        10U
#define OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_LUMINOSITY   43U
#define OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_LUMINOSITY 20000U

#define OPT4060_MAX_MEASURE_PERIOD_US (10U * 1000U * 1000U)

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

opt4060_ret_t opt4060_i2c_write_read(const struct device *i2c_dev, uint16_t addr,
				     const void *write_buf, size_t num_write, void *read_buf,
				     size_t num_read)
{
	const uint8_t *const p_wbuf = write_buf;
	uint8_t *const p_rbuf = read_buf;

	struct i2c_msg msg[OPT4060_I2C_MSG_RX_TX_ARRAY_SIZE] = {
		[0] =
			{
				.buf = (uint8_t *)p_wbuf, // NOSONAR: cast required by Zephyr I2C
							  // API, buffer not modified
				.len = num_write,
				.flags = I2C_MSG_WRITE | I2C_MSG_RESTART,
			},
		[1] =
			{
				.buf = p_rbuf,
				.len = num_read,
				.flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP,
			},
	};

	return i2c_transfer(i2c_dev, msg, ARRAY_SIZE(msg), addr);
}

static bool opt4060_reg_read(const struct device *const p_dev, const enum opt4060_e reg,
			     uint16_t *const p_val)
{
	const opt4060_config_t *p_config = p_dev->config;
	uint8_t value[2] = {0};

	if (0 != i2c_burst_read_dt(&p_config->i2c, (uint8_t)reg, &value[0], sizeof(value))) {
		return false;
	}

	*p_val = (uint16_t)((uint16_t)value[0] << BITS_PER_BYTE) + value[1];

	return true;
}

static bool opt4060_bulk_read(const struct device *const p_dev, const enum opt4060_e reg,
			      uint16_t *const p_arr, const size_t arr_len)
{
	const opt4060_config_t *p_config = p_dev->config;

	uint8_t *const p_arr_u8 = (uint8_t *)p_arr;
	if (0 != i2c_burst_read_dt(&p_config->i2c, (uint8_t)reg, &p_arr_u8[0],
				   sizeof(uint16_t) * arr_len)) {
		return false;
	}
	for (int32_t i = 0; i < arr_len; ++i) {
		p_arr[i] = (uint16_t)((uint16_t)p_arr_u8[i * UINT16_NUM_BYTES] << BITS_PER_BYTE) +
			   p_arr_u8[(i * UINT16_NUM_BYTES) + 1];
	}

	return true;
}

static bool opt4060_reg_write(const struct device *const p_dev, const enum opt4060_e reg,
			      const uint16_t val)
{
	const opt4060_config_t *p_config = p_dev->config;

	const uint8_t tx_buf[] = {(uint8_t)reg, (uint8_t)(val >> BITS_PER_BYTE),
				  (uint8_t)(val & BYTE_MASK)};

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
	uint32_t res = val - ((val >> HAMMING_WEIGHT32_SHIFT_1) & HAMMING_WEIGHT32_MASK_1);
	res = (res & HAMMING_WEIGHT32_MASK_2) +
	      ((res >> HAMMING_WEIGHT32_SHIFT_2) & HAMMING_WEIGHT32_MASK_2);
	res = (res + (res >> HAMMING_WEIGHT32_SHIFT_4)) & HAMMING_WEIGHT32_MASK_4;
	res = res + (res >> HAMMING_WEIGHT32_SHIFT_8);
	return (res + (res >> HAMMING_WEIGHT32_SHIFT_16)) & HAMMING_WEIGHT32_MASK_16;
}

static uint32_t opt4060_xor(const uint8_t exp, const uint32_t mantissa, const uint8_t cnt)
{
	return (hamming_weight32(mantissa) + hamming_weight32(exp) + hamming_weight32(cnt)) %
	       PARITY_MODULUS;
}

/**
 * @brief Calculate a 4-bit CRC for the OPT4060 sensor
 * @param exp 4-bit exponent value
 * @param mantissa 20-bit mantissa value
 * @param cnt 4-bit counter value
 */
static uint8_t opt4060_calc_crc(const uint8_t exp, const uint32_t mantissa, const uint8_t cnt)
{
	uint8_t crc = (uint8_t)opt4060_xor(exp, mantissa, cnt);
	crc |= (uint8_t)opt4060_xor(exp & 0xAU, mantissa & 0xAAAAAU, cnt & 0xAU) << 1U; // NOSONAR
	crc |= (uint8_t)opt4060_xor(exp & 0x8U, mantissa & 0x88888U, cnt & 0x8U) << 2U; // NOSONAR
	crc |= (uint8_t)opt4060_xor(exp & 0x0U, mantissa & 0x80808U, cnt & 0x0U) << 3U; // NOSONAR

	return crc;
}

static bool opt4060_decode_raw(const uint16_t raw_msb, const uint16_t raw_lsb,
			       opt4060_ch_data_t *const p_data)
{
	const uint8_t crc = raw_lsb & 0x0FU;                                  // NOSONAR
	p_data->exponent = (raw_msb >> 12U) & 0x0FU;                          // NOSONAR
	p_data->cnt = (raw_lsb >> 4U) & 0x0FU;                                // NOSONAR
	p_data->mantissa = ((uint32_t)(raw_msb & 0x0FFFU) << BITS_PER_BYTE) + // NOSONAR
			   (raw_lsb >> BITS_PER_BYTE);

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
	opt4060_data_t *const p_data = p_dev->data;
	for (int32_t chan = 0; chan < OPT4060_CHANNEL_NUM; ++chan) {
		p_data->ch_data[chan].is_valid = false;
	}
}

#if defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
static void opt4060_set_overflow_for_all_channels(const struct device *const p_dev)
{
	opt4060_data_t *const p_data = p_dev->data;
	for (int chan = 0; chan < OPT4060_CHANNEL_NUM; ++chan) {
		p_data->ch_data[chan].is_valid = false;
		p_data->ch_data[chan].mantissa = OPT4060_OVERFLOW_MANTISSA;
		p_data->ch_data[chan].exponent = OPT4060_OVERFLOW_EXPONENT;
		p_data->ch_data[chan].cnt = 0;
	}
}
#endif // defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT

bool opt4060_read_all_channels(const struct device *const p_dev)
{
	opt4060_data_t *const p_data = p_dev->data;

	LOG_DBG("Read all channels");

	uint16_t raw_data[OPT4060_CHANNEL_NUM * 2] = {0};
	if (!opt4060_bulk_read(p_dev, OPT4060_REG_MEASUREMENTS, &raw_data[0],
			       ARRAY_SIZE(raw_data))) {
		LOG_ERR("opt4060_bulk_read failed");
		opt4060_set_invalid_for_all_channels(p_dev);
		return false;
	}
	LOG_HEXDUMP_DBG(raw_data, sizeof(raw_data), "Raw measurements");

	for (int32_t chan = 0; chan < OPT4060_CHANNEL_NUM; ++chan) {
		if (!opt4060_decode_raw(raw_data[chan * UINT16_NUM_BYTES],
					raw_data[(chan * UINT16_NUM_BYTES) + 1],
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

static opt4060_ret_t opt4060_read_one_channel(const struct device *const p_dev,
					      const enum sensor_channel sensor_chan)
{
	opt4060_data_t *const p_data = p_dev->data;

	enum opt4060_channel_e chan_idx = OPT4060_CHANNEL_RED;
	enum opt4060_e reg = OPT4060_REG_CH0_MSB;
	switch (sensor_chan) {
	case SENSOR_CHAN_RED:
		reg = OPT4060_REG_CH0_MSB;
		chan_idx = OPT4060_CHANNEL_RED;
		break;
	case SENSOR_CHAN_GREEN:
		reg = OPT4060_REG_CH1_MSB;
		chan_idx = OPT4060_CHANNEL_GREEN;
		break;
	case SENSOR_CHAN_BLUE:
		reg = OPT4060_REG_CH2_MSB;
		chan_idx = OPT4060_CHANNEL_BLUE;
		break;
	case SENSOR_CHAN_LIGHT:
		reg = OPT4060_REG_CH3_MSB;
		chan_idx = OPT4060_CHANNEL_LUMINOSITY;
		break;
	default:
		LOG_ERR("Unsupported sensor channel %d", sensor_chan);
		return -ENOTSUP;
	}
	LOG_DBG("Read one channel %d", chan_idx);

	opt4060_ch_data_t *const p_ch_data = &p_data->ch_data[chan_idx];

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

#if defined(CONFIG_OPT4060_TRIGGER) && CONFIG_OPT4060_TRIGGER
static int opt4060_trigger_drdy_set(const struct device *const p_dev,
				    const struct sensor_trigger *const p_trig,
				    sensor_trigger_handler_t handler)
{
	opt4060_data_t *const p_data = p_dev->data;

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
#endif // defined(CONFIG_OPT4060_TRIGGER) && CONFIG_OPT4060_TRIGGER

static int opt4060_sample_fetch(const struct device *const p_dev, // NOSONAR: Zephyr API
				const enum sensor_channel chan)
{
	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL);

#if defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
	opt4060_data_t *const p_data = p_dev->data;
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
#else  // defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
	LOG_DBG("Continuous mode is enabled, no need to start conversion");
	return 0;
#endif // defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
}

static int // NOSONAR: Zephyr API
opt4060_channel_get(const struct device *const p_dev, const enum sensor_channel chan,
		    struct sensor_value *const p_val)
{
#if defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
	opt4060_data_t *const p_data = p_dev->data;
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
#else  // defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
	const opt4060_data_t *const p_data = p_dev->data;
	opt4060_ret_t res = opt4060_read_one_channel(p_dev, chan);
	if (0 != res) {
		LOG_DBG("Failed to read last data, res=%d", res);
		return res;
	}
#endif // defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT

	int32_t chan_idx = 0;
	uint32_t k = 0;
	switch (chan) {
	case SENSOR_CHAN_RED:
		chan_idx = OPT4060_CHANNEL_RED;
		k = OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_RED;
		break;
	case SENSOR_CHAN_GREEN:
		chan_idx = OPT4060_CHANNEL_GREEN;
		k = OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_GREEN;
		break;
	case SENSOR_CHAN_BLUE:
		chan_idx = OPT4060_CHANNEL_BLUE;
		k = OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_BLUE;
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
		if ((OPT4060_OVERFLOW_MANTISSA == p_ch_data->mantissa) &&
		    (OPT4060_OVERFLOW_EXPONENT == p_ch_data->exponent)) {
			LOG_DBG("Channel %d: overflow", chan);
			return -ERANGE;
		}
		LOG_DBG("Channel %d: no valid data", chan);
		return -EIO;
	}

	uint64_t uval = p_ch_data->mantissa;
	uval <<= p_ch_data->exponent;
	if (OPT4060_CHANNEL_LUMINOSITY != chan_idx) {
		const uint64_t tmp_val = uval * k;
		p_val->val1 = (uint32_t)(tmp_val / OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_RGB);
		p_val->val2 = (tmp_val % OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_RGB) *
			      (SENSOR_VALUE_FRACTIONAL_MULTIPLIER /
			       OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_RGB);
	} else {
		// k = 0.00215 = 43 / 20000
		const uint64_t tmp_val =
			uval * OPT4060_CHAN_NORMALIZATION_COEF_NUMERATOR_LUMINOSITY;
		p_val->val1 = (uint32_t)(tmp_val /
					 OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_LUMINOSITY);
		p_val->val2 = (tmp_val % OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_LUMINOSITY) *
			      (SENSOR_VALUE_FRACTIONAL_MULTIPLIER /
			       OPT4060_CHAN_NORMALIZATION_COEF_DENOMINATOR_LUMINOSITY);
	}
	// Encode cnt in the least significant 4 bits of val2
	uint32_t tmp_val2 = (uint32_t)p_val->val2;
	tmp_val2 &= ~OPT4060_MEASUREMENT_CNT_MASK;
	tmp_val2 |= p_ch_data->cnt & OPT4060_MEASUREMENT_CNT_MASK;
	p_val->val2 = tmp_val2;
	LOG_DBG("Fetch channel %d: exponent %d, mantissa %d, cnt %d, uval %lld, val: %d.%06d", chan,
		p_ch_data->exponent, p_ch_data->mantissa, p_ch_data->cnt, uval, p_val->val1,
		p_val->val2);

	return 0;
}

static const struct sensor_driver_api opt4060_driver_api = {
#if defined(CONFIG_OPT4060_TRIGGER) && CONFIG_OPT4060_TRIGGER
	.trigger_set = &opt4060_trigger_set,
#endif
	.sample_fetch = &opt4060_sample_fetch,
	.channel_get = &opt4060_channel_get,
};

static opt4060_ret_t opt4060_read_chan_cnt(const struct device *const p_dev,
					   const enum sensor_channel chan,
					   opt4060_measurement_cnt_t *const p_cnt)
{
	opt4060_data_t *const p_data = p_dev->data;
	for (int32_t i = 0; i < OPT4060_READ_CHAN_CNT_MAX_RETRIES; ++i) {
		struct sensor_value val = {0};
		const int64_t t1 = k_uptime_ticks();
		opt4060_ret_t res = sensor_channel_get(p_dev, chan, &val);
		const int64_t t2 = k_uptime_ticks();
		if (0 == res) {
			const uint32_t tmp_val2 = (uint32_t)val.val2;
			const opt4060_measurement_cnt_t cnt =
				tmp_val2 & OPT4060_MEASUREMENT_CNT_MASK;
			val.val2 = tmp_val2 & ~OPT4060_MEASUREMENT_CNT_MASK;
			LOG_DBG("Measured: %d.%06u, cnt=%u", val.val1, val.val2, cnt);
			if (p_data->sensor_channel_get_cnt <
			    OPT4060_MEASURE_MEASUREMENT_DURATION_NUM_CYCLES) {
				p_data->sensor_channel_get_cnt += 1;
				p_data->sensor_channel_get_accum_time += t2 - t1;
			}
			*p_cnt = cnt;
			return 0;
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
					   const enum sensor_channel chan,
					   const opt4060_measurement_cnt_t cur_cnt)
{
	const opt4060_measurement_cnt_t next_cnt =
		((uint32_t)cur_cnt + 1U) & OPT4060_MEASUREMENT_CNT_MASK;
	const uint32_t timeout_us = (((OPT4060_CONV_TIME_US * OPT4060_CHANNEL_NUM) *
				      OPT4060_TIMEOUT_MARGIN_MULTIPLIER_NUM) /
				     OPT4060_TIMEOUT_MARGIN_MULTIPLIER_DEN) +
				    OPT4060_TIMEOUT_EXTRA_US;
	const uint64_t timeout_ticks = k_us_to_ticks_ceil32(timeout_us);
	const int64_t time_start = k_uptime_ticks();
	bool flag_timeout = false;
	while (1) {
		opt4060_measurement_cnt_t cnt = 0;
		const opt4060_ret_t res = opt4060_read_chan_cnt(p_dev, chan, &cnt);
		if (res < 0) {
			LOG_ERR("%s:%d", __FILE__, __LINE__);
			return false;
		}
		if (cnt == next_cnt) {
			break;
		}
		if (cnt != cur_cnt) {
			LOG_ERR("%s:%d: res=%d, cur_cnt=%u, next_cnt=%u", __FILE__, __LINE__, res,
				cur_cnt, next_cnt);
			return false;
		}
		const int64_t time_end = k_uptime_ticks();
		if (flag_timeout) {
			LOG_ERR("%s:%d: time_start=%" PRId64 ", time_end=%" PRId64
				", delta=%" PRId64 ", timeout_us=%" PRIu32
				", timeout_ticks=%" PRId64,
				__FILE__, __LINE__, time_start, time_end, time_end - time_start,
				timeout_us, timeout_ticks);
			return false;
		}
		if ((time_end - time_start) > timeout_ticks) {
			// Check if counter is changed after timeout expired
			flag_timeout = true;
		}
	}
	return true;
}

static bool opt4060_measure_period(const struct device *const p_dev)
{
	opt4060_data_t *const p_data = p_dev->data;
	const enum sensor_channel chan = SENSOR_CHAN_GREEN;
	const uint32_t max_wait_time_us =
		(OPT4060_MAX_MEASURE_PERIOD_US < (OPT4060_CONV_TIME_US * 16))
			? OPT4060_MAX_MEASURE_PERIOD_US
			: (OPT4060_CONV_TIME_US * 16);
	const int64_t max_wait_time_ticks = k_us_to_ticks_ceil32(max_wait_time_us);
	uint32_t cycle_cnt = 0;

	opt4060_measurement_cnt_t cur_cnt = 0;
	opt4060_ret_t res = opt4060_read_chan_cnt(p_dev, chan, &cur_cnt);
	if (res < 0) {
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
		cur_cnt = (opt4060_measurement_cnt_t)(cur_cnt + 1) & OPT4060_MEASUREMENT_CNT_MASK;
		if (!opt4060_wait_for_next_chan_cnt(p_dev, chan, cur_cnt)) {
			LOG_ERR("%s:%d", __FILE__, __LINE__);
			return false;
		}
		const int64_t delta_time_ticks = k_uptime_ticks() - time_start;
		if (delta_time_ticks > max_wait_time_ticks) {
			p_data->one_measurement_duration_ticks =
				(int32_t)((delta_time_ticks +
					   (cycle_cnt *
					    (OPT4060_CHANNEL_NUM / OPT4060_ROUND_HALF_DIVISOR))) /
					  (cycle_cnt * OPT4060_CHANNEL_NUM));
			p_data->sensor_channel_get_duration_ticks =
				(int32_t)((p_data->sensor_channel_get_accum_time +
					   (p_data->sensor_channel_get_cnt /
					    OPT4060_ROUND_HALF_DIVISOR)) /
					  p_data->sensor_channel_get_cnt);
			LOG_INF("Configured conv time: %" PRIu32 " us", OPT4060_CONV_TIME_US);
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
	const opt4060_data_t *const p_data = p_dev->data;
	return p_data->one_measurement_duration_ticks;
}

static void opt4060_set_fast_speed_i2c(void)
{
	static NRF_TWIM_Type *const g_p_twim = (NRF_TWIM_Type *)0x40003000;
	/*
		https://docs.nordicsemi.com/bundle/errata_nRF52840_Rev3/page/ERR/
		nRF52840/Rev3/latest/anomaly_840_219.html#anomaly_840_219

		Symptom:
		The low period of the SCL clock is too short to meet the I2C specification at 400
		kHz. The actual low period of the SCL clock is 1.25 µs while the I2C specification
		requires the SCL clock to have a minimum low period of 1.3 µs.

		Workaround:
		If communication does not work at 400 kHz with an I2C compatible device that
		requires the SCL clock to have a minimum low period of 1.3 µs, use 390 kHz instead
	   of 400kHz by writing 0x06200000 to the FREQUENCY register. With this setting, the SCL low
		period is greater than 1.3 µs.

		To set TWI frequency to 400 kHz, use constant NRF_TWIM_FREQ_400K defined in
		nrf_twim.h. Its value is 0x06400000UL.
	*/
	nrf_twim_frequency_set(g_p_twim, NRF_TWIM_FREQ_390K); // Set TWI frequency to 390 kHz
}

static int // NOSONAR: Zephyr API
opt4060_init(const struct device *const p_dev)
{
	const opt4060_config_t *const p_config = p_dev->config;
	opt4060_data_t *const p_data = p_dev->data;

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
		const opt4060_ret_t res = opt4060_init_interrupt(p_dev);
		if (0 != res) {
			LOG_ERR("Failed to initialize interrupts.");
			return res;
		}
	}
#endif

	p_data->cfg_reg = OPT4060_REG_CONFIG_VAL_LATCH | OPT4060_REG_CONFIG_VAL_INT_POL_ACTIVE_LOW;

#if defined(CONFIG_OPT4060_QUICK_WAKEUP) && CONFIG_OPT4060_QUICK_WAKEUP
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_QWAKE_ON;
#endif

#if defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
#if defined(CONFIG_OPT4060_OP_MODE_ONE_SHOT_FORCED_AUTO_RANGE) &&                                  \
	CONFIG_OPT4060_OP_MODE_ONE_SHOT_FORCED_AUTO_RANGE
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_OPERATING_MODE_FORCED_ONESHOT;
#else
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_OPERATING_MODE_ONESHOT;
#endif
#else
	p_data->cfg_reg |= OPT4060_REG_CONFIG_VAL_OPERATING_MODE_CONTINUOUS;
#endif

	p_data->cfg_reg |= OPT4060_REG_CONFIG_DEFAULT_RANGE;

	p_data->cfg_reg |= OPT4060_REG_CONFIG_DEFAULT_CONV_TIME;

#if defined(CONFIG_OPT4060_INT) && CONFIG_OPT4060_INT
	uint16_t int_flags = OPT4060_REG_CONFIG2_VAL_INT_DIR_OUTPUT;
#if defined(CONFIG_OPT4060_INT_DATA_READY_FOR_ALL_CHANNELS) &&                                     \
	CONFIG_OPT4060_INT_DATA_READY_FOR_ALL_CHANNELS
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

#if defined(CONFIG_OPT4060_OP_MODE_ONESHOT) && CONFIG_OPT4060_OP_MODE_ONESHOT
	p_data->flag_one_shot_started = false;
#else
	if (!opt4060_reg_write(p_dev, OPT4060_REG_CONFIG, p_data->cfg_reg)) {
		LOG_ERR("Failed to configure sensor");
		return -EIO;
	}

	opt4060_ret_t res = 0;

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

opt4060_ret_t opt4060_configure_conv_time(const struct device *const p_dev,
					  const uint16_t conv_time)
{
	opt4060_data_t *const p_data = p_dev->data;

	p_data->cfg_reg &= ~OPT4060_REG_CONFIG_VAL_CONV_TIME_MASK;
	p_data->cfg_reg |= conv_time;

	if (!opt4060_reg_write(p_dev, OPT4060_REG_CONFIG, p_data->cfg_reg)) {
		LOG_ERR("Failed to configure sensor");
		return -EIO;
	}
	return 0;
}

#if defined(CONFIG_OPT4060_INT) && CONFIG_OPT4060_INT
#define OPT4060_CFG_INT(inst) .gpio_int = GPIO_DT_SPEC_INST_GET_BY_IDX(inst, irq_gpios, 0),
#else
#define OPT4060_CFG_INT(inst) /* NOSONAR */
#endif

#define OPT4060_DEFINE(inst) /* NOSONAR */                                                         \
	static opt4060_data_t opt4060_data_##inst;                                                 \
	static const opt4060_config_t opt4060_config_##inst = {.i2c = I2C_DT_SPEC_INST_GET(inst),  \
							       OPT4060_CFG_INT(inst)};             \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, opt4060_init, NULL, &opt4060_data_##inst,               \
				     &opt4060_config_##inst, POST_KERNEL,                          \
				     CONFIG_SENSOR_INIT_PRIORITY, &opt4060_driver_api);

DT_INST_FOREACH_STATUS_OKAY(OPT4060_DEFINE)
