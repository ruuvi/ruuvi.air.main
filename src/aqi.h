/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#ifndef AQI_H
#define AQI_H

#include <stdbool.h>
#include "rgb_led_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum air_quality_index_e
{
    AIR_QUALITY_INDEX_NONE = 0,
    AIR_QUALITY_INDEX_EXCELLENT,
    AIR_QUALITY_INDEX_GOOD,
    AIR_QUALITY_INDEX_FAIR,
    AIR_QUALITY_INDEX_POOR,
    AIR_QUALITY_INDEX_VERY_POOR,
    AIR_QUALITY_NUM_INDEXES = AIR_QUALITY_INDEX_VERY_POOR + 1,
} air_quality_index_e;

typedef enum manual_brightness_level
{
    MANUAL_BRIGHTNESS_LEVEL_OFF = 0,
    MANUAL_BRIGHTNESS_LEVEL_NIGHT,
    MANUAL_BRIGHTNESS_LEVEL_DAY,
    MANUAL_BRIGHTNESS_LEVEL_BRIGHT_DAY,
    MANUAL_BRIGHTNESS_LEVELS,
} manual_brightness_level_e;

typedef struct manual_brightness_color_t
{
    rgb_led_currents_t currents;
    rgb_led_color_t    colors[AIR_QUALITY_NUM_INDEXES];
} manual_brightness_color_t;

void
aqi_init(void);

const rgb_led_exp_current_coefs_t*
aqi_get_led_currents_alpha(void);

void
aqi_update_led(const float air_quality_index);

void
aqi_refresh_led(void);

void
aqi_recalc_auto_brightness_level(const float luminosity);

const manual_brightness_color_t*
aqi_get_colors_table(const manual_brightness_level_e level);

void
aqi_set_colors_table(const manual_brightness_level_e level, const manual_brightness_color_t* const p_table);

void
aqi_reset_colors_table(const manual_brightness_level_e level);

#ifdef __cplusplus
}
#endif

#endif // AQI_H
