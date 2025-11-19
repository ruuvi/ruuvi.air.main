/**
 * @copyright (c) 2025 Ruuvi Innovations Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT ti_lp5810

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/util.h>
#include "include/lp5810.h"
#include "include/lp5810_api.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lp5810, CONFIG_LED_LOG_LEVEL);

#define LP5810_NUM_RETRIES (3)

/* Maximum number of channels */
#define LP5810_MAX_CHANNELS (4)

#define LP5810_DISABLE_DELAY_US         (10)
#define LP5810_CHIP_ENABLE_DELAY_US     (1000)
#define LP5810_DELAY_BETWEEN_RETRIES_US (500)

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

typedef uint16_t lp5810_reg_t;

static inline lp5810_reg_t lp5810_reg_manual_dc(const lp5810_led_idx_e channel)
{
	return (lp5810_reg_t)LP5810_BASE_REG_MANUAL_DC + (lp5810_reg_t)channel;
}

static inline lp5810_reg_t lp5810_reg_manual_pwm(const lp5810_led_idx_e channel)
{
	return (lp5810_reg_t)LP5810_BASE_REG_MANUAL_PWM + (lp5810_reg_t)channel;
}

static inline lp5810_reg_t lp5810_reg_auto_dc(const lp5810_led_idx_e channel)
{
	return (lp5810_reg_t)LP5810_BASE_REG_AUTO_DC + (lp5810_reg_t)channel;
}

static inline lp5810_reg_t lp5810_reg_auto_animation_base(const lp5810_led_idx_e channel)
{
	return (lp5810_reg_t)LP5810_BASE_REG_AUTO_ANIMATION +
	       ((lp5810_reg_t)channel * LP5810_AUTO_ANIMATION_CFG_SIZE);
}

struct lp5810_config {
	struct i2c_dt_spec i2c;
	const struct gpio_dt_spec gpio_enable;
	uint8_t max_leds;
	uint8_t num_leds;
	bool max_curr_opt;
	bool lod_action_cur_out_shutdown;
	bool lsd_action_all_out_shutdown;
	uint8_t lsd_threshold;
	const struct led_info *leds_info;
};

struct lp5810_data {
	struct k_mutex mutex;
	uint8_t write_chan_buf[0x1A + 1]; /* +1 for the command byte */
	uint8_t led_mask;                 /* Mask of enabled LEDs */
};

static uint8_t lp5810_i2c_addr(const struct device *const p_dev, const lp5810_reg_t reg)
{
	const struct lp5810_config *const p_config = p_dev->config;
	return (uint8_t)(p_config->i2c.addr +
			 ((reg >> BITS_PER_BYTE) & LP5810_I2C_ADDR_LSB_REG_MASK));
}

static bool lp5810_reg_read(const struct device *const p_dev, const lp5810_reg_t reg,
			    uint8_t *const p_val)
{
	const struct lp5810_config *p_config = p_dev->config;
	const uint8_t i2c_addr = lp5810_i2c_addr(p_dev, reg);

	if (0 != i2c_burst_read(p_config->i2c.bus, i2c_addr, (uint8_t)(reg & BYTE_MASK), p_val,
				sizeof(*p_val))) {
		return false;
	}

	return true;
}

#if 0
static bool lp5810_reg_read_with_retries(const struct device *const p_dev, const lp5810_reg_t reg,
					 uint8_t *const p_val)
{
	for (int i = 0; i < LP5810_NUM_RETRIES; ++i) {
		if (lp5810_reg_read(p_dev, reg, p_val)) {
			return true;
		}
		k_usleep(LP5810_DELAY_BETWEEN_RETRIES_US);
	}
	LOG_ERR("%s: Failed to read reg 0x%02x after retries", p_dev->name, reg);
	return false;
}
#endif

static bool lp5810_reg_write(const struct device *const p_dev, const lp5810_reg_t reg,
			     const uint8_t val)
{
	const struct lp5810_config *p_config = p_dev->config;
	const uint8_t i2c_addr = lp5810_i2c_addr(p_dev, reg);
	const uint8_t tx_buf[] = {(uint8_t)(reg & BYTE_MASK), (uint8_t)(val & BYTE_MASK)};

	if (0 != i2c_write(p_config->i2c.bus, tx_buf, sizeof(tx_buf), i2c_addr)) {
		return false;
	}
	return true;
}

static bool lp5810_reg_write_with_retries(const struct device *const p_dev, const lp5810_reg_t reg,
					  const uint8_t val)
{
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		if (lp5810_reg_write(p_dev, reg, val)) {
			return true;
		}
		k_usleep(LP5810_DELAY_BETWEEN_RETRIES_US);
	}
	LOG_ERR("%s: Failed to write reg 0x%02x after retries", p_dev->name, reg);
	return false;
}

