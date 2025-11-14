/**
 * @copyright (c) 2025 Ruuvi Innovations Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_LED_LP5810_API_H_
#define ZEPHYR_INCLUDE_DRIVERS_LED_LP5810_API_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>

typedef struct lp5810_auto_animation_cfg {
	uint8_t auto_pause;
	uint8_t auto_playback;
	uint8_t AEU1_PWM[5];
	uint8_t AEU1_T12;
	uint8_t AEU1_T34;
	uint8_t AEU1_playback;
	uint8_t AEU2_PWM[5];
	uint8_t AEU2_T12;
	uint8_t AEU2_T34;
	uint8_t AEU2_playback;
	uint8_t AEU3_PWM[5];
	uint8_t AEU3_T12;
	uint8_t AEU3_T34;
	uint8_t AEU3_playback;
} lp5810_auto_animation_cfg_t;

_Static_assert(sizeof(lp5810_auto_animation_cfg_t) == 0x1A,
	       "lp5810_auto_animation_cfg_t size mismatch");

int lp5810_deinit(const struct device *dev);

void lp5810_lock(const struct device *dev);

void lp5810_unlock(const struct device *dev);

int lp5810_read_pwms(const struct device *dev, uint8_t *const p_buf, const size_t buf_len);

int lp5810_write_pwms(const struct device *dev, const uint8_t *const p_buf, const size_t buf_len);

bool lp5810_check_and_reinit_if_needed(const struct device *dev);

bool lp5810_auto_animation_enable(const struct device *dev, const uint8_t *const p_auto_dc_buf,
				  const size_t buf_len);

bool lp5810_auto_animation_configure(const struct device *dev, const int channel,
				     const lp5810_auto_animation_cfg_t *const p_cfg);

bool lp5810_auto_animation_start(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_LED_LP5810_API_H_ */
