/**
 * @file tkl_kws.c
 * @version 0.1
 * @date 2025-04-08
 */

#include "tuya_cloud_types.h"

#include "tkl_memory.h"
#include "tkl_system.h"
#include "tkl_thread.h"
#include "tkl_mutex.h"
#include "tkl_semaphore.h"
#include "tuya_ringbuf.h"
#include <stdint.h>
#include <stdio.h>

#include "tkl_kws.h"

#if defined(ENABLE_AUDIO_ALSA) && (ENABLE_AUDIO_ALSA == 1)
#include "tdd_audio_alsa.h"
#endif

#include "kws/audio_subsys_kws_factory2.h"

/***********************************************************
************************macro define************************
***********************************************************/
// mono 16k 16bit, 20ms frame -> 320bytes
#define TKL_KWS_FRAME_SIZE   (320 * 2) // 16bit
#define KWS_RING_BUFF_LEN    (TKL_KWS_FRAME_SIZE * (2000 / 20)) // 2s buffer for 16kHz, 16bit audio

#define MODEL_CHUNK_TIME_MS   (300) // 300ms chunk for kws model
#define MODEL_CHUNK_SIZE      (MODEL_CHUNK_TIME_MS / 20 * TKL_KWS_FRAME_SIZE) // 16kHz, 16bit

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    uint32_t   w32WakeWord;
    char *asr_txt;
    TKL_KWS_WAKEUP_WORD_E wakeup_word;
} ASR_WAKEUP_WORD_MAP_T;

typedef struct {
    audio_subsys_module_t* module;
    TKL_KWS_WAKEUP_CB wakeup_cb;
    TKL_THREAD_HANDLE kws_thread;
    TUYA_RINGBUFF_T mic_ringbuf;
} TKL_KWS_CTX_S;

/***********************************************************
***********************variable define**********************
***********************************************************/

static ASR_WAKEUP_WORD_MAP_T cASR_WAKEUP_WORD_MAP[] = {
    {1, "你好涂鸦",    TKL_KWS_WAKEUP_NIHAO_TUYA}
};

static TKL_KWS_CTX_S g_kws_ctx = {0};

/***********************************************************
***********************function define**********************
***********************************************************/
void __tkl_kws_process(int16_t *mic_data, int16_t *ref_data, int16_t *out_data, uint32_t frames)
{
    if (g_kws_ctx.module == NULL || g_kws_ctx.mic_ringbuf == NULL) {
        return;
    }

    uint32_t rb_free = tuya_ring_buff_free_size_get(g_kws_ctx.mic_ringbuf);
    if (rb_free < frames * 2) {
        // ringbuf full, discard data
        return;
    }
    tuya_ring_buff_write(g_kws_ctx.mic_ringbuf, (uint8_t *)mic_data, frames * 2);

    return;
}

static int __tkl_kws_result(void* data, int dlen, int flag, void* userdata) {
    if (flag == 1 && g_kws_ctx.wakeup_cb != NULL) {
        g_kws_ctx.wakeup_cb(TKL_KWS_WAKEUP_NIHAO_TUYA);
        // printf("data[%d] %p %s, flag %d, kws_count %d\n",
        //     dlen, data, (char*)data, flag, kws_count);
    }


    return 0;
}

static void __tkl_kws_task(void *args)
{
    uint8_t *mic_data = NULL;

	// OPERATE_RET rt = 0;
    for (;;) {
        // read mic data from ringbuf
        uint32_t rb_data_len = tuya_ring_buff_used_size_get(g_kws_ctx.mic_ringbuf);
        if (rb_data_len < MODEL_CHUNK_SIZE) {
            tkl_system_sleep(10);
            continue;;
        }

        if (mic_data == NULL) {
            mic_data = (uint8_t *)tkl_system_malloc(MODEL_CHUNK_SIZE);
            if (mic_data == NULL) {
                tkl_system_sleep(10);
                continue;;
            }
        }

        memset(mic_data, 0, MODEL_CHUNK_SIZE);
        tuya_ring_buff_read(g_kws_ctx.mic_ringbuf, mic_data, MODEL_CHUNK_SIZE);

        g_kws_ctx.module->process(g_kws_ctx.module->self, (char *)mic_data, MODEL_CHUNK_SIZE, NULL, MODEL_CHUNK_SIZE);

        tkl_system_sleep(10);
    }

}

