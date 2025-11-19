/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "data_fmt_6.h"

re_6_data_t
data_fmt_6_init(
    const sensors_measurement_t* const p_measurement,
    const uint16_t                     measurement_cnt,
    const uint64_t                     radio_mac,
    const re_6_flags_t                 flags)
{
    // re_6_mac_address_t address
    const re_6_data_t data = {
        .temperature_c = sen66_wrap_conv_raw_to_float_temperature(p_measurement->sen66.ambient_temperature),
        .humidity_rh   = sen66_wrap_conv_raw_to_float_humidity(p_measurement->sen66.ambient_humidity),
        .pressure_pa   = p_measurement->dps310_pressure,
        .pm2p5_ppm     = sen66_wrap_conv_raw_to_float_pm(p_measurement->sen66.mass_concentration_pm2p5),
        .co2           = sen66_wrap_conv_raw_to_float_co2(p_measurement->sen66.co2),
        .voc           = sen66_wrap_conv_raw_to_float_voc_index(p_measurement->sen66.voc_index),
        .nox           = sen66_wrap_conv_raw_to_float_nox_index(p_measurement->sen66.nox_index),
        .luminosity    = p_measurement->luminosity,
        .sound_avg_dba = p_measurement->sound_avg_dba,
        .seq_cnt2      = measurement_cnt & RE_BYTE_MASK,
        .flags         = flags,

        .mac_addr_24 = { .byte3 = (uint8_t)((radio_mac >> RE_BYTE_2_SHIFT) & RE_BYTE_MASK),
                         .byte4 = (uint8_t)((radio_mac >> RE_BYTE_1_SHIFT) & RE_BYTE_MASK),
                         .byte5 = (uint8_t)((radio_mac >> RE_BYTE_0_SHIFT) & RE_BYTE_MASK) }
    };
    return data;
}
