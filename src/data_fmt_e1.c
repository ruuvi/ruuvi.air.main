/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "data_fmt_e1.h"

re_e1_data_t
data_fmt_e1_init(
    const sensors_measurement_t* const p_measurement,
    const uint32_t                     measurement_cnt,
    const uint64_t                     radio_mac)
{
    const re_e1_data_t data = {
        .temperature_c     = sen66_wrap_conv_raw_to_float_temperature(p_measurement->sen66.ambient_temperature),
        .humidity_rh       = sen66_wrap_conv_raw_to_float_humidity(p_measurement->sen66.ambient_humidity),
        .pressure_pa       = p_measurement->dps310_pressure,
        .pm1p0_ppm         = sen66_wrap_conv_raw_to_float_pm(p_measurement->sen66.mass_concentration_pm1p0),
        .pm2p5_ppm         = sen66_wrap_conv_raw_to_float_pm(p_measurement->sen66.mass_concentration_pm2p5),
        .pm4p0_ppm         = sen66_wrap_conv_raw_to_float_pm(p_measurement->sen66.mass_concentration_pm4p0),
        .pm10p0_ppm        = sen66_wrap_conv_raw_to_float_pm(p_measurement->sen66.mass_concentration_pm10p0),
        .co2               = sen66_wrap_conv_raw_to_float_co2(p_measurement->sen66.co2),
        .voc = sen66_wrap_conv_raw_to_float_voc_index(p_measurement->sen66.voc_index),
        .nox = sen66_wrap_conv_raw_to_float_nox_index(p_measurement->sen66.nox_index),
        .luminosity        = (re_float)p_measurement->luminosity,
        .sound_inst_dba     = (re_float)p_measurement->sound_inst_dba,
        .sound_avg_dba     = (re_float)p_measurement->sound_avg_dba,
        .sound_peak_spl_db    = (re_float)p_measurement->sound_peak_spl_db,
        .seq_cnt = measurement_cnt,

        .flags = {
            .flag_calibration_in_progress = false,
            .flag_button_pressed          = false,
            .flag_rtc_running_on_boot     = false,
        },

        .address = radio_mac,
    };
    return data;
}
