/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "moving_avg.h"
#include "zassert.h"
#include "ruuvi_endpoint_e1.h"

static void*
test_setup(void);

static void
test_suite_before(void* f);

static void
test_suite_after(void* f);

static void
test_teardown(void* f);

ZTEST_SUITE(test_suite_moving_avg, NULL, &test_setup, &test_suite_before, &test_suite_after, &test_teardown);

typedef struct test_suite_moving_avg_fixture
{
    int stub;
} test_suite_moving_avg_fixture_t;

static void*
test_setup(void)
{
    test_suite_moving_avg_fixture_t* p_fixture = calloc(1, sizeof(*p_fixture));
    assert(NULL != p_fixture);
    return p_fixture;
}

static void
test_suite_before(void* f)
{
    test_suite_moving_avg_fixture_t* p_fixture = f;
    memset(p_fixture, 0, sizeof(*p_fixture));
    moving_avg_init();
}

static void
test_suite_after(void* f)
{
}

static void
test_teardown(void* f)
{
    if (NULL != f)
    {
        free(f);
    }
}

static re_e1_data_t
convert_record_to_e1_data(const hist_log_record_data_t* const p_record, const radio_mac_t radio_mac)
{
    uint8_t buffer[RE_E1_OFFSET_PAYLOAD + RE_E1_DATA_LENGTH] = { 0 };
    memcpy(&buffer[RE_E1_OFFSET_PAYLOAD], p_record->buf, sizeof(p_record->buf));
    for (int i = 0; i < 6; ++i)
    {
        buffer[RE_E1_OFFSET_PAYLOAD + RE_E1_OFFSET_ADDR_MSB + i] = (radio_mac >> (5 - i) * 8) & 0xFFU;
    }

    re_e1_data_t e1_data = { 0 };
    ZASSERT_EQ_INT(RE_SUCCESS, re_e1_decode(buffer, &e1_data));
    return e1_data;
}

ZTEST_F(test_suite_moving_avg, test_1)
{
    const sensors_measurement_t measurement = {
        .sen66 = {
            .mass_concentration_pm1p0 = 106, // 10.6
            .mass_concentration_pm2p5 = 124, // 12.4
            .mass_concentration_pm4p0 = 136, //13.6
            .mass_concentration_pm10p0 = 142, // 14.2
            .ambient_humidity = 5588, // 55.88
            .ambient_temperature = 5493, // 27.465
            .voc_index = 800, // 80.0
            .nox_index = 20, // 2.0
            .co2 = 549,
        },
        .dps310_temperature = 28.576f,
        .dps310_pressure = 100746.855f,
        .shtc3_temperature = 27.511f,
        .shtc3_humidity = 57.351f,
        .luminosity = 88.0f,
        .sound_inst_dba = 71.0f,
        .sound_avg_dba = 64.0f,
        .sound_peak_spl_db = 81.0f,
    };
    for (int i = 0; i < 5 * 60 - 1; ++i)
    {
        zassert_false(moving_avg_append(&measurement));
    }
    zassert_true(moving_avg_append(&measurement));

    const measurement_cnt_t measurement_cnt = 0x123456;
    const radio_mac_t       radio_mac       = 0x112233445566;
    const sensors_flags_t   flags           = {
                    .flag_calibration_in_progress = false,
                    .flag_button_pressed          = false,
                    .flag_rtc_running_on_boot     = true,
    };

    const hist_log_record_data_t record  = moving_avg_get_accum(measurement_cnt, radio_mac, flags);
    const re_e1_data_t           e1_data = convert_record_to_e1_data(&record, radio_mac);

    ZASSERT_EQ_FLOAT((float)measurement.sen66.ambient_temperature / 200, e1_data.temperature_c);
    ZASSERT_EQ_FLOAT((float)measurement.sen66.ambient_humidity / 100, e1_data.humidity_rh);
    ZASSERT_EQ_INT(lrintf(measurement.dps310_pressure), lrintf(e1_data.pressure_pa));
    ZASSERT_EQ_FLOAT((float)measurement.sen66.mass_concentration_pm1p0 / 10, e1_data.pm1p0_ppm);
    ZASSERT_EQ_FLOAT((float)measurement.sen66.mass_concentration_pm2p5 / 10, e1_data.pm2p5_ppm);
    ZASSERT_EQ_FLOAT((float)measurement.sen66.mass_concentration_pm4p0 / 10, e1_data.pm4p0_ppm);
    ZASSERT_EQ_FLOAT((float)measurement.sen66.mass_concentration_pm10p0 / 10, e1_data.pm10p0_ppm);
    ZASSERT_EQ_FLOAT((float)measurement.sen66.co2, e1_data.co2);
    ZASSERT_EQ_FLOAT((float)measurement.sen66.voc_index / 10, e1_data.voc);
    ZASSERT_EQ_FLOAT((float)measurement.sen66.nox_index / 10, e1_data.nox);
    ZASSERT_EQ_FLOAT(measurement.luminosity, e1_data.luminosity);
    ZASSERT_EQ_FLOAT(measurement.sound_inst_dba, e1_data.sound_inst_dba);
    ZASSERT_EQ_FLOAT(measurement.sound_avg_dba, e1_data.sound_avg_dba);
    ZASSERT_EQ_FLOAT(measurement.sound_peak_spl_db, e1_data.sound_peak_spl_db);
    ZASSERT_EQ_INT(0, e1_data.flags.flag_calibration_in_progress);
    ZASSERT_EQ_INT(0, e1_data.flags.flag_button_pressed);
    ZASSERT_EQ_INT(1, e1_data.flags.flag_rtc_running_on_boot);
    ZASSERT_EQ_INT(measurement_cnt, e1_data.seq_cnt);
    zassert_equal(radio_mac, e1_data.address);
}