static bool lp5810_buf_write(const struct device *const p_dev, const lp5810_reg_t start_reg,
			     const uint8_t *const p_buf, const size_t buf_len)
{
	const struct lp5810_config *const p_config = p_dev->config;
	struct lp5810_data *const p_data = p_dev->data;
	const uint8_t i2c_addr = lp5810_i2c_addr(p_dev, start_reg);
	if (buf_len >= sizeof(p_data->write_chan_buf)) {
		LOG_ERR("%s: Buffer length %zu exceeds maximum %zu", p_dev->name, buf_len,
			sizeof(p_data->write_chan_buf) - 1);
		return false;
	}
	p_data->write_chan_buf[0] = (uint8_t)(start_reg & BYTE_MASK);
	memcpy(&p_data->write_chan_buf[1], p_buf, buf_len);

	if (0 != i2c_write(p_config->i2c.bus, p_data->write_chan_buf, buf_len + 1, i2c_addr)) {
		return false;
	}
	return true;
}

static bool lp5810_buf_write_with_retries(const struct device *const p_dev,
					  const lp5810_reg_t start_reg, const uint8_t *const p_buf,
					  const size_t buf_len)
{
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		if (lp5810_buf_write(p_dev, start_reg, p_buf, buf_len)) {
			return true;
		}
		k_usleep(LP5810_DELAY_BETWEEN_RETRIES_US);
	}
	LOG_ERR("%s: Failed to write buf to 0x%02x after retries", p_dev->name, start_reg);
	return false;
}

static bool lp5810_buf_read(const struct device *const p_dev, const lp5810_reg_t start_reg,
			    uint8_t *const p_buf, const size_t buf_len)
{
	const struct lp5810_config *const p_config = p_dev->config;
	const uint8_t i2c_addr = lp5810_i2c_addr(p_dev, start_reg);

	if (0 != i2c_burst_read(p_config->i2c.bus, i2c_addr, (uint8_t)(start_reg & BYTE_MASK),
				p_buf, buf_len)) {
		return false;
	}
	return true;
}

static bool lp5810_buf_read_with_retries(const struct device *const p_dev,
					 const lp5810_reg_t start_reg, uint8_t *const p_buf,
					 const size_t buf_len)
{
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		if (lp5810_buf_read(p_dev, start_reg, p_buf, buf_len)) {
			return true;
		}
		k_usleep(LP5810_DELAY_BETWEEN_RETRIES_US);
	}
	LOG_ERR("%s: Failed to read buf from 0x%02x after retries", p_dev->name, start_reg);
	return false;
}

static void lp5810_ll_software_reset(const struct device *dev)
{
	/* LP5810 often does not respond on software reset command */
	(void)lp5810_reg_write(dev, LP5810_REG_RESET, LP5810_REG_RESET_CMD);
}

static bool lp5810_ll_read_chip_enable(const struct device *const dev, bool *const p_flag_enable)
{
	uint8_t value = 0;

	if (!lp5810_reg_read(dev, LP5810_REG_CHIP_EN, &value)) {
		LOG_ERR("%s: Failed to read CHIP_EN", dev->name);
		return false;
	}
	*p_flag_enable =
		(LP5810_REG_CHIP_EN_VAL == (value & LP5810_REG_CHIP_EN_MASK)) ? true : false;

	return true;
}

static bool lp5810_read_chip_enable_with_retries(const struct device *const dev,
						 bool *const p_flag_enable)
{
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		if (lp5810_ll_read_chip_enable(dev, p_flag_enable)) {
			return true;
		}
		k_usleep(LP5810_DELAY_BETWEEN_RETRIES_US);
	}
	LOG_ERR("%s: Failed to read CHIP_EN after retries", dev->name);
	return false;
}

static bool lp5810_ll_write_chip_enable(const struct device *const dev, const bool flag_enable)
{
	const uint8_t value = flag_enable ? LP5810_REG_CHIP_EN_VAL : 0;

	if (!lp5810_reg_write(dev, LP5810_REG_CHIP_EN, value)) {
		LOG_ERR("%s: Failed to write CHIP_EN", dev->name);
		return false;
	}
	return true;
}

static bool lp5810_write_chip_enable_with_retries(const struct device *const dev,
						  const bool flag_enable)
{
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		if (lp5810_ll_write_chip_enable(dev, flag_enable)) {
			return true;
		}
		k_usleep(LP5810_DELAY_BETWEEN_RETRIES_US);
	}
	return false;
}

