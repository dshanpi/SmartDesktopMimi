/**
 * @file tkl_vad.c
 * @version 0.1
 * @date 2025-04-16
 */

#include "tkl_audio.h"
#include "tkl_memory.h"
#include "tkl_queue.h"
#include "tkl_thread.h"
#include "tkl_system.h"
#include "tkl_vad.h"

#include "tkl_output.h"

#if defined(ENABLE_AUDIO_ALSA) && (ENABLE_AUDIO_ALSA == 1)
#include "tdd_audio_alsa.h"
#endif

#include "stdio.h"
#include <pthread.h>

/***********************************************************
************************macro define************************
***********************************************************/
#define AEC_VAD_FRAME_SIZE (640) // 20ms 16kHz 16bit mono

// 500ms buffer for 16kHz, 16bit mono
#define AEC_VAD_RING_BUFF_LEN    (AEC_VAD_FRAME_SIZE * (500 / 20))

/***********************************************************
***********************typedef define***********************
***********************************************************/
/* Private configuration expected by the vendor rnn_vad_init() ABI. */
typedef struct {
    float speech_min_ms;
    float noise_min_ms;
} RNN_VAD_TIMING_CONFIG_T;

/***********************************************************
********************function declaration********************
***********************************************************/
extern void *speex_aes_create(int framesize);
extern void speex_aes_destory(void *obj);
extern int speex_aes_process(void *obj, short *mic, short *ref, short *aec);
extern float speex_get_param(void *obj, float *out, short *linearout);

extern void *rnn_vad_create();
extern void rnn_vad_destroy(void *obj);
extern int rnn_vad_init(RNN_VAD_TIMING_CONFIG_T *config, void *obj);
extern void rnn_vad_start(void *obj);
extern void rnn_vad_stop(void *obj);
extern float rnn_vad_process(void *obj, short *x);
extern void rnn_vad_set_callback(void *obj, float threshold);
extern OPERATE_RET tkl_asr_feed_with_vad(uint8_t *data, uint16_t datalen, uint8_t vadflag);
/***********************************************************
***********************variable define**********************
***********************************************************/
static void *__s_speex_aec_handle = NULL;
static void *__s_rnn_vad_handle = NULL;
static uint16_t *__s_linearaec = NULL;
static uint32_t __s_frame_size = 0;
static TKL_VAD_STATUS_T __s_aec_vad_flag = TKL_VAD_STATUS_NONE;

static uint8_t sg_mic_data[AEC_VAD_RING_BUFF_LEN] = {0};
static uint32_t sg_mic_data_len = 0;
static uint8_t sg_ref_data[AEC_VAD_RING_BUFF_LEN] = {0};
static uint32_t sg_ref_data_len = 0;
static uint8_t sg_out_data[AEC_VAD_FRAME_SIZE] = {0};
/* The capture thread feeds VAD while the AI mode thread starts/stops it.
 * Serialize both the vendor handle and the reframe lengths: resetting a
 * length between a callback's bounds check and memmove would underflow the
 * size and crash the Tuya process. */
static pthread_mutex_t sg_vad_process_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t __audio_mean_abs(const int16_t *data, uint32_t samples)
{
    uint64_t total = 0;
    uint32_t i;

    for (i = 0; i < samples; ++i) {
        int32_t value = data[i];
        total += (uint32_t)(value < 0 ? -value : value);
    }
    return samples == 0 ? 0 : (uint32_t)(total / samples);
}

/***********************************************************
***********************function define**********************
***********************************************************/
static OPERATE_RET __tkl_aec_vad_process(int16_t *mic_data, int16_t *ref_data, int16_t *out_data)
{
    OPERATE_RET rt = OPRT_OK;

    if (mic_data == NULL || ref_data == NULL || out_data == NULL) {
        tkl_log_output("Invalid parameter: mic_data, ref_data, or out_data is NULL\r\n");
        return OPRT_INVALID_PARM;
    }

    if (__s_speex_aec_handle) {
        speex_aes_process(__s_speex_aec_handle, (short *)mic_data, (short *)ref_data, (short *)out_data);
    }

    if (__s_speex_aec_handle && __s_rnn_vad_handle && __s_linearaec) {
        int has_vad = (int)rnn_vad_process(__s_rnn_vad_handle, (short *)out_data);
        if (has_vad && __s_aec_vad_flag != TKL_VAD_STATUS_SPEECH) {
            tkl_log_output("################ [vad start] mic=%u ref=%u aec=%u ################\r\n",
                           __audio_mean_abs(mic_data, AEC_VAD_FRAME_SIZE / 2),
                           __audio_mean_abs(ref_data, AEC_VAD_FRAME_SIZE / 2),
                           __audio_mean_abs(out_data, AEC_VAD_FRAME_SIZE / 2));
            __s_aec_vad_flag = TKL_VAD_STATUS_SPEECH;
        } else if (!has_vad && __s_aec_vad_flag != TKL_VAD_STATUS_NONE) {
            tkl_log_output("################ [vad stop] ################\r\n");
            __s_aec_vad_flag = TKL_VAD_STATUS_NONE;
        }

        speex_get_param(__s_speex_aec_handle, NULL, (short *)__s_linearaec);
    }

    return rt;
}

