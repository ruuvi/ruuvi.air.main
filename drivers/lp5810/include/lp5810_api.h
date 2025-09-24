/**
 * @copyright (c) 2025 Ruuvi Innovations Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_LED_LP5810_API_H_
#define ZEPHYR_INCLUDE_DRIVERS_LED_LP5810_API_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>

int lp5810_deinit(const struct device *dev);

void lp5810_lock(const struct device *dev);

void lp5810_unlock(const struct device *dev);

int lp5810_read_pwms(const struct device *dev, uint8_t *const p_buf, const size_t buf_len);

int lp5810_write_pwms(const struct device *dev, const uint8_t *const p_buf, const size_t buf_len);

bool lp5810_check_and_reinit_if_needed(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_LED_LP5810_API_H_ */
