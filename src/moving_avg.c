/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "moving_avg.h"
#include <math.h>
#include <assert.h>
#include <string.h>
#include "avg_accum.h"
#include "ruuvi_endpoint_e1.h"
#include "data_fmt_e1.h"

#define MOVING_AVG_WINDOW_SIZE_SECONDS (5 * 60U)

#define MOVING_AVG_WINDOW_SIZE_STAGE1 (20)
_Static_assert(MOVING_AVG_WINDOW_SIZE_STAGE1 <= UINT8_MAX);

_Static_assert(0 == (MOVING_AVG_WINDOW_SIZE_SECONDS % MOVING_AVG_WINDOW_SIZE_STAGE1));
#define MOVING_AVG_WINDOW_SIZE_STAGE2 (MOVING_AVG_WINDOW_SIZE_SECONDS / MOVING_AVG_WINDOW_SIZE_STAGE1)
_Static_assert(MOVING_AVG_WINDOW_SIZE_STAGE2 <= UINT8_MAX);

#define INVALID_BATTERY_VOLTAGE (0xFFFFU)
#define INVALID_LUMINOSITY      (0xFFFFU)
#define INVALID_SOUND_DBA       (0)

typedef struct moving_avg_data_t
{
    int16_t  ambient_temperature;
    int16_t  ambient_humidity;
    float    ambient_pressure;
    uint16_t mass_concentration_pm1p0;
    uint16_t mass_concentration_pm2p5;
    uint16_t mass_concentration_pm4p0;
    uint16_t mass_concentration_pm10p0;
    int16_t  voc_index;
    int16_t  nox_index;
    uint16_t co2;
    uint16_t luminosity;
    int16_t  sound_inst_dba_x100;
    int16_t  sound_avg_dba_x100;
    int16_t  sound_peak_spl_db_x100;
} moving_avg_data_t;

typedef struct moving_avg_accum_data_t
{
    avg_accum_t ambient_temperature;
    avg_accum_t ambient_humidity;
    avg_accum_t ambient_pressure;
    avg_accum_t mass_concentration_pm1p0;
    avg_accum_t mass_concentration_pm2p5;
    avg_accum_t mass_concentration_pm4p0;
    avg_accum_t mass_concentration_pm10p0;
    avg_accum_t voc_index;
    avg_accum_t nox_index;
    avg_accum_t co2;
    avg_accum_t battery_voltage_mv;
    avg_accum_t luminosity;
    avg_accum_t sound_dba_inst_x100;
    avg_accum_t sound_dba_avg_x100;
    int16_t     sound_dba_peak_x100;
} moving_avg_accum_data_t;

typedef struct moving_avg_arr_t
{
    moving_avg_data_t* const measurements;
    const uint8_t            size;
    uint8_t                  cnt;
} moving_avg_arr_t;

static moving_avg_data_t g_moving_avg1_data[MOVING_AVG_WINDOW_SIZE_STAGE1];
static moving_avg_data_t g_moving_avg2_data[MOVING_AVG_WINDOW_SIZE_STAGE2];

static moving_avg_arr_t g_moving_avg1 = {
    .measurements = g_moving_avg1_data,
    .size         = MOVING_AVG_WINDOW_SIZE_STAGE1,
    .cnt          = 0,
};
static moving_avg_arr_t g_moving_avg2 = {
    .measurements = g_moving_avg2_data,
    .size         = MOVING_AVG_WINDOW_SIZE_STAGE2,
    .cnt          = 0,
};

void
moving_avg_init(void)
{
    g_moving_avg1.cnt = 0;
    g_moving_avg2.cnt = 0;
}

static bool
moving_avg_data_append(moving_avg_arr_t* const p_moving_avg, const moving_avg_data_t* const p_data)
{
    p_moving_avg->measurements[p_moving_avg->cnt] = *p_data;
    p_moving_avg->cnt++;
    if (p_moving_avg->cnt >= p_moving_avg->size)
    {
        p_moving_avg->cnt = 0;
        return true;
    }
    return false;
}

