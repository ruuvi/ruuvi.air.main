/**
 * @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 */

#include "mic_pdm.h"
#include <zephyr/devicetree.h>

#if !defined(CONFIG_RUUVI_AIR_MIC_NONE)

#if DT_NODE_EXISTS(DT_NODELABEL(dmic_dev)) && DT_NODE_HAS_STATUS(DT_NODELABEL(dmic_dev), okay)

#include <assert.h>
#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/dsp/types.h>
#include "dsp_rms.h"
#include "tlog.h"
#include "dsp_rms.h"
#include "spl_calc.h"
#if CONFIG_RUUVI_AIR_MIC_SPG08P4HM4H
#include "mic_spg08p4hm4h.h"
#else
#error "No microphone selected"
#endif

LOG_MODULE_REGISTER(mic_pdm, LOG_LEVEL_INF);

#define MIC_PDM_SAMPLE_BIT_WIDTH 16

#define MIC_PDM_BLOCK_COUNT (10)

#define MAX_Q15 (32767)

/* Milliseconds to wait for a block to be read. */
#define READ_TIMEOUT (MIC_PDM_BLOCK_DURATION_MS * (MIC_PDM_BLOCK_COUNT - 1))

/* Driver will allocate blocks from this slab to receive audio data into them.
 * Application, after getting a given block from the driver and processing its
 * data, needs to free that block.
 */
K_MEM_SLAB_DEFINE_STATIC(g_mem_slab, MIC_PDM_MAX_BLOCK_SIZE, MIC_PDM_BLOCK_COUNT, sizeof(uint32_t));

static K_MUTEX_DEFINE(mic_pdm_mutex);
static uint8_t   g_max_spl_db;
static uint8_t   g_avg_db_a;
static uint8_t   g_inst_db_a;
static float32_t g_buf_f32[MIC_PDM_NUM_SAMPLES_IN_BLOCK];

static void
mic_pdm_thread(void* p1, void* p2, void* p3);

K_THREAD_DEFINE(
    mic_pdm_tid,
    CONFIG_RUUVI_AIR_MIC_PDM_THREAD_STACK_SIZE,
    &mic_pdm_thread,
    NULL,
    NULL,
    NULL,
    CONFIG_RUUVI_AIR_MIC_PDM_THREAD_PRIORITY,
    0,
    1000 /* Scheduling delay (in milliseconds) */);

static spl_db_t
spl_calc_db(const float32_t rms)
{
    float32_t spl_db = NAN;
    if (rms > 0)
    {
        float32_t output_dbfs = 20 * log10f(rms);
        spl_db                = MIC_REFERENCE_SPL_DB + (output_dbfs - MIC_SENSITIVITY_DBFS);
        if (spl_db < 0.0f)
        {
            spl_db = NAN;
        }
    }
    const spl_db_t spl_db_int8 = isnan(spl_db) ? 0 : (spl_db_t)lrintf(spl_db);
    return spl_db_int8;
}

static void
convert_buf_q15_to_float(const q15_t* const p_q15_buffer, float32_t* const p_float_buffer, const uint32_t num_samples)
{
    for (uint32_t i = 0; i < num_samples; i++)
    {
        p_float_buffer[i] = (float32_t)p_q15_buffer[i] / MAX_Q15;
    }
}