static bool lp5810_check_if_device_present(const struct device *dev)
{
	lp5810_ll_software_reset(dev);

	bool is_chip_responding = false;
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		k_usleep(LP5810_CHIP_ENABLE_DELAY_US);
		uint8_t value = 0;
		if (!lp5810_reg_read(dev, LP5810_REG_CHIP_EN, &value)) {
			LOG_ERR("%s: Failed to read CHIP_EN", dev->name);
			continue;
		}
		if (!lp5810_reg_read(dev, LP5810_REG_TSD_CONFIG_STATUS, &value)) {
			LOG_ERR("%s: Failed to read TSD_CONFIG_STATUS", dev->name);
			continue;
		}
		is_chip_responding = true;
		break;
	}
	return is_chip_responding;
}

static bool lp5810_reset(const struct device *dev)
{
	lp5810_ll_software_reset(dev);

	bool chip_enabled = false;
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		if (!lp5810_ll_write_chip_enable(dev, true)) {
			LOG_WRN("%s: Failed to enable chip", dev->name);
			continue;
		}
		if (!lp5810_ll_read_chip_enable(dev, &chip_enabled)) {
			LOG_WRN("%s: Failed to read chip enable status", dev->name);
			continue;
		}
		if (!chip_enabled) {
			LOG_WRN("%s: Could not enable LP5810 chip", dev->name);
			continue;
		}
		break;
	}
	if (!chip_enabled) {
		LOG_ERR("%s: Could not enable LP5810 chip", dev->name);
		return false;
	}
	for (int32_t i = 0; i < LP5810_NUM_RETRIES; ++i) {
		lp5810_ll_software_reset(dev);
		k_usleep(LP5810_CHIP_ENABLE_DELAY_US);
		if (!lp5810_ll_read_chip_enable(dev, &chip_enabled)) {
			LOG_WRN("%s: Failed to read chip enable status", dev->name);
			continue;
		}
		if (chip_enabled) {
			LOG_WRN("%s: Chip is enabled after reset", dev->name);
			continue;
		}
		break;
	}
	if (chip_enabled) {
		LOG_ERR("%s: Failed to reset LP5810", dev->name);
		return false;
	}
	LOG_INF("%s: LP5810 reset successful", dev->name);
	return true;
}

static const char *lp5810_get_lsd_threshold_str(const struct lp5810_config *config)
{
	switch (config->lsd_threshold) {
	case LP5810_LSD_THRESHOLD_035_VOUT:
		return "0.35 VOUT";
	case LP5810_LSD_THRESHOLD_045_VOUT:
		return "0.45 VOUT";
	case LP5810_LSD_THRESHOLD_055_VOUT:
		return "0.55 VOUT";
	case LP5810_LSD_THRESHOLD_065_VOUT:
		return "0.65 VOUT";
	default:
		return "Unknown threshold";
	}
}

static bool lp5810_check_status_regs(const struct device *dev)
{
	const struct lp5810_data *const p_data = dev->data;

	const uint8_t led_mask =
		(0 == p_data->led_mask)
			? (LP5810_REG_LED_EN_1_VAL_LED_EN_0 | LP5810_REG_LED_EN_1_VAL_LED_EN_1 |
			   LP5810_REG_LED_EN_1_VAL_LED_EN_2 | LP5810_REG_LED_EN_1_VAL_LED_EN_3)
			: p_data->led_mask;
	uint8_t status_regs[(LP5810_REG_LSD_STATUS_0 - LP5810_REG_TSD_CONFIG_STATUS) + 1] = {0};
	if (!lp5810_buf_read_with_retries(dev, LP5810_REG_TSD_CONFIG_STATUS, status_regs,
					  sizeof(status_regs))) {
		LOG_ERR("%s: Failed to read status regs", dev->name);
		return false;
	}

	bool res = true;

	const uint8_t tsd_config_status = status_regs[0];
	if ((tsd_config_status & LP5810_REG_TSD_CONFIG_STATUS_CONFIG_ERR) != 0) {
		LOG_WRN("%s: TSD_CONFIG_STATUS indicates CONFIG_ERR", dev->name);
	}
	if ((tsd_config_status & LP5810_REG_TSD_CONFIG_STATUS_TSD_STATUS) != 0) {
		LOG_WRN("%s: TSD_CONFIG_STATUS indicates TSD", dev->name);
	}
	const uint8_t lod_status =
		status_regs[LP5810_REG_LOD_STATUS_0 - LP5810_REG_TSD_CONFIG_STATUS];
	if ((lod_status & led_mask) != 0) {
		LOG_WRN("%s: LOD_STATUS_0 indicates an error: 0x%02x", dev->name, lod_status);
		if (!lp5810_reg_write_with_retries(dev, LP5810_REG_FAULT_CLEAR,
						   LP5810_REG_FAULT_CLEAR_LOD)) {
			LOG_ERR("%s: Failed to write FAULT_CLEAR", dev->name);
			res = false;
		}
	}
	const uint8_t lsd_status =
		status_regs[LP5810_REG_LSD_STATUS_0 - LP5810_REG_TSD_CONFIG_STATUS];
	if ((lsd_status & led_mask) != 0) {
		LOG_WRN("%s: LSD_STATUS_0 indicates an error: 0x%02x", dev->name, lsd_status);
		if (!lp5810_reg_write_with_retries(dev, LP5810_REG_FAULT_CLEAR,
						   LP5810_REG_FAULT_CLEAR_LSD)) {
			LOG_ERR("%s: Failed to write FAULT_CLEAR", dev->name);
			res = false;
		}
	}
	return res;
}

