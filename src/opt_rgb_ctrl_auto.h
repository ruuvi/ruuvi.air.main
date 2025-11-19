/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef OPT_RBG_CTRL_AUTO_H
#define OPT_RBG_CTRL_AUTO_H

#include <zephyr/dsp/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum opt_rgb_ctrl_error_e
{
    OPT_RGB_CTRL_ERROR_NONE = 0,
    OPT_RGB_CTRL_ERROR_TIMEOUT_READING_GREEN_CHANNEL_MEASUREMENT,
    OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_GREEN_CHANNEL_MEASUREMENT,
    OPT_RGB_CTRL_ERROR_TIMEOUT_WAITING_LUMINOSITY_CHANNEL_MEASUREMENT,
    OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_CNT_CHANGED_UNEXPECTEDLY,
    OPT_RGB_CTRL_ERROR_FAILED_TO_TURN_OFF_LED,
    OPT_RGB_CTRL_ERROR_CHECK_BLUE_CHANNEL_FAILED,
    OPT_RGB_CTRL_ERROR_LUMINOSITY_CHANNEL_LATE,
    OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL,
    OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_CNT_CHANGED,
    OPT_RGB_CTRL_ERROR_REREAD_LUMINOSITY_CHANNEL_VAL_CHANGED,
    OPT_RGB_CTRL_ERROR_FAILED_TO_READ_LED,
    OPT_RGB_CTRL_ERROR_FAILED_TO_RESTORE_LED,
} opt_rgb_ctrl_error_e;

void
opt_rgb_ctrl_auto_init(void);

float32_t
opt_rgb_ctrl_auto_get_luminosity(void);

void
opt_rgb_ctrl_auto_measure_i2c_delays(void);

void
opt_rgb_ctrl_auto_do_measure_luminosity(void);

#ifdef __cplusplus
}
#endif

#endif // OPT_RBG_CTRL_AUTO_H