OPERATE_RET tkl_kws_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    char *modelpath = KWS_MODEL_PATH;
    char *tokenpath = KWS_MODEL_TOKEN_PATH;

    if (modelpath == NULL || strlen(modelpath) == 0) {
        modelpath = "~/tuyaopen_models/mdtc_chunk_300ms.mnn";
    }
    if (tokenpath == NULL || strlen(tokenpath) == 0) {
        tokenpath = "~/tuyaopen_models/tokens.txt";
    }

    // ringbuf create for mic data
    rt = tuya_ring_buff_create(KWS_RING_BUFF_LEN, OVERFLOW_STOP_TYPE, &g_kws_ctx.mic_ringbuf);
    if (rt != OPRT_OK) {
        printf("kws mic ringbuff create failed\n");
        return rt;
    }

    /* This model is a fixed two-class wake-word classifier trained for
     * "你好涂鸦".  model_type 0 does not consume a configurable token sequence;
     * changing only this string cannot add an English wake phrase. */
    char keyword[] = "你好涂鸦";

    kws_factory2_config_t kwsconfig;
    kwsconfig.interpreter_type = 2; // mnn  
    kwsconfig.chunk_size = 300 * 16; // 300ms, 16k
    kwsconfig.feats_size = 30;
    kwsconfig.sample_rate = 16000;
    kwsconfig.interval = 100;
    kwsconfig.model_type = 0;
    kwsconfig.num_bins = 80;
    kwsconfig.batch_size = 10;
    kwsconfig.threshold = 0.8;
    kwsconfig.key_word = keyword;
    kwsconfig.model_path = modelpath;
    kwsconfig.token_path = tokenpath;

    int err = 0;
    g_kws_ctx.module = &audio_subsys_kws_factory2;
    int ret = g_kws_ctx.module->create(&g_kws_ctx.module->self, &kwsconfig);
    if (ret < 0) {
        printf("kws module create failed\n");
        return -1;
    }

    err = g_kws_ctx.module->set_callback(g_kws_ctx.module->self, __tkl_kws_result, NULL);
    if (err != 0) {
        printf("kws module set callback failed\n");
        tkl_kws_deinit();
        return -1;
    }

    tkl_thread_create(&g_kws_ctx.kws_thread,
                        "tkl_kws_task",
                        4096,
                        4,
                        __tkl_kws_task,
                        NULL);

#if defined(ENABLE_AUDIO_ALSA) && (ENABLE_AUDIO_ALSA == 1)
    tdd_audio_alsa_register_kws_feed_cb(__tkl_kws_process);
#endif

    return rt;
}

OPERATE_RET tkl_kws_enable(void)
{
    return OPRT_OK;
}

OPERATE_RET tkl_kws_disable(void)
{
    return OPRT_OK;
}

OPERATE_RET tkl_kws_reg_wakeup_cb(TKL_KWS_WAKEUP_CB wakeup_cb)
{
    g_kws_ctx.wakeup_cb = wakeup_cb;
    return OPRT_OK;
}

OPERATE_RET tkl_kws_deinit(void)
{
    if (g_kws_ctx.module == NULL) {
        return OPRT_OK;
    }

    if (g_kws_ctx.kws_thread != NULL) {
        tuya_ring_buff_free(g_kws_ctx.kws_thread);
    }

    tkl_thread_release(g_kws_ctx.kws_thread);

    g_kws_ctx.module->destroy(g_kws_ctx.module->self);

    g_kws_ctx.module = NULL;

    return OPRT_OK;
}