moving_avg_data_t
moving_avg_data_get_avg(const moving_avg_arr_t* const p_moving_avg)
{
    moving_avg_accum_data_t accum = {
        .ambient_temperature       = AVG_ACCUM_INIT_I16(SEN66_INVALID_RAW_VALUE_TEMPERATURE),
        .ambient_humidity          = AVG_ACCUM_INIT_I16(SEN66_INVALID_RAW_VALUE_HUMIDITY),
        .ambient_pressure          = AVG_ACCUM_INIT_F32(),
        .mass_concentration_pm1p0  = AVG_ACCUM_INIT_U16(SEN66_INVALID_RAW_VALUE_PM),
        .mass_concentration_pm2p5  = AVG_ACCUM_INIT_U16(SEN66_INVALID_RAW_VALUE_PM),
        .mass_concentration_pm4p0  = AVG_ACCUM_INIT_U16(SEN66_INVALID_RAW_VALUE_PM),
        .mass_concentration_pm10p0 = AVG_ACCUM_INIT_U16(SEN66_INVALID_RAW_VALUE_PM),
        .voc_index                 = AVG_ACCUM_INIT_I16(SEN66_INVALID_RAW_VALUE_VOC),
        .nox_index                 = AVG_ACCUM_INIT_I16(SEN66_INVALID_RAW_VALUE_NOX),
        .co2                       = AVG_ACCUM_INIT_U16(SEN66_INVALID_RAW_VALUE_CO2),
        .battery_voltage_mv        = AVG_ACCUM_INIT_U16(INVALID_BATTERY_VOLTAGE),
        .luminosity                = AVG_ACCUM_INIT_U16(INVALID_LUMINOSITY),
        .sound_dba_avg_x100        = AVG_ACCUM_INIT_I16(INVALID_SOUND_DBA),
        .sound_dba_peak_x100       = INVALID_SOUND_DBA,
    };
    for (int i = 0; i < p_moving_avg->size; ++i)
    {
        const moving_avg_data_t* const p_data = &p_moving_avg->measurements[i];
        avg_accum_add_i16(&accum.ambient_temperature, p_data->ambient_temperature);
        avg_accum_add_i16(&accum.ambient_humidity, p_data->ambient_humidity);
        avg_accum_add_f32(&accum.ambient_pressure, p_data->ambient_pressure);
        avg_accum_add_u16(&accum.mass_concentration_pm1p0, p_data->mass_concentration_pm1p0);
        avg_accum_add_u16(&accum.mass_concentration_pm2p5, p_data->mass_concentration_pm2p5);
        avg_accum_add_u16(&accum.mass_concentration_pm4p0, p_data->mass_concentration_pm4p0);
        avg_accum_add_u16(&accum.mass_concentration_pm10p0, p_data->mass_concentration_pm10p0);
        avg_accum_add_i16(&accum.voc_index, p_data->voc_index);
        avg_accum_add_i16(&accum.nox_index, p_data->nox_index);
        avg_accum_add_u16(&accum.co2, p_data->co2);
        avg_accum_add_u16(&accum.luminosity, p_data->luminosity);
        avg_accum_add_i16(&accum.sound_dba_inst_x100, p_data->sound_inst_dba_x100);
        avg_accum_add_i16(&accum.sound_dba_avg_x100, p_data->sound_avg_dba_x100);
        if (INVALID_SOUND_DBA != p_data->sound_peak_spl_db_x100)
        {
            if ((INVALID_SOUND_DBA == accum.sound_dba_peak_x100)
                || (accum.sound_dba_peak_x100 < p_data->sound_peak_spl_db_x100))
            {
                accum.sound_dba_peak_x100 = p_data->sound_peak_spl_db_x100;
            }
        }
    }
    const moving_avg_data_t data = {
        .ambient_temperature       = avg_accum_calc_avg_i16(&accum.ambient_temperature),
        .ambient_humidity          = avg_accum_calc_avg_i16(&accum.ambient_humidity),
        .ambient_pressure          = avg_accum_calc_avg_f32(&accum.ambient_pressure),
        .mass_concentration_pm1p0  = avg_accum_calc_avg_u16(&accum.mass_concentration_pm1p0),
        .mass_concentration_pm2p5  = avg_accum_calc_avg_u16(&accum.mass_concentration_pm2p5),
        .mass_concentration_pm4p0  = avg_accum_calc_avg_u16(&accum.mass_concentration_pm4p0),
        .mass_concentration_pm10p0 = avg_accum_calc_avg_u16(&accum.mass_concentration_pm10p0),
        .voc_index                 = avg_accum_calc_avg_i16(&accum.voc_index),
        .nox_index                 = avg_accum_calc_avg_i16(&accum.nox_index),
        .co2                       = avg_accum_calc_avg_u16(&accum.co2),
        .luminosity                = avg_accum_calc_avg_u16(&accum.luminosity),
        .sound_inst_dba_x100       = avg_accum_calc_avg_i16(&accum.sound_dba_inst_x100),
        .sound_avg_dba_x100        = avg_accum_calc_avg_i16(&accum.sound_dba_avg_x100),
        .sound_peak_spl_db_x100    = accum.sound_dba_peak_x100,
    };
    return data;
}