static bool lp5810_configure(const struct device *dev)
{
	const struct lp5810_config *config = dev->config;
	struct lp5810_data *const p_data = dev->data;

	bool chip_enabled = false;
	if (!lp5810_read_chip_enable_with_retries(dev, &chip_enabled)) {
		LOG_ERR("%s: Failed to read chip enable status", dev->name);
		return false;
	}
	if (!chip_enabled) {
		LOG_ERR("%s: Could not enable chip", dev->name);
		return false;
	}

	if (config->max_curr_opt) {
		LOG_INF("%s: Set max current to 51 mA", dev->name);
		if (!lp5810_reg_write_with_retries(dev, LP5810_REG_DEV_CONFIG_0,
						   LP5810_REG_DEV_CONFIG_0_VAL_MAX_CURRENT_51MA)) {
			LOG_ERR("%s: Failed to write DEV_CONFIG_0", dev->name);
			return false;
		}
	} else {
		LOG_INF("%s: Set max current to 25.5 mA", dev->name);
	}

	LOG_INF("%s: Set LSD threshold to %d: %s, lod_action_cur_out_shutdown=%d, "
		"lsd_action_all_out_shutdown=%d",
		dev->name, config->lsd_threshold, lp5810_get_lsd_threshold_str(config),
		config->lod_action_cur_out_shutdown, config->lsd_action_all_out_shutdown);
	uint8_t cfg_val = 0;
	cfg_val |= config->lod_action_cur_out_shutdown
			   ? LP5810_REG_DEV_CONFIG_12_VAL_LOD_ACTION_CUR_OUT_SHUTDOWN
			   : 0U;
	cfg_val |= config->lsd_action_all_out_shutdown
			   ? LP5810_REG_DEV_CONFIG_12_VAL_LSD_ACTION_ALL_OUT_SHUTDOWN
			   : 0U;
	cfg_val |= config->lsd_threshold & LP5810_REG_DEV_CONFIG_12_VAL_LSD_THRESHOLD_MASK;
	if (!lp5810_reg_write_with_retries(dev, LP5810_REG_DEV_CONFIG_12, cfg_val)) {
		LOG_ERR("%s: Failed to set LSD_threshold", dev->name);
		return false;
	}

	if (IS_ENABLED(CONFIG_LP5810_EXPONENTIAL_PWM)) {
		LOG_INF("%s: Using exponential PWM dimming curve", dev->name);
		if (!lp5810_reg_write_with_retries(
			    dev, LP5810_REG_DEV_CONFIG_5,
			    LP5810_REG_DEV_CONFIG_5_VAL_EXP_EN_LED_0 |
				    LP5810_REG_DEV_CONFIG_5_VAL_EXP_EN_LED_1 |
				    LP5810_REG_DEV_CONFIG_5_VAL_EXP_EN_LED_2 |
				    LP5810_REG_DEV_CONFIG_5_VAL_EXP_EN_LED_3)) {
			LOG_ERR("%s: Failed to set DEV_CONFIG_5", dev->name);
			return false;
		}
	}

	uint8_t led_en_mask = 0;
	for (int32_t i = 0; i < config->num_leds; ++i) {
		const struct led_info *led_info = &config->leds_info[i];
		switch (led_info->index) {
		case LP5810_LED_IDX_0:
			led_en_mask |= LP5810_REG_LED_EN_1_VAL_LED_EN_0;
			break;
		case LP5810_LED_IDX_1:
			led_en_mask |= LP5810_REG_LED_EN_1_VAL_LED_EN_1;
			break;
		case LP5810_LED_IDX_2:
			led_en_mask |= LP5810_REG_LED_EN_1_VAL_LED_EN_2;
			break;
		case LP5810_LED_IDX_3:
			led_en_mask |= LP5810_REG_LED_EN_1_VAL_LED_EN_3;
			break;
		default:
			LOG_ERR("%s: Invalid LED index %d", dev->name, led_info->index);
			return false;
		}
		LOG_INF("%s: Enable LED: index=%d: label='%s', num_colors=%u", dev->name,
			led_info->index, led_info->label, led_info->num_colors);
	}
	p_data->led_mask = led_en_mask;

	if (!lp5810_reg_write_with_retries(dev, LP5810_REG_LED_EN_1, led_en_mask)) {
		LOG_ERR("%s: Failed to write ENABLE Channels", dev->name);
		return false;
	}

	if (!lp5810_reg_write_with_retries(dev, LP5810_REG_CMD_UPDATE, LP5810_REG_CMD_UPDATE_VAL)) {
		LOG_ERR("%s: Failed to write UPDATE", dev->name);
		return false;
	}

	lp5810_check_status_regs(dev);

	return true;
}