OPERATE_RET tkl_vad_set_threshold(TKL_AUDIO_VAD_THRESHOLD_E level)
{
    if (__s_rnn_vad_handle) {
        switch (level) {
        case TKL_AUDIO_VAD_HIGH:
            rnn_vad_set_callback(__s_rnn_vad_handle, -40);
            break;
        case TKL_AUDIO_VAD_MID:
            rnn_vad_set_callback(__s_rnn_vad_handle, -60);
            break;
        case TKL_AUDIO_VAD_LOW:
            rnn_vad_set_callback(__s_rnn_vad_handle, -80);
            break;
        default:
            break;
        }

        return OPRT_OK;
    }

    return OPRT_RESOURCE_NOT_READY;
}

void __tkl_vad_process(int16_t *mic_data, int16_t *ref_data, int16_t *out_data, uint32_t frames)
{
    if (mic_data == NULL || ref_data == NULL || out_data == NULL || frames == 0) {
        tkl_log_output("Invalid parameter: mic_data, ref_data, out_data is NULL or frames is 0\r\n");
        return;
    }
    const uint32_t data_len = frames * sizeof(int16_t);

    pthread_mutex_lock(&sg_vad_process_lock);

    if (sg_mic_data_len + (frames * 2) > AEC_VAD_RING_BUFF_LEN ||
        sg_ref_data_len + (frames * 2) > AEC_VAD_RING_BUFF_LEN) {
        // buffer full, discard data
        // tkl_log_output("tkl_vad_process buffer full, discard data\r\n");
        pthread_mutex_unlock(&sg_vad_process_lock);
        return;
    }

    memcpy(sg_mic_data + sg_mic_data_len, (uint8_t *)mic_data, data_len);
    sg_mic_data_len += data_len;
    memcpy(sg_ref_data + sg_ref_data_len, (uint8_t *)ref_data, data_len);
    sg_ref_data_len += data_len;

    /*
     * ALSA periods are not required to match the 320-sample frame consumed by
     * Speex/RNN VAD (the A133 configuration currently returns 512 samples).
     * The old code buffered a 20 ms frame but then accidentally processed the
     * caller's current period instead.  That skipped the tail of every ALSA
     * period and repeatedly broke the VAD stream's frame continuity.  Process
     * the accumulated buffers and drain every complete 20 ms frame instead.
     */
    while (sg_mic_data_len >= AEC_VAD_FRAME_SIZE &&
           sg_ref_data_len >= AEC_VAD_FRAME_SIZE) {
        __tkl_aec_vad_process((int16_t *)sg_mic_data,
                              (int16_t *)sg_ref_data,
                              (int16_t *)sg_out_data);

        memmove(sg_mic_data, sg_mic_data + AEC_VAD_FRAME_SIZE, sg_mic_data_len - AEC_VAD_FRAME_SIZE);
        sg_mic_data_len -= AEC_VAD_FRAME_SIZE;
        memmove(sg_ref_data, sg_ref_data + AEC_VAD_FRAME_SIZE, sg_ref_data_len - AEC_VAD_FRAME_SIZE);
        sg_ref_data_len -= AEC_VAD_FRAME_SIZE;
    }

    pthread_mutex_unlock(&sg_vad_process_lock);
}