bool
moving_avg_append(const sensors_measurement_t* const p_measurement)
{
    const moving_avg_data_t data = {
        .ambient_temperature       = p_measurement->sen66.ambient_temperature,
        .ambient_humidity          = p_measurement->sen66.ambient_humidity,
        .ambient_pressure          = p_measurement->dps310_pressure,
        .mass_concentration_pm1p0  = p_measurement->sen66.mass_concentration_pm1p0,
        .mass_concentration_pm2p5  = p_measurement->sen66.mass_concentration_pm2p5,
        .mass_concentration_pm4p0  = p_measurement->sen66.mass_concentration_pm4p0,
        .mass_concentration_pm10p0 = p_measurement->sen66.mass_concentration_pm10p0,
        .voc_index                 = p_measurement->sen66.voc_index,
        .nox_index                 = p_measurement->sen66.nox_index,
        .co2                       = p_measurement->sen66.co2,
        .luminosity                = isnan(p_measurement->luminosity) ? INVALID_LUMINOSITY
                                                                      : (uint16_t)lrintf(p_measurement->luminosity),
        .sound_inst_dba_x100       = isnan(p_measurement->sound_inst_dba) ? INVALID_SOUND_DBA
                                                                          : (int16_t)(p_measurement->sound_inst_dba * 100.0f),
        .sound_avg_dba_x100        = isnan(p_measurement->sound_avg_dba) ? INVALID_SOUND_DBA
                                                                         : (int16_t)(p_measurement->sound_avg_dba * 100.0f),
        .sound_peak_spl_db_x100    = isnan(p_measurement->sound_peak_spl_db)
                                         ? INVALID_SOUND_DBA
                                         : (int16_t)(p_measurement->sound_peak_spl_db * 100.0f),
    };
    if (moving_avg_data_append(&g_moving_avg1, &data))
    {
        const moving_avg_data_t data2 = moving_avg_data_get_avg(&g_moving_avg1);
        if (moving_avg_data_append(&g_moving_avg2, &data2))
        {
            return true;
        }
    }
    return false;
}

hist_log_record_data_t
moving_avg_get_accum(const measurement_cnt_t measurement_cnt, const radio_mac_t radio_mac, const sensors_flags_t flags)
{
    const moving_avg_data_t avg_data = moving_avg_data_get_avg(&g_moving_avg2);

    const sensors_measurement_t measurement_avg = {
        .sen66.mass_concentration_pm1p0  = avg_data.mass_concentration_pm1p0,
        .sen66.mass_concentration_pm2p5  = avg_data.mass_concentration_pm2p5,
        .sen66.mass_concentration_pm4p0  = avg_data.mass_concentration_pm4p0,
        .sen66.mass_concentration_pm10p0 = avg_data.mass_concentration_pm10p0,
        .sen66.ambient_humidity          = avg_data.ambient_humidity,
        .sen66.ambient_temperature       = avg_data.ambient_temperature,
        .sen66.voc_index                 = avg_data.voc_index,
        .sen66.nox_index                 = avg_data.nox_index,
        .sen66.co2                       = avg_data.co2,
        .dps310_temperature              = NAN,
        .dps310_pressure                 = avg_data.ambient_pressure,
        .shtc3_temperature               = NAN,
        .shtc3_humidity                  = NAN,
        .luminosity        = (INVALID_LUMINOSITY == avg_data.luminosity) ? NAN : (float)avg_data.luminosity,
        .sound_inst_dba    = (INVALID_SOUND_DBA == avg_data.sound_inst_dba_x100)
                                 ? NAN
                                 : (float)avg_data.sound_inst_dba_x100 / 100.0f,
        .sound_avg_dba     = (INVALID_SOUND_DBA == avg_data.sound_avg_dba_x100)
                                 ? NAN
                                 : (float)avg_data.sound_avg_dba_x100 / 100.0f,
        .sound_peak_spl_db = (INVALID_SOUND_DBA == avg_data.sound_peak_spl_db_x100)
                                 ? NAN
                                 : (float)avg_data.sound_peak_spl_db_x100 / 100.0f,
    };

    const re_e1_data_t e1_data = data_fmt_e1_init(
        &measurement_avg,
        measurement_cnt,
        radio_mac,
        (re_e1_flags_t) {
            .flag_calibration_in_progress = flags.flag_calibration_in_progress,
            .flag_button_pressed          = flags.flag_button_pressed,
            .flag_rtc_running_on_boot     = flags.flag_rtc_running_on_boot,
        });
    uint8_t buffer[RE_E1_DATA_LENGTH];
    (void)re_e1_encode(buffer, &e1_data);
    hist_log_record_data_t record = { 0 };
    memcpy(&record.buf[0], buffer, sizeof(record.buf));
    return record;
}