bool lp5810_check_and_reinit_if_needed(const struct device *dev)
{
	bool chip_enabled = false;
	if (!lp5810_read_chip_enable_with_retries(dev, &chip_enabled)) {
		LOG_ERR("%s: Failed to read chip enable status", dev->name);
		return false;
	}
	if (!chip_enabled) {
		LOG_ERR("%s: LP5810 chip is not enabled (self-reset?)", dev->name);
		if (!lp5810_configure(dev)) {
			LOG_ERR("%s: Failed to re-configure chip", dev->name);
			return false;
		}
	}

	return lp5810_check_status_regs(dev);
}

static const struct led_info *lp5810_led_to_info(const struct lp5810_config *const config,
						 const uint32_t led)
{
	if (led < config->num_leds) {
		return &config->leds_info[led];
	}

	return NULL;
}

static int // NOSONAR: Zephyr API
lp5810_get_info(const struct device *dev, uint32_t led, const struct led_info **p_p_info)
{
	const struct lp5810_config *config = dev->config;
	const struct led_info *const p_led_info = lp5810_led_to_info(config, led);

	if (NULL == p_led_info) {
		return -EINVAL;
	}

	*p_p_info = p_led_info;

	return 0;
}

static int // NOSONAR: Zephyr API
lp5810_set_brightness(const struct device *dev, uint32_t led, uint8_t value)
{
	const struct lp5810_config *config = dev->config;
	const struct led_info *const p_led_info = lp5810_led_to_info(config, led);

	if (!p_led_info) {
		LOG_ERR("%s: LED %d not found", dev->name, led);
		return -ENODEV;
	}

	if (led > config->num_leds) {
		LOG_ERR("%s: LED index out of bounds: led=%d, max=%d", dev->name, led,
			config->num_leds - 1);
		return -EINVAL;
	}

	if (value > LP5810_MAX_BRIGHTNESS) {
		LOG_ERR("%s: brightness value out of bounds: val=%d, max=%d", dev->name, value,
			LP5810_MAX_BRIGHTNESS);
		return -EINVAL;
	}

	const lp5810_led_idx_e led_idx = (lp5810_led_idx_e)led;

	lp5810_lock(dev);
	const uint8_t pwm = (uint8_t)(((uint16_t)value * BYTE_MASK) / LP5810_MAX_BRIGHTNESS);
	if (!lp5810_reg_write_with_retries(dev, lp5810_reg_manual_pwm(led_idx), pwm)) {
		LOG_ERR("%s: Failed to set PWM for LED %d", dev->name, led);
		lp5810_unlock(dev);
		return -EIO;
	}
	lp5810_unlock(dev);
	return 0;
}

static int // NOSONAR: Zephyr API
lp5810_set_color(const struct device *dev, uint32_t led, uint8_t num_colors,
		 const uint8_t *p_colors_buf)
{
	const struct lp5810_config *config = dev->config;
	const struct led_info *led_info = lp5810_led_to_info(config, led);

	if (NULL == led_info) {
		return -ENODEV;
	}

	if (num_colors != led_info->num_colors) {
		LOG_ERR("%s: invalid number of colors: got=%d, expected=%d", dev->name, num_colors,
			led_info->num_colors);
		return -EINVAL;
	}

	const lp5810_led_idx_e led_idx = (lp5810_led_idx_e)led;

	lp5810_lock(dev);
	if (!lp5810_reg_write_with_retries(dev, lp5810_reg_manual_dc(led_idx), p_colors_buf[0])) {
		LOG_ERR("LP5810: Failed to set brightness for LED %d", led);
		lp5810_unlock(dev);
		return -EIO;
	}
	lp5810_unlock(dev);
	return 0;
}