OPERATE_RET tkl_vad_init(TKL_VAD_CONFIG_T *config)
{
    OPERATE_RET rt = OPRT_OK;
    RNN_VAD_TIMING_CONFIG_T timing_cfg;

    if (NULL == config) {
        return OPRT_INVALID_PARM;
    }

    if (__s_speex_aec_handle == NULL) {
        __s_speex_aec_handle = speex_aes_create(AEC_VAD_FRAME_SIZE / 2);
        if (__s_speex_aec_handle == NULL) {
            tkl_log_output("__s_speex_aec_handle create failed\r\n");
            goto __err_exit;
        }
    }

    if (__s_rnn_vad_handle == NULL) {
        __s_rnn_vad_handle = rnn_vad_create();
        if (__s_rnn_vad_handle == NULL) {
            tkl_log_output("__s_rnn_vad_handle create failed\r\n");
            goto __err_exit;
        }

        /* rnn_vad_create() leaves the library at its internal 1600/200 ms
         * defaults.  Apply the recorder's requested 200/1000 ms timing so a
         * normal question reliably opens the upload session and short pauses
         * do not cut it off. */
        timing_cfg.speech_min_ms = (float)config->speech_min_ms;
        timing_cfg.noise_min_ms = (float)config->noise_min_ms;
        if (rnn_vad_init(&timing_cfg, __s_rnn_vad_handle) < 0) {
            tkl_log_output("__s_rnn_vad_handle init failed\r\n");
            goto __err_exit;
        }

        tkl_vad_set_threshold(TKL_AUDIO_VAD_MID);
    }

    if (__s_linearaec == NULL) {
#ifdef ENABLE_EXT_RAM
        __s_linearaec = tkl_system_psram_malloc(AEC_VAD_FRAME_SIZE * 2);
        if (NULL == __s_linearaec) {
            tkl_log_output("__s_linearaec psram malloc failed\r\n");
            goto __err_exit;
        }
#else
        __s_linearaec = tkl_system_malloc(AEC_VAD_FRAME_SIZE * 2);
        if (NULL == __s_linearaec) {
            tkl_log_output("__s_linearaec malloc failed\r\n");
            goto __err_exit;
        }
#endif
    }

    __s_frame_size = AEC_VAD_FRAME_SIZE;

#if defined(ENABLE_AUDIO_ALSA) && (ENABLE_AUDIO_ALSA == 1)
    tdd_audio_alsa_register_vad_feed_cb(__tkl_vad_process);
#endif

    return OPRT_OK;

__err_exit:
    tkl_vad_deinit();
    return rt;
}

OPERATE_RET tkl_vad_feed(uint8_t *data, uint32_t len)
{
    // Nothing to do
    // T5 vad feed is called in _aec_v3_algorithm_process() function in
    // platform/T5AI/t5_os/ap/components/bk_audio/audio_algorithms/aec_v3_algorithm/aec_v3_algorithm.c

    return OPRT_OK;
}

TKL_VAD_STATUS_T tkl_vad_get_status(void)
{
    return __s_aec_vad_flag;
}

OPERATE_RET tkl_vad_start(void)
{
    pthread_mutex_lock(&sg_vad_process_lock);
    __s_aec_vad_flag = TKL_VAD_STATUS_NONE;
    sg_mic_data_len = 0;
    sg_ref_data_len = 0;
    if (__s_rnn_vad_handle) {
        rnn_vad_start(__s_rnn_vad_handle);
    }
    pthread_mutex_unlock(&sg_vad_process_lock);

    return OPRT_OK;
}

OPERATE_RET tkl_vad_stop(void)
{
    pthread_mutex_lock(&sg_vad_process_lock);
    __s_aec_vad_flag = TKL_VAD_STATUS_NONE;
    sg_mic_data_len = 0;
    sg_ref_data_len = 0;
    if (__s_rnn_vad_handle) {
        rnn_vad_stop(__s_rnn_vad_handle);
    }
    pthread_mutex_unlock(&sg_vad_process_lock);

    return OPRT_OK;
}

OPERATE_RET tkl_vad_deinit(void)
{
    tdd_audio_alsa_unregister_vad_feed_cb();
    pthread_mutex_lock(&sg_vad_process_lock);

    if (__s_linearaec) {
#ifdef ENABLE_EXT_RAM
        tkl_system_psram_free(__s_linearaec);
#else
        tkl_system_free(__s_linearaec);
#endif
        __s_linearaec = NULL;
    }

    if (__s_rnn_vad_handle) {
        rnn_vad_destroy(__s_rnn_vad_handle);
        __s_rnn_vad_handle = NULL;
    }

    if (__s_speex_aec_handle) {
        speex_aes_destory(__s_speex_aec_handle);
        __s_speex_aec_handle = NULL;
    }

    __s_frame_size = 0;
    sg_mic_data_len = 0;
    sg_ref_data_len = 0;

    pthread_mutex_unlock(&sg_vad_process_lock);

    return OPRT_OK;
}
