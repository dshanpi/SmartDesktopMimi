/**
 * @file datasink_mem.c
 * @brief 
 * @version 0.1
 * @date 2025-09-23
 * 
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 * 
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying 
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 */

#include "tal_api.h"
#include "datasink_cfg.h"
#include "tuya_ringbuf.h"

#ifndef AI_PLAYER_MEM_PREBUFFER_SIZE
#define AI_PLAYER_MEM_PREBUFFER_SIZE (AI_PLAYER_FRAMEBUF_SIZE * 2)
#endif

typedef struct {
    TUYA_RINGBUFF_T ringbuf;
    MUTEX_HANDLE mutex;
    bool eof;
    bool buffering;
    uint32_t underrun_count;
} MEM_DATASINK_CTX_T;

OPERATE_RET datasink_mem_start(char *value, void* *handle)
{
    OPERATE_RET rt = OPRT_OK;
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)(*handle);

    if(ctx != NULL) {
        tal_mutex_lock(ctx->mutex);
        tuya_ring_buff_reset(ctx->ringbuf);
        ctx->eof = false;
        ctx->buffering = true;
        ctx->underrun_count = 0;
        tal_mutex_unlock(ctx->mutex);

        return OPRT_OK;
    }

    ctx = (MEM_DATASINK_CTX_T *)Malloc(sizeof(MEM_DATASINK_CTX_T));
    if (ctx == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    memset(ctx, 0, sizeof(MEM_DATASINK_CTX_T));
    ctx->buffering = true;
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&ctx->mutex));
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    TUYA_CALL_ERR_RETURN(tuya_ring_buff_create(AI_PLAYER_RINGBUF_SIZE, OVERFLOW_PSRAM_STOP_TYPE, &ctx->ringbuf));
#else
    TUYA_CALL_ERR_RETURN(tuya_ring_buff_create(AI_PLAYER_RINGBUF_SIZE, OVERFLOW_STOP_TYPE, &ctx->ringbuf));
#endif

    *handle = (void*)ctx;
    return OPRT_OK;
}

OPERATE_RET datasink_mem_stop(void* handle)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(ctx->mutex);
    tuya_ring_buff_reset(ctx->ringbuf);
    tal_mutex_unlock(ctx->mutex);

    return OPRT_OK;
}

OPERATE_RET datasink_mem_exit(void* handle)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(ctx->mutex);
    tuya_ring_buff_free(ctx->ringbuf);
    ctx->ringbuf = NULL;
    tal_mutex_unlock(ctx->mutex);

    tal_mutex_release(ctx->mutex);
    Free(ctx);

    return OPRT_OK;
}

OPERATE_RET datasink_mem_feed(void* handle, uint8_t *data, uint32_t len)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }

    if((data == NULL) && (len == 0)) { // eof
        tal_mutex_lock(ctx->mutex);
        uint32_t used = tuya_ring_buff_used_size_get(ctx->ringbuf);
        ctx->eof = true;
        tal_mutex_unlock(ctx->mutex);
        PR_NOTICE("player memory stream eof set with %u buffered bytes", used);
        return OPRT_OK;
    }

    if(data == NULL || len == 0) {
        return OPRT_INVALID_PARM;
    }

    uint8_t *cursor = data;
    uint32_t remaining = len;
    uint32_t stalled_ms = 0;

    while (remaining > 0) {
        int written;

        tal_mutex_lock(ctx->mutex);
        written = tuya_ring_buff_write(ctx->ringbuf, (char *)cursor, remaining);
        tal_mutex_unlock(ctx->mutex);

        if (written < 0 || (uint32_t)written > remaining) {
            PR_ERR("player ring buf write error %d, remaining %u", written, remaining);
            return written < 0 ? written : OPRT_COM_ERROR;
        }

        if (written > 0) {
            cursor += written;
            remaining -= (uint32_t)written;
            stalled_ms = 0;
            continue;
        }

        if (stalled_ms >= 5000) {
            PR_ERR("player ring buf write timed out, dropped %u of %u bytes",
                   remaining, len);
            return OPRT_TIMEOUT;
        }

        tal_system_sleep(10);
        stalled_ms += 10;
    }

    return OPRT_OK;
}

OPERATE_RET datasink_mem_read(void* handle, uint8_t *data, uint32_t len, uint32_t *out_len)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL || data == NULL || len == 0 || out_len == NULL) {
        return OPRT_INVALID_PARM;
    }

    uint32_t used;
    int rt = 0;

    tal_mutex_lock(ctx->mutex);
    used = tuya_ring_buff_used_size_get(ctx->ringbuf);

    /*
     * A cloud TTS stream arrives in bursts.  Starting the decoder on the first
     * packet drains the compressed data before the next burst and repeatedly
     * sends ALSA into XRUN recovery.  Hold the first read (and any read after
     * a real starvation) until a small reservoir is available.  EOF always
     * releases the reservoir so short prompts are not delayed forever.
     */
    if (ctx->buffering) {
        if (!ctx->eof && used < AI_PLAYER_MEM_PREBUFFER_SIZE) {
            *out_len = 0;
            tal_mutex_unlock(ctx->mutex);
            return OPRT_OK;
        }

        ctx->buffering = false;
        PR_NOTICE("player memory stream buffered %u bytes%s",
                  used, ctx->eof ? " at eof" : "");
    } else if (!ctx->eof && used == 0) {
        ctx->buffering = true;
        ctx->underrun_count++;
        PR_WARN("player memory stream starved (%u), buffering again",
                ctx->underrun_count);
        *out_len = 0;
        tal_mutex_unlock(ctx->mutex);
        return OPRT_OK;
    }

    rt = tuya_ring_buff_read(ctx->ringbuf, (char *)data, len);
    *out_len = rt;

    if((rt == 0) && (ctx->eof == true)) {
        tal_mutex_unlock(ctx->mutex);
        PR_NOTICE("player memory stream eof consumed after %u underruns",
                  ctx->underrun_count);
        return OPRT_NOT_FOUND; // eof
    }

    tal_mutex_unlock(ctx->mutex);
    return OPRT_OK;
}

DATASINK_T g_datasink_mem = {
    .start = datasink_mem_start,
    .stop  = datasink_mem_stop,
    .exit  = datasink_mem_exit,
    .feed  = datasink_mem_feed,
    .read  = datasink_mem_read,
};