static int // NOSONAR: Zephyr API
lp5810_write_channels(const struct device *dev, uint32_t start_channel, uint32_t num_channels,
		      const uint8_t *buf)
{
	const struct lp5810_config *const p_config = dev->config;

	const uint32_t max_channels = (uint32_t)p_config->num_leds + p_config->num_leds; // DC + PWM
	if ((start_channel + num_channels) > max_channels) {
		LOG_ERR("%s: Invalid channel range: start=%d, num=%d, max=%d", dev->name,
			start_channel, num_channels, max_channels);
		return -EINVAL;
	}

	lp5810_lock(dev);

	const uint8_t start_channel_idx = (uint8_t)start_channel;

	const uint8_t num_dc = (start_channel_idx < p_config->num_leds)
				       ? (p_config->num_leds - start_channel_idx)
				       : 0;
	if (start_channel_idx < p_config->num_leds) {
		const lp5810_led_idx_e start_dc = (lp5810_led_idx_e)start_channel_idx;
		if (!lp5810_buf_write_with_retries(dev, lp5810_reg_manual_dc(start_dc), &buf[0],
						   num_dc)) {
			LOG_ERR("%s: Failed to write DC channels", dev->name);
			lp5810_unlock(dev);
			return -EIO;
		}
	}
	if ((start_channel_idx + num_channels) > p_config->num_leds) {
		const lp5810_led_idx_e start_pwm =
			(lp5810_led_idx_e)((start_channel_idx >= p_config->num_leds)
						   ? (start_channel_idx - p_config->num_leds)
						   : 0);
		const uint8_t num_pwm =
			(start_channel_idx + (uint8_t)num_channels) - p_config->num_leds;
		const uint8_t buf_offset = (start_channel_idx >= p_config->num_leds) ? 0 : num_dc;
		if (!lp5810_buf_write_with_retries(dev, lp5810_reg_manual_pwm(start_pwm),
						   &buf[buf_offset], num_pwm)) {
			LOG_ERR("%s: Failed to write PWM channels", dev->name);
			lp5810_unlock(dev);
			return -EIO;
		}
	}
#if 0
	lp5810_check_status_regs(dev);
#endif
	lp5810_unlock(dev);
	return 0;
}

static int // NOSONAR: Zephyr API
lp5810_on(const struct device *dev, uint32_t led)
{
	lp5810_lock(dev);
	const uint8_t colors_buf[LP5810_COLORS_PER_LED] = {BYTE_MASK};
	lp5810_ret_t res = lp5810_set_color(dev, led, sizeof(colors_buf), colors_buf);
	if (0 != res) {
		LOG_ERR("lp5810_set_color failed: %d", res);
		lp5810_unlock(dev);
		return res;
	}
	res = lp5810_set_brightness(dev, led, LP5810_MAX_BRIGHTNESS);
	if (0 != res) {
		LOG_ERR("lp5810_set_brightness failed: %d", res);
		lp5810_unlock(dev);
		return res;
	}
	lp5810_unlock(dev);
	return 0;
}

static int // NOSONAR: Zephyr API
lp5810_off(const struct device *dev, uint32_t led)
{
	lp5810_lock(dev);
	const uint8_t colors_buf[LP5810_COLORS_PER_LED] = {0x00};
	lp5810_ret_t res = lp5810_set_color(dev, led, sizeof(colors_buf), colors_buf);
	if (0 != res) {
		LOG_ERR("lp5810_set_color failed: %d", res);
		lp5810_unlock(dev);
		return res;
	}
	res = lp5810_set_brightness(dev, led, 0);
	if (0 != res) {
		LOG_ERR("lp5810_set_brightness failed: %d", res);
		lp5810_unlock(dev);
		return res;
	}
	lp5810_unlock(dev);
	return 0;
}

static bool lp5810_hw_enable(const struct device *dev, const bool flag_enable)
{
	const struct lp5810_config *const config = dev->config;

	if (NULL == config->gpio_enable.port) {
		/* Nothing to do */
		return true;
	}

	const lp5810_ret_t err = gpio_pin_set_dt(&config->gpio_enable, flag_enable);
	if (err < 0) {
		LOG_ERR("%s: failed to set enable gpio", dev->name);
		return false;
	}

	k_usleep(flag_enable ? LP5810_CHIP_ENABLE_DELAY_US : LP5810_DISABLE_DELAY_US);

	return true;
}