static void
mic_pdm_thread(void* p1, void* p2, void* p3)
{
    TLOG_INF("Start MIC PDM thread");

    spl_calc_init();

    const struct device* const p_dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
    if (NULL == p_dmic_dev)
    {
        TLOG_ERR("Could not get PDM device");
        return;
    }
    if (!device_is_ready(p_dmic_dev))
    {
        TLOG_ERR("%s is not ready", p_dmic_dev->name);
        return;
    }

    struct pcm_stream_cfg stream = {
        .pcm_width = MIC_PDM_SAMPLE_BIT_WIDTH,
        .mem_slab  = &g_mem_slab,
    };
    struct dmic_cfg cfg = {
        .io = {
            /* These fields can be used to limit the PDM clock
             * configurations that the driver is allowed to use
             * to those supported by the microphone.
             */
            .min_pdm_clk_freq = MIC_MIN_PDM_CLK_FREQ,
            .max_pdm_clk_freq = MIC_MAX_PDM_CLK_FREQ,
            .min_pdm_clk_dc   = 40,
            .max_pdm_clk_dc   = 60,
        },
        .streams = &stream,
        .channel = {
            .req_num_streams = 1,
        },
    };

    cfg.channel.req_num_chan    = 1;
    cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
    cfg.streams[0].pcm_rate     = MIC_PDM_SAMPLE_RATE;
    cfg.streams[0].block_size   = MIC_PDM_BLOCK_SIZE(cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);

    TLOG_INF(
        "PCM output rate: %u, channels: %u, block_count: %u",
        cfg.streams[0].pcm_rate,
        cfg.channel.req_num_chan,
        MIC_PDM_BLOCK_COUNT);

    int ret = dmic_configure(p_dmic_dev, &cfg);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure the driver: %d", ret);
        return;
    }

    ret = dmic_trigger(p_dmic_dev, DMIC_TRIGGER_START);
    if (ret < 0)
    {
        TLOG_ERR("START trigger failed: %d", ret);
        return;
    }

    uint32_t first_blocks_cnt = 0;
    while (1)
    {
        void*    buffer = NULL;
        uint32_t size   = 0;

        ret = dmic_read(p_dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
        if (ret < 0)
        {
            TLOG_ERR("dmic_read failed: %d", ret);
            TLOG_WRN("DMIC_TRIGGER_STOP");
            ret = dmic_trigger(p_dmic_dev, DMIC_TRIGGER_STOP);
            if (ret < 0)
            {
                TLOG_ERR("STOP trigger failed: %d", ret);
                return;
            }
            TLOG_WRN("DMIC_TRIGGER_START");
            ret = dmic_trigger(p_dmic_dev, DMIC_TRIGGER_START);
            if (ret < 0)
            {
                TLOG_ERR("STOP trigger failed: %d", ret);
                return;
            }
            continue;
        }
        assert(size == MIC_PDM_NUM_SAMPLES_IN_BLOCK * MIC_PDM_BYTES_PER_SAMPLE);

        if (first_blocks_cnt < (1 * 1000 / MIC_PDM_BLOCK_DURATION_MS))
        {
            first_blocks_cnt++;
        }
        else
        {
            convert_buf_q15_to_float((const q15_t*)buffer, g_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK);
            if (spl_calc_handle_buffer(buffer, g_buf_f32, MIC_PDM_NUM_SAMPLES_IN_BLOCK))
            {
                const float32_t last_max_rms = spl_calc_get_rms_last_max();
                const float32_t last_avg_rms = spl_calc_get_rms_last_avg();
                TLOG_DBG("Last Avg RMS: %f, SPL: %d dB(A)", (double)last_avg_rms, (int)spl_calc_db(last_avg_rms));
                TLOG_DBG("Last Max RMS: %f, SPL: %d SPL dB", (double)last_max_rms, (int)spl_calc_db(last_max_rms));
                const float32_t avg_filtered_rms       = spl_calc_get_rms_avg();
                const float32_t max_unfiltered_rms     = spl_calc_get_rms_max();
                const bool      flag_mic_invalid       = (0 == spl_calc_db(last_max_rms)) ? true : false;
                const spl_db_t  inst_filtered_spl_db_a = flag_mic_invalid ? SPL_DB_INVALID : spl_calc_db(last_avg_rms);
                const spl_db_t  avg_filtered_spl_db_a  = flag_mic_invalid ? SPL_DB_INVALID
                                                                          : spl_calc_db(avg_filtered_rms);
                const spl_db_t  max_unfiltered_spl_db  = flag_mic_invalid ? SPL_DB_INVALID
                                                                          : spl_calc_db(max_unfiltered_rms);
                TLOG_DBG("Avg RMS (filtered): %f, SPL: %d dB(A)", (double)avg_filtered_rms, avg_filtered_spl_db_a);
                TLOG_DBG("Max RMS (unfiltered): %f, SPL: %d SPL dB", (double)max_unfiltered_rms, max_unfiltered_spl_db);
                k_mutex_lock(&mic_pdm_mutex, K_FOREVER);
                g_inst_db_a  = inst_filtered_spl_db_a;
                g_avg_db_a   = avg_filtered_spl_db_a;
                g_max_spl_db = max_unfiltered_spl_db;
                k_mutex_unlock(&mic_pdm_mutex);
            }
        }
        k_mem_slab_free(&g_mem_slab, buffer);
    }
}

#endif // DT_NODE_EXISTS(DT_NODELABEL(dmic_dev)) && DT_NODE_HAS_STATUS(DT_NODELABEL(dmic_dev), okay)

#endif /* CONFIG_RUUVI_AIR_MIC_NONE */

void
mic_pdm_get_measurements(spl_db_t* const p_inst_db_a, spl_db_t* const p_avg_db_a, spl_db_t* const p_max_spl_db)
{
#if defined(CONFIG_RUUVI_AIR_MIC_NONE) \
    || !(DT_NODE_EXISTS(DT_NODELABEL(dmic_dev)) && DT_NODE_HAS_STATUS(DT_NODELABEL(dmic_dev), okay))
    *p_inst_db_a  = SPL_DB_INVALID;
    *p_avg_db_a   = SPL_DB_INVALID;
    *p_max_spl_db = SPL_DB_INVALID;
#else
    k_mutex_lock(&mic_pdm_mutex, K_FOREVER);
    *p_inst_db_a  = g_inst_db_a;
    *p_avg_db_a   = g_avg_db_a;
    *p_max_spl_db = g_max_spl_db;
    k_mutex_unlock(&mic_pdm_mutex);
#endif
}
