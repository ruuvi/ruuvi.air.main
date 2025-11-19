/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "api.h"
#include <math.h>
#include <zephyr/sys/util.h>
#include "tlog.h"

LOG_MODULE_REGISTER(API, LOG_LEVEL_INF);

#define AQI_MAX (100)

#define PM25_MAX   (60)
#define PM25_MIN   (0)
#define PM25_SCALE ((float32_t)AQI_MAX / (PM25_MAX - PM25_MIN)) /* ≈ 1.6667 */

#define CO2_MAX   (2300)
#define CO2_MIN   (420)
#define CO2_SCALE ((float32_t)AQI_MAX / (CO2_MAX - CO2_MIN)) /* ≈ 0.05319 */

static float32_t
clamp(float32_t value, float32_t min, float32_t max)
{
    if (value < min)
    {
        return min;
    }
    else if (value > max)
    {
        return max;
    }
    return value;
}

float32_t
api_calc_air_quality_index(const sensors_measurement_t* const p_measurement)
{
    float32_t pm2p5 = sen66_wrap_conv_raw_to_float_pm(p_measurement->sen66.mass_concentration_pm2p5);
    float32_t co2   = sen66_wrap_conv_raw_to_float_co2(p_measurement->sen66.co2);
    if (isnan(pm2p5) || isnan(co2))
    {
        LOG_WRN("AQI: Invalid sensor readings: PM2.5: %f, CO2: %f", (double)pm2p5, (double)co2);
        return NAN;
    }
    pm2p5 = clamp(pm2p5, PM25_MIN, PM25_MAX);
    co2   = clamp(co2, CO2_MIN, CO2_MAX);

    const float32_t dx = (pm2p5 - PM25_MIN) * PM25_SCALE; // 0..100
    const float32_t dy = (co2 - CO2_MIN) * CO2_SCALE;     // 0..100

    const float32_t r                 = sqrtf((dx * dx) + (dy * dy)); // distance from (0,0) to (dx,dy)
    const float32_t air_quality_index = clamp(AQI_MAX - r, 0, AQI_MAX);

    LOG_INF(
        "AQI: %f (PM2.5: %f, dx: %f, CO2: %f, dy: %f, r: %f)",
        (double)air_quality_index,
        (double)pm2p5,
        (double)dx,
        (double)co2,
        (double)dy,
        (double)r);

    return air_quality_index;
}