static int // NOSONAR: Zephyr API
lp5810_init(const struct device *dev)
{
	const struct lp5810_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("%s: I2C device not ready", dev->name);
		return -ENODEV;
	}

	if (config->num_leds > config->max_leds) {
		LOG_ERR("%s: invalid number of LEDs %d (max %d)", dev->name, config->num_leds,
			config->max_leds);
		return -EINVAL;
	}

	/* Configure GPIO if present */
	if (config->gpio_enable.port != NULL) {
		if (!gpio_is_ready_dt(&config->gpio_enable)) {
			LOG_ERR("%s: enable gpio is not ready", dev->name);
			return -ENODEV;
		}

		lp5810_ret_t err =
			gpio_pin_configure_dt(&config->gpio_enable, GPIO_OUTPUT_INACTIVE);
		if (err < 0) {
			LOG_ERR("%s: failed to initialize enable gpio", dev->name);
			return err;
		}
	}

	/* Enable hardware */
	if (!lp5810_hw_enable(dev, true)) {
		LOG_ERR("%s: failed to enable hardware", dev->name);
		return -EIO;
	}

	/* Check device */
	if (!lp5810_check_if_device_present(dev)) {
		LOG_ERR("%s: device not present", dev->name);
		return -EIO;
	}

	/* Reset device */
	if (!lp5810_reset(dev)) {
		LOG_ERR("%s: failed to reset", dev->name);
		return -EIO;
	}

	/* Enable device */
	if (!lp5810_write_chip_enable_with_retries(dev, true)) {
		LOG_ERR("%s: failed to enable", dev->name);
		return -EIO;
	}

	/* Configure device */
	if (!lp5810_configure(dev)) {
		LOG_ERR("%s: failed to configure", dev->name);
		return -EIO;
	}

	LOG_INF("%s: initialized successfully: max LEDs %u, configured LEDs %u", dev->name,
		config->max_leds, config->num_leds);

	return 0;
}

lp5810_ret_t lp5810_deinit(const struct device *dev)
{
	const struct lp5810_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("%s: I2C device not ready", dev->name);
		return -ENODEV;
	}
	lp5810_ll_software_reset(dev);

	/* Disable hardware */
	if (!lp5810_hw_enable(dev, false)) {
		LOG_ERR("%s: failed to disable hardware", dev->name);
		return -EIO;
	}
	return 0;
}

#ifdef CONFIG_PM_DEVICE
static lp5810_ret_t lp5810_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		return lp5810_write_chip_enable_with_retries(dev, false);
	case PM_DEVICE_ACTION_RESUME:
		return lp5810_write_chip_enable_with_retries(dev, true);
	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif /* CONFIG_PM_DEVICE */

static const struct led_driver_api lp5810_led_api = {
	.on = lp5810_on,
	.off = lp5810_off,
	.get_info = lp5810_get_info,
	.set_brightness = lp5810_set_brightness,
	.set_color = lp5810_set_color,
	.write_channels = lp5810_write_channels,
};

#define COLOR_MAPPING(led_node_id)                                                                 \
	const uint8_t color_mapping_##led_node_id[] = DT_PROP(led_node_id, color_mapping);

#define LED_INFO(led_node_id)                                                                      \
	{                                                                                          \
		.label = DT_PROP(led_node_id, label),                                              \
		.index = DT_PROP(led_node_id, index),                                              \
		.num_colors = DT_PROP_LEN(led_node_id, color_mapping),                             \
		.color_mapping = color_mapping_##led_node_id,                                      \
	},

#define LP5810_DEVICE(n) /* NOSONAR */                                                             \
	DT_INST_FOREACH_CHILD(n, COLOR_MAPPING)                                                    \
                                                                                                   \
	static const struct led_info lp5810_leds_##n[] = {DT_INST_FOREACH_CHILD(n, LED_INFO)};     \
                                                                                                   \
	static const struct lp5810_config lp5810_config_##n = {                                    \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                                    \
		.gpio_enable = GPIO_DT_SPEC_INST_GET_OR(n, enable_gpios, {0}),                     \
		.max_leds = LP5810_MAX_LEDS,                                                       \
		.num_leds = ARRAY_SIZE(lp5810_leds_##n),                                           \
		.max_curr_opt = DT_INST_PROP(n, max_curr_opt),                                     \
		.lod_action_cur_out_shutdown = DT_INST_PROP(n, lod_action_cur_out_shutdown),       \
		.lsd_action_all_out_shutdown = DT_INST_PROP(n, lsd_action_all_out_shutdown),       \
		.lsd_threshold = DT_INST_PROP(n, lsd_threshold),                                   \
		.leds_info = lp5810_leds_##n,                                                      \
	};                                                                                         \
                                                                                                   \
	static struct lp5810_data lp5810_data_##n = {                                              \
		.mutex = Z_MUTEX_INITIALIZER(lp5810_data_##n.mutex),                               \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, lp5810_init, PM_DEVICE_DT_INST_GET(n), &lp5810_data_##n,          \
			      &lp5810_config_##n, POST_KERNEL, CONFIG_LED_INIT_PRIORITY,           \
			      &lp5810_led_api);