ZTEST_F(test_suite_moving_avg, test_2)
{
    const sensors_measurement_t mea_invalid = {
        .sen66 = {
            .mass_concentration_pm1p0 = SEN66_INVALID_RAW_VALUE_PM,
            .mass_concentration_pm2p5 = SEN66_INVALID_RAW_VALUE_PM,
            .mass_concentration_pm4p0 = SEN66_INVALID_RAW_VALUE_PM,
            .mass_concentration_pm10p0 = SEN66_INVALID_RAW_VALUE_PM,
            .ambient_humidity = SEN66_INVALID_RAW_VALUE_HUMIDITY,
            .ambient_temperature = SEN66_INVALID_RAW_VALUE_TEMPERATURE,
            .voc_index = SEN66_INVALID_RAW_VALUE_VOC,
            .nox_index = SEN66_INVALID_RAW_VALUE_NOX,
            .co2 = SEN66_INVALID_RAW_VALUE_CO2,
        },
        .dps310_temperature = NAN,
        .dps310_pressure = NAN,
        .shtc3_temperature = NAN,
        .shtc3_humidity = NAN,
        .luminosity = NAN,
        .sound_inst_dba = NAN,
        .sound_avg_dba = NAN,
        .sound_peak_spl_db = NAN,
    };
    const sensors_measurement_t mea1 = {
        .sen66 = {
            .mass_concentration_pm1p0 = 106, // 10.6
            .mass_concentration_pm2p5 = 124, // 12.4
            .mass_concentration_pm4p0 = 136, //13.6
            .mass_concentration_pm10p0 = 142, // 14.2
            .ambient_humidity = 5588, // 55.88
            .ambient_temperature = 5411,
            .voc_index = 800, // 80.0
            .nox_index = 20, // 2.0
            .co2 = 549,
        },
        .dps310_temperature = 28.576f,
        .dps310_pressure = 100746.855f,
        .shtc3_temperature = 27.511f,
        .shtc3_humidity = 57.351f,
        .luminosity = 88.0f,
        .sound_inst_dba = 73.0f,
        .sound_avg_dba = 64.0f,
        .sound_peak_spl_db = 81.0f,
    };
    const sensors_measurement_t mea2 = {
        .sen66 = {
            .mass_concentration_pm1p0 = 206,
            .mass_concentration_pm2p5 = 224,
            .mass_concentration_pm4p0 = 236,
            .mass_concentration_pm10p0 = 242,
            .ambient_humidity = 5988,
            .ambient_temperature = 5624,
            .voc_index = 900,
            .nox_index = 30,
            .co2 = 649,
        },
        .dps310_temperature = 29.576f,
        .dps310_pressure = 101746.855f,
        .shtc3_temperature = 28.511f,
        .shtc3_humidity = 59.351f,
        .luminosity = 80.0f,
        .sound_inst_dba = 70.0f,
        .sound_avg_dba = 67.0f,
        .sound_peak_spl_db = 73.0f,
    };
    zassert_false(moving_avg_append(&mea_invalid));
    for (int i = 0; i < 149; ++i)
    {
        zassert_false(moving_avg_append(&mea1));
    }
    zassert_false(moving_avg_append(&mea_invalid));
    for (int i = 0; i < 148; ++i)
    {
        zassert_false(moving_avg_append(&mea2));
    }
    zassert_true(moving_avg_append(&mea2));

    const measurement_cnt_t measurement_cnt = 0x123456;
    const radio_mac_t       radio_mac       = 0x112233445566;
    const sensors_flags_t   flags           = {
                    .flag_calibration_in_progress = false,
                    .flag_button_pressed          = false,
                    .flag_rtc_running_on_boot     = true,
    };

    const hist_log_record_data_t record  = moving_avg_get_accum(measurement_cnt, radio_mac, flags);
    const re_e1_data_t           e1_data = convert_record_to_e1_data(&record, radio_mac);

    const float avg_temp = (float)(mea1.sen66.ambient_temperature + mea2.sen66.ambient_temperature) / 2 / 200;
    ZASSERT_EQ_FLOAT_WITHIN(avg_temp, e1_data.temperature_c, 0.01f);
    const float avg_humidity = (float)(mea1.sen66.ambient_humidity + mea2.sen66.ambient_humidity) / 2 / 100;
    ZASSERT_EQ_FLOAT_WITHIN(avg_humidity, e1_data.humidity_rh, 0.02f);
    const float avg_pressure = (mea1.dps310_pressure + mea2.dps310_pressure) / 2;
    ZASSERT_EQ_FLOAT_WITHIN(avg_pressure, e1_data.pressure_pa, 2.0f);
    const float avg_pm1p0 = (float)(mea1.sen66.mass_concentration_pm1p0 + mea2.sen66.mass_concentration_pm1p0) / 2 / 10;
    ZASSERT_EQ_FLOAT_WITHIN(avg_pm1p0, e1_data.pm1p0_ppm, 0.01f);
    const float avg_pm2p5 = (float)(mea1.sen66.mass_concentration_pm2p5 + mea2.sen66.mass_concentration_pm2p5) / 2 / 10;
    ZASSERT_EQ_FLOAT_WITHIN(avg_pm2p5, e1_data.pm2p5_ppm, 0.01f);
    const float avg_pm4p0 = (float)(mea1.sen66.mass_concentration_pm4p0 + mea2.sen66.mass_concentration_pm4p0) / 2 / 10;
    ZASSERT_EQ_FLOAT_WITHIN(avg_pm4p0, e1_data.pm4p0_ppm, 0.01f);
    const float avg_pm10p0 = (float)(mea1.sen66.mass_concentration_pm10p0 + mea2.sen66.mass_concentration_pm10p0) / 2
                             / 10;
    ZASSERT_EQ_FLOAT_WITHIN(avg_pm10p0, e1_data.pm10p0_ppm, 0.01f);
    const float avg_co2 = (float)(mea1.sen66.co2 + mea2.sen66.co2) / 2;
    ZASSERT_EQ_FLOAT_WITHIN(avg_co2, e1_data.co2, 0.1f);
    const float avg_voc = (float)(mea1.sen66.voc_index + mea2.sen66.voc_index) / 2 / 10;
    ZASSERT_EQ_FLOAT_WITHIN(avg_voc, e1_data.voc, 0.1f);
    const float avg_nox = (float)(mea1.sen66.nox_index + mea2.sen66.nox_index) / 2 / 10;
    ZASSERT_EQ_FLOAT_WITHIN(avg_nox, e1_data.nox, 0.5f);
    const float avg_luminosity = (mea1.luminosity + mea2.luminosity) / 2;
    ZASSERT_EQ_FLOAT_WITHIN(avg_luminosity, e1_data.luminosity, 0.1f);
    const float avg_sound_inst_dba = (mea1.sound_inst_dba + mea2.sound_inst_dba) / 2;
    ZASSERT_EQ_FLOAT_WITHIN(avg_sound_inst_dba, e1_data.sound_inst_dba, 0.1f);
    const float avg_sound_avg_dba = (mea1.sound_avg_dba + mea2.sound_avg_dba) / 2;
    ZASSERT_EQ_FLOAT_WITHIN(avg_sound_avg_dba, e1_data.sound_avg_dba, 0.1f);
    ZASSERT_EQ_FLOAT(MAX(mea1.sound_peak_spl_db, mea2.sound_peak_spl_db), e1_data.sound_peak_spl_db);
    ZASSERT_EQ_INT(0, e1_data.flags.flag_calibration_in_progress);
    ZASSERT_EQ_INT(0, e1_data.flags.flag_button_pressed);
    ZASSERT_EQ_INT(1, e1_data.flags.flag_rtc_running_on_boot);
    ZASSERT_EQ_INT(measurement_cnt, e1_data.seq_cnt);
    zassert_equal(radio_mac, e1_data.address);
}