DT_INST_FOREACH_STATUS_OKAY(LP5810_DEVICE)

void lp5810_lock(const struct device *dev)
{
	struct lp5810_data *const p_data = dev->data;
	k_mutex_lock(&p_data->mutex, K_FOREVER);
}

void lp5810_unlock(const struct device *dev)
{
	struct lp5810_data *const p_data = dev->data;
	k_mutex_unlock(&p_data->mutex);
}

lp5810_ret_t lp5810_read_pwms(const struct device *dev, uint8_t *const p_buf, const size_t buf_len)
{
	const struct lp5810_config *const p_config = dev->config;
	const size_t num_pwms = MIN(buf_len, p_config->num_leds);
	lp5810_lock(dev);
	if (!lp5810_buf_read_with_retries(dev, lp5810_reg_manual_pwm(0), p_buf, num_pwms)) {
		LOG_ERR("%s: Failed to read PWM channels", dev->name);
		memset(p_buf, 0, buf_len);
		lp5810_unlock(dev);
		return -EIO;
	}
	lp5810_unlock(dev);
	return 0;
}

lp5810_ret_t lp5810_write_pwms(const struct device *dev, const uint8_t *const p_buf,
			       const size_t buf_len)
{
	const struct lp5810_config *const p_config = dev->config;
	const size_t num_pwms = MIN(buf_len, p_config->num_leds);
	lp5810_lock(dev);
	if (!lp5810_buf_write_with_retries(dev, lp5810_reg_manual_pwm(0), p_buf, num_pwms)) {
		LOG_ERR("%s: Failed to write PWM channels", dev->name);
		lp5810_unlock(dev);
		return -EIO;
	}
	lp5810_unlock(dev);
	return 0;
}

bool lp5810_auto_animation_enable(const struct device *dev, const uint8_t *const p_auto_dc_buf,
				  const size_t buf_len)
{
	const struct lp5810_config *config = dev->config;

	if (!lp5810_buf_write_with_retries(dev, lp5810_reg_auto_dc(0), p_auto_dc_buf, buf_len)) {
		LOG_ERR("LP5810: Failed to set LP5810_REG_AUTO_DC");
		return false;
	}

	uint8_t led_auto_en_mask = 0;
	for (uint8_t i = 0; i < MIN(buf_len, config->num_leds); ++i) {
		switch (i) {
		case LP5810_LED_IDX_0:
			led_auto_en_mask |= LP5810_REG_DEV_CONFIG_3_VAL_AUTO_EN_0;
			break;
		case LP5810_LED_IDX_1:
			led_auto_en_mask |= LP5810_REG_DEV_CONFIG_3_VAL_AUTO_EN_1;
			break;
		case LP5810_LED_IDX_2:
			led_auto_en_mask |= LP5810_REG_DEV_CONFIG_3_VAL_AUTO_EN_2;
			break;
		case LP5810_LED_IDX_3:
			led_auto_en_mask |= LP5810_REG_DEV_CONFIG_3_VAL_AUTO_EN_3;
			break;
		default:
			LOG_ERR("%s: Invalid LED index %d", dev->name, i);
			return false;
		}
	}
	if (!lp5810_reg_write_with_retries(dev, LP5810_REG_DEV_CONFIG_3, led_auto_en_mask)) {
		LOG_ERR("%s: Failed to write LP5810_REG_DEV_CONFIG_3", dev->name);
		return false;
	}
	if (!lp5810_reg_write_with_retries(dev, LP5810_REG_CMD_UPDATE, LP5810_REG_CMD_UPDATE_VAL)) {
		LOG_ERR("%s: Failed to write LP5810_REG_CMD_UPDATE", dev->name);
		return false;
	}
	return true;
}

bool lp5810_auto_animation_configure(const struct device *dev, const lp5810_led_idx_e channel,
				     const lp5810_auto_animation_cfg_t *const p_cfg)
{
	const struct lp5810_config *config = dev->config;

	if ((channel < 0) || (channel >= config->num_leds)) {
		LOG_ERR("LP5810: Invalid auto animation channel: %d", channel);
		return false;
	}
	if (!lp5810_buf_write_with_retries(dev, lp5810_reg_auto_animation_base(channel),
					   (const uint8_t *)p_cfg, sizeof(*p_cfg))) {
		LOG_ERR("LP5810: Failed to set LP5810_REG_AUTO_ANIMATION");
		return false;
	}
	return true;
}

bool lp5810_auto_animation_start(const struct device *dev)
{
	if (!lp5810_reg_write_with_retries(dev, LP5810_REG_CMD_START, LP5810_REG_CMD_START_VAL)) {
		LOG_ERR("%s: Failed to write START", dev->name);
		return false;
	}
	return true;
}
