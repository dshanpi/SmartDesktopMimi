/**
 * @file tdd_audio_alsa.c
 * @brief Implementation of Tuya Device Driver layer audio interface for ALSA.
 *
 * This file implements the device driver interface for audio functionality using
 * ALSA (Advanced Linux Sound Architecture) on Linux/Ubuntu platforms. It provides
 * the implementation for audio device initialization, configuration, volume control,
 * and audio data handling. The driver supports both audio input (microphone) and
 * output (speaker) operations with configurable parameters.
 *
 * Key functionalities include:
 * - ALSA device registration and initialization
 * - Microphone data capture with threaded callback mechanism
 * - Speaker playback with ALSA PCM interface
 * - Volume control through ALSA mixer API
 * - Frame-based audio data processing
 * - Proper resource cleanup and error handling
 *
 * This implementation bridges the ALSA library APIs with the higher-level
 * TDL (Tuya Driver Layer) audio management system.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_cloud_types.h"

#if defined(ENABLE_AUDIO_ALSA) && (ENABLE_AUDIO_ALSA == 1)

#include <alsa/asoundlib.h>
#include <pthread.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_thread.h"

#include "tdd_audio_alsa.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define AUDIO_PCM_FRAME_MS       10
#define AEC_REF_QUEUE_MS         2000
#define AEC_REF_START_DELAY_MS   64

#define ALSA_CAPTURE_THREAD_STACK_SIZE (4096)
#define ALSA_CAPTURE_THREAD_PRIORITY   (THREAD_PRIO_2)

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    TDD_AUDIO_ALSA_CFG_T cfg;
    TDL_AUDIO_MIC_CB mic_cb;

    // ALSA handles
    snd_pcm_t *capture_handle;
    snd_pcm_t *playback_handle;
    snd_mixer_t *mixer_handle;
    snd_mixer_elem_t *mixer_elem;

    // Capture thread
    pthread_t capture_thread;
    volatile uint8_t capture_running;

    // Thread synchronization for ref_buffer
    pthread_mutex_t ref_buffer_mutex;

    // Playback settings
    uint8_t play_volume;
    long mixer_min;
    long mixer_max;

    // Buffer
    uint8_t *capture_buffer;
    uint32_t capture_buffer_size;

    uint8_t *ref_buffer;
    uint32_t ref_buffer_size;
    uint8_t *ref_queue;
    uint32_t ref_queue_size;
    uint32_t ref_queue_read;
    uint32_t ref_queue_write;
    uint32_t ref_queue_used;
    uint8_t ref_stream_active;
    uint8_t *out_buffer;
    uint32_t out_buffer_size;
} TDD_AUDIO_ALSA_HANDLE_T;

/***********************************************************
********************function declaration********************
***********************************************************/
static void *__alsa_capture_thread(void *arg);
static OPERATE_RET __alsa_setup_capture(TDD_AUDIO_ALSA_HANDLE_T *hdl);
static OPERATE_RET __alsa_setup_playback(TDD_AUDIO_ALSA_HANDLE_T *hdl);
static OPERATE_RET __alsa_setup_mixer(TDD_AUDIO_ALSA_HANDLE_T *hdl);

// Public API for VAD/KWS callback registration
void tdd_audio_alsa_register_vad_feed_cb(TDD_AUDIO_AFE_FEED_CB cb);
void tdd_audio_alsa_register_kws_feed_cb(TDD_AUDIO_AFE_FEED_CB cb);
void tdd_audio_alsa_unregister_vad_feed_cb(void);
void tdd_audio_alsa_unregister_kws_feed_cb(void);

/***********************************************************
***********************variable define**********************
***********************************************************/
static TDD_AUDIO_AFE_FEED_CB __s_vad_feed_cb = NULL;
static TDD_AUDIO_AFE_FEED_CB __s_kws_feed_cb = NULL;

static void __ref_queue_reset(TDD_AUDIO_ALSA_HANDLE_T *hdl)
{
    hdl->ref_queue_read = 0;
    hdl->ref_queue_write = 0;
    hdl->ref_queue_used = 0;
    hdl->ref_stream_active = 0;
    if (hdl->ref_buffer && hdl->ref_buffer_size > 0) {
        memset(hdl->ref_buffer, 0, hdl->ref_buffer_size);
    }
}

static void __ref_queue_push(TDD_AUDIO_ALSA_HANDLE_T *hdl,
                             const uint8_t *data, uint32_t len)
{
    while (len > 0) {
        uint32_t room;
        uint32_t chunk;

        if (hdl->ref_queue_used == hdl->ref_queue_size) {
            /* Playback must never stall behind diagnostics.  If scheduling
             * falls behind by two full seconds, discard the oldest reference
             * samples and keep the newest audio aligned with the speaker. */
            uint32_t drop = hdl->ref_buffer_size;
            if (drop > hdl->ref_queue_used) {
                drop = hdl->ref_queue_used;
            }
            hdl->ref_queue_read = (hdl->ref_queue_read + drop) %
                                  hdl->ref_queue_size;
            hdl->ref_queue_used -= drop;
        }

        room = hdl->ref_queue_size - hdl->ref_queue_write;
        chunk = len;
        if (chunk > room) {
            chunk = room;
        }
        if (chunk > hdl->ref_queue_size - hdl->ref_queue_used) {
            chunk = hdl->ref_queue_size - hdl->ref_queue_used;
        }
        memcpy(hdl->ref_queue + hdl->ref_queue_write, data, chunk);
        hdl->ref_queue_write = (hdl->ref_queue_write + chunk) %
                               hdl->ref_queue_size;
        hdl->ref_queue_used += chunk;
        data += chunk;
        len -= chunk;
    }
}

static void __ref_queue_push_silence(TDD_AUDIO_ALSA_HANDLE_T *hdl,
                                     uint32_t len)
{
    static const uint8_t silence[256] = {0};

    while (len > 0) {
        uint32_t chunk = len > sizeof(silence) ? sizeof(silence) : len;
        __ref_queue_push(hdl, silence, chunk);
        len -= chunk;
    }
}

static void __ref_queue_pop(TDD_AUDIO_ALSA_HANDLE_T *hdl,
                            uint8_t *data, uint32_t len)
{
    uint32_t copied = 0;

    while (copied < len && hdl->ref_queue_used > 0) {
        uint32_t chunk = len - copied;
        uint32_t contiguous = hdl->ref_queue_size - hdl->ref_queue_read;
        if (chunk > contiguous) {
            chunk = contiguous;
        }
        if (chunk > hdl->ref_queue_used) {
            chunk = hdl->ref_queue_used;
        }
        memcpy(data + copied, hdl->ref_queue + hdl->ref_queue_read, chunk);
        hdl->ref_queue_read = (hdl->ref_queue_read + chunk) %
                              hdl->ref_queue_size;
        hdl->ref_queue_used -= chunk;
        copied += chunk;
    }
    if (copied < len) {
        memset(data + copied, 0, len - copied);
    }
}

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Convert bits per sample to ALSA format
 */
static snd_pcm_format_t __get_alsa_format(TDD_ALSA_DATABITS_E bits)
{
    switch (bits) {
    case TDD_ALSA_DATABITS_8:
        return SND_PCM_FORMAT_S8;
    case TDD_ALSA_DATABITS_16:
        return SND_PCM_FORMAT_S16_LE;
    case TDD_ALSA_DATABITS_24:
        return SND_PCM_FORMAT_S24_LE;
    case TDD_ALSA_DATABITS_32:
        return SND_PCM_FORMAT_S32_LE;
    default:
        return SND_PCM_FORMAT_S16_LE;
    }
}

/**
 * @brief Setup ALSA capture device
 */
static OPERATE_RET __alsa_setup_capture(TDD_AUDIO_ALSA_HANDLE_T *hdl)
{
    int err;
    snd_pcm_hw_params_t *hw_params = NULL;

    // Open PCM device for capture (non-blocking mode to avoid hanging)
    err = snd_pcm_open(&hdl->capture_handle, hdl->cfg.capture_device, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (err < 0) {
        PR_WARN("Audio capture device '%s' not available: %s", hdl->cfg.capture_device, snd_strerror(err));
        PR_WARN("Continuing without audio capture (this is normal on systems without audio hardware)");
        return OPRT_COM_ERROR;
    }

    // Switch back to blocking mode for normal operation
    snd_pcm_nonblock(hdl->capture_handle, 0);

    // Allocate hardware parameters object
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(hdl->capture_handle, hw_params);

    // Set parameters
    snd_pcm_hw_params_set_access(hdl->capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(hdl->capture_handle, hw_params, __get_alsa_format(hdl->cfg.data_bits));
    snd_pcm_hw_params_set_channels(hdl->capture_handle, hw_params, hdl->cfg.channels);
    snd_pcm_hw_params_set_rate_near(hdl->capture_handle, hw_params, (unsigned int *)&hdl->cfg.sample_rate, 0);

    // Set buffer and period sizes
    snd_pcm_uframes_t buffer_size = hdl->cfg.buffer_frames;
    snd_pcm_uframes_t period_size = hdl->cfg.period_frames;
    snd_pcm_hw_params_set_buffer_size_near(hdl->capture_handle, hw_params, &buffer_size);
    snd_pcm_hw_params_set_period_size_near(hdl->capture_handle, hw_params, &period_size, 0);

    // Write parameters to device
    err = snd_pcm_hw_params(hdl->capture_handle, hw_params);
    if (err < 0) {
        PR_ERR("Cannot set capture parameters: %s", snd_strerror(err));
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
        return OPRT_COM_ERROR;
    }

    // Prepare the device
    err = snd_pcm_prepare(hdl->capture_handle);
    if (err < 0) {
        PR_ERR("Cannot prepare capture device: %s", snd_strerror(err));
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
        return OPRT_COM_ERROR;
    }

    // Calculate buffer size
    hdl->capture_buffer_size = hdl->cfg.period_frames * hdl->cfg.channels * (hdl->cfg.data_bits / 8);
    hdl->ref_buffer_size = hdl->capture_buffer_size;
    hdl->out_buffer_size = hdl->capture_buffer_size;
    hdl->ref_buffer = (uint8_t *)tal_malloc(hdl->ref_buffer_size);
    if (NULL == hdl->ref_buffer) {
        PR_ERR("Cannot allocate reference buffer");
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
        return OPRT_MALLOC_FAILED;
    }
    memset(hdl->ref_buffer, 0, hdl->ref_buffer_size);

    hdl->ref_queue_size = hdl->cfg.sample_rate * hdl->cfg.channels *
                          (hdl->cfg.data_bits / 8) * AEC_REF_QUEUE_MS / 1000;
    hdl->ref_queue = (uint8_t *)tal_malloc(hdl->ref_queue_size);
    if (NULL == hdl->ref_queue) {
        PR_ERR("Cannot allocate AEC reference queue");
        tal_free(hdl->ref_buffer);
        hdl->ref_buffer = NULL;
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
        return OPRT_MALLOC_FAILED;
    }
    memset(hdl->ref_queue, 0, hdl->ref_queue_size);
    __ref_queue_reset(hdl);

    // Initialize mutex for ref_buffer protection
    if (pthread_mutex_init(&hdl->ref_buffer_mutex, NULL) != 0) {
        PR_ERR("Cannot initialize ref_buffer mutex");
        tal_free(hdl->ref_buffer);
        hdl->ref_buffer = NULL;
        tal_free(hdl->ref_queue);
        hdl->ref_queue = NULL;
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
        return OPRT_COM_ERROR;
    }
    hdl->out_buffer = (uint8_t *)tal_malloc(hdl->out_buffer_size);
    if (NULL == hdl->out_buffer) {
        PR_ERR("Cannot allocate output buffer");
        pthread_mutex_destroy(&hdl->ref_buffer_mutex);
        tal_free(hdl->ref_buffer);
        hdl->ref_buffer = NULL;
        tal_free(hdl->ref_queue);
        hdl->ref_queue = NULL;
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
        return OPRT_MALLOC_FAILED;
    }
    memset(hdl->out_buffer, 0, hdl->out_buffer_size);

    hdl->capture_buffer = (uint8_t *)tal_malloc(hdl->capture_buffer_size);
    if (NULL == hdl->capture_buffer) {
        PR_ERR("Cannot allocate capture buffer");
        tal_free(hdl->out_buffer);
        hdl->out_buffer = NULL;
        pthread_mutex_destroy(&hdl->ref_buffer_mutex);
        tal_free(hdl->ref_buffer);
        hdl->ref_buffer = NULL;
        tal_free(hdl->ref_queue);
        hdl->ref_queue = NULL;
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
        return OPRT_MALLOC_FAILED;
    }

    PR_INFO("ALSA capture device setup: %s, rate=%d, channels=%d, bits=%d", hdl->cfg.capture_device,
            hdl->cfg.sample_rate, hdl->cfg.channels, hdl->cfg.data_bits);

    return OPRT_OK;
}

/**
 * @brief Setup ALSA playback device
 */
static OPERATE_RET __alsa_setup_playback(TDD_AUDIO_ALSA_HANDLE_T *hdl)
{
    int err;
    snd_pcm_hw_params_t *hw_params = NULL;

    // Open PCM device for playback (non-blocking mode to avoid hanging)
    err = snd_pcm_open(&hdl->playback_handle, hdl->cfg.playback_device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
        PR_WARN("Audio playback device '%s' not available: %s", hdl->cfg.playback_device, snd_strerror(err));
        PR_WARN("Continuing without audio playback (this is normal on systems without audio hardware)");
        return OPRT_COM_ERROR;
    }

    // Switch back to blocking mode for normal operation
    snd_pcm_nonblock(hdl->playback_handle, 0);

    // Allocate hardware parameters object
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(hdl->playback_handle, hw_params);

    // Set parameters
    snd_pcm_hw_params_set_access(hdl->playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(hdl->playback_handle, hw_params, __get_alsa_format(hdl->cfg.data_bits));
    snd_pcm_hw_params_set_channels(hdl->playback_handle, hw_params, hdl->cfg.channels);
    snd_pcm_hw_params_set_rate_near(hdl->playback_handle, hw_params, (unsigned int *)&hdl->cfg.spk_sample_rate, 0);

    // Set buffer and period sizes
    snd_pcm_uframes_t buffer_size = hdl->cfg.buffer_frames;
    snd_pcm_uframes_t period_size = hdl->cfg.period_frames;
    snd_pcm_hw_params_set_buffer_size_near(hdl->playback_handle, hw_params, &buffer_size);
    snd_pcm_hw_params_set_period_size_near(hdl->playback_handle, hw_params, &period_size, 0);

    // Write parameters to device
    err = snd_pcm_hw_params(hdl->playback_handle, hw_params);
    if (err < 0) {
        PR_ERR("Cannot set playback parameters: %s", snd_strerror(err));
        snd_pcm_close(hdl->playback_handle);
        hdl->playback_handle = NULL;
        return OPRT_COM_ERROR;
    }

    // Prepare the device
    err = snd_pcm_prepare(hdl->playback_handle);
    if (err < 0) {
        PR_ERR("Cannot prepare playback device: %s", snd_strerror(err));
        snd_pcm_close(hdl->playback_handle);
        hdl->playback_handle = NULL;
        return OPRT_COM_ERROR;
    }

    PR_INFO("ALSA playback device setup: %s, rate=%d, channels=%d, bits=%d", hdl->cfg.playback_device,
            hdl->cfg.spk_sample_rate, hdl->cfg.channels, hdl->cfg.data_bits);

    return OPRT_OK;
}

/**
 * @brief Setup ALSA mixer for volume control
 */
static OPERATE_RET __alsa_setup_mixer(TDD_AUDIO_ALSA_HANDLE_T *hdl)
{
    int err;
    const char *card = "default";
    const char *selem_name = "Master";

    // Open mixer
    err = snd_mixer_open(&hdl->mixer_handle, 0);
    if (err < 0) {
        PR_WARN("Cannot open mixer: %s", snd_strerror(err));
        hdl->mixer_handle = NULL;
        return OPRT_OK; // Non-critical error
    }

    // Attach mixer to card
    err = snd_mixer_attach(hdl->mixer_handle, card);
    if (err < 0) {
        PR_WARN("Cannot attach mixer to card: %s", snd_strerror(err));
        snd_mixer_close(hdl->mixer_handle);
        hdl->mixer_handle = NULL;
        return OPRT_OK; // Non-critical error
    }

    // Register mixer
    err = snd_mixer_selem_register(hdl->mixer_handle, NULL, NULL);
    if (err < 0) {
        PR_WARN("Cannot register mixer: %s", snd_strerror(err));
        snd_mixer_close(hdl->mixer_handle);
        hdl->mixer_handle = NULL;
        return OPRT_OK; // Non-critical error
    }

    // Load mixer elements
    err = snd_mixer_load(hdl->mixer_handle);
    if (err < 0) {
        PR_WARN("Cannot load mixer: %s", snd_strerror(err));
        snd_mixer_close(hdl->mixer_handle);
        hdl->mixer_handle = NULL;
        return OPRT_OK; // Non-critical error
    }

    // Find mixer element
    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selem_name);
    hdl->mixer_elem = snd_mixer_find_selem(hdl->mixer_handle, sid);

    if (hdl->mixer_elem) {
        snd_mixer_selem_get_playback_volume_range(hdl->mixer_elem, &hdl->mixer_min, &hdl->mixer_max);
        PR_INFO("ALSA mixer setup: %s, range=[%ld, %ld]", selem_name, hdl->mixer_min, hdl->mixer_max);
    } else {
        PR_WARN("Cannot find mixer element: %s", selem_name);
    }

    return OPRT_OK;
}

/**
 * @brief Audio capture thread
 */
static void *__alsa_capture_thread(void *arg)
{
    TDD_AUDIO_ALSA_HANDLE_T *hdl = (TDD_AUDIO_ALSA_HANDLE_T *)arg;
    snd_pcm_sframes_t frames;

    PR_INFO("ALSA capture thread started");

    while (hdl->capture_running) {
        // Read audio frames
        frames = snd_pcm_readi(hdl->capture_handle, hdl->capture_buffer, hdl->cfg.period_frames);

        if (frames < 0) {
            // Handle buffer overrun
            if (frames == -EPIPE) {
                PR_WARN("ALSA capture overrun occurred");
                snd_pcm_prepare(hdl->capture_handle);
                continue;
            } else {
                PR_ERR("ALSA capture error: %s", snd_strerror(frames));
                break;
            }
        }

        // Call callback with captured data
        if (hdl->mic_cb && frames > 0) {
            uint32_t data_size = frames * hdl->cfg.channels * (hdl->cfg.data_bits / 8);
            hdl->mic_cb(TDL_AUDIO_FRAME_FORMAT_PCM, TDL_AUDIO_STATUS_RECEIVING, hdl->capture_buffer, data_size);
        }

        // Feed data to VAD/KWS if registered
        // ref_buffer contains the reference data (speaker playback) for AEC
        // If no playback is active, ref_buffer contains zeros
        if ((__s_vad_feed_cb || __s_kws_feed_cb) && frames > 0) {
            uint32_t data_size = frames * hdl->cfg.channels *
                                 (hdl->cfg.data_bits / 8);
            int16_t *mic_data = (int16_t *)hdl->capture_buffer;
            int16_t *ref_data = (int16_t *)hdl->ref_buffer;
            int16_t *out_data = (int16_t *)hdl->out_buffer;
            
            // Safely read ref_buffer with mutex protection
            // ref_data is always valid (all zeros if no playback)
            pthread_mutex_lock(&hdl->ref_buffer_mutex);
            __ref_queue_pop(hdl, hdl->ref_buffer, data_size);
            
            if (__s_vad_feed_cb) {
                __s_vad_feed_cb(mic_data, ref_data, out_data, (uint32_t)frames);
            }
            if (__s_kws_feed_cb) {
                __s_kws_feed_cb(mic_data, ref_data, out_data, (uint32_t)frames);
            }
            
            pthread_mutex_unlock(&hdl->ref_buffer_mutex);
        }
    }

    PR_INFO("ALSA capture thread stopped");
    return NULL;
}

/**
 * @brief Open audio device
 */
static OPERATE_RET __tdd_audio_alsa_open(TDD_AUDIO_HANDLE_T handle, TDL_AUDIO_MIC_CB mic_cb)
{
    OPERATE_RET rt = OPRT_OK;
    TDD_AUDIO_ALSA_HANDLE_T *hdl = (TDD_AUDIO_ALSA_HANDLE_T *)handle;

    if (NULL == hdl) {
        return OPRT_COM_ERROR;
    }

    hdl->mic_cb = mic_cb;

    // Setup capture device
    rt = __alsa_setup_capture(hdl);
    if (OPRT_OK != rt) {
        PR_ERR("Failed to setup capture device");
        return rt;
    }

    // Setup playback device
    rt = __alsa_setup_playback(hdl);
    if (OPRT_OK != rt) {
        PR_ERR("Failed to setup playback device");
        // Cleanup capture
        if (hdl->capture_handle) {
            snd_pcm_close(hdl->capture_handle);
            hdl->capture_handle = NULL;
        }
        if (hdl->capture_buffer) {
            tal_free(hdl->capture_buffer);
            hdl->capture_buffer = NULL;
        }
        if (hdl->out_buffer) {
            tal_free(hdl->out_buffer);
            hdl->out_buffer = NULL;
        }
        if (hdl->ref_buffer) {
            tal_free(hdl->ref_buffer);
            hdl->ref_buffer = NULL;
        }
        if (hdl->ref_queue) {
            tal_free(hdl->ref_queue);
            hdl->ref_queue = NULL;
        }
        pthread_mutex_destroy(&hdl->ref_buffer_mutex);
        return rt;
    }

    // Setup mixer for volume control
    __alsa_setup_mixer(hdl);

    // Set initial volume
    if (hdl->mixer_elem && hdl->play_volume > 0) {
        long volume = hdl->mixer_min + (hdl->mixer_max - hdl->mixer_min) * hdl->play_volume / 100;
        snd_mixer_selem_set_playback_volume_all(hdl->mixer_elem, volume);
    }

    // Start capture thread
    hdl->capture_running = 1;
    int err = pthread_create(&hdl->capture_thread, NULL, __alsa_capture_thread, hdl);
    if (err != 0) {
        PR_ERR("Failed to create capture thread: %d", err);
        hdl->capture_running = 0;
        // Cleanup
        snd_pcm_close(hdl->capture_handle);
        snd_pcm_close(hdl->playback_handle);
        if (hdl->mixer_handle) {
            snd_mixer_close(hdl->mixer_handle);
        }
        tal_free(hdl->capture_buffer);
        return OPRT_COM_ERROR;
    }

    PR_INFO("ALSA audio device opened successfully");

    return rt;
}

/**
 * @brief Play audio data
 */
static OPERATE_RET __tdd_audio_alsa_play(TDD_AUDIO_HANDLE_T handle, uint8_t *data, uint32_t len)
{
    OPERATE_RET rt = OPRT_OK;
    TDD_AUDIO_ALSA_HANDLE_T *hdl = (TDD_AUDIO_ALSA_HANDLE_T *)handle;

    TUYA_CHECK_NULL_RETURN(hdl, OPRT_COM_ERROR);
    TUYA_CHECK_NULL_RETURN(hdl->playback_handle, OPRT_COM_ERROR);

    if (NULL == data || len == 0) {
        PR_ERR("Play data is NULL or empty");
        return OPRT_COM_ERROR;
    }

    // Calculate number of frames
    uint32_t frame_size = hdl->cfg.channels * (hdl->cfg.data_bits / 8);
    snd_pcm_uframes_t frames = len / frame_size;
    snd_pcm_uframes_t remaining = frames;
    uint8_t *cursor = data;
    uint32_t retry_count = 0;

    /* Queue every sample in playback order.  The old single-period buffer was
     * overwritten by each decoder callback and cleared by an unrelated
     * capture wakeup, so most TTS reached VAD with a zero or mismatched AEC
     * reference.  A bounded FIFO follows the blocking ALSA writer and keeps
     * capture/reference frames continuous. */
    if (hdl->ref_queue && hdl->ref_queue_size > 0) {
        pthread_mutex_lock(&hdl->ref_buffer_mutex);
        if (!hdl->ref_stream_active) {
            uint32_t frame_size_bytes = hdl->cfg.channels *
                                        (hdl->cfg.data_bits / 8);
            uint32_t delay_bytes = hdl->cfg.sample_rate * frame_size_bytes *
                                   AEC_REF_START_DELAY_MS / 1000;
            __ref_queue_push_silence(hdl, delay_bytes);
            hdl->ref_stream_active = 1;
        }
        __ref_queue_push(hdl, data, len);
        pthread_mutex_unlock(&hdl->ref_buffer_mutex);
    }

    /*
     * snd_pcm_writei() is allowed to complete only part of a request, even in
     * blocking mode.  Dropping that unwritten tail removes a slice from every
     * decoded TTS frame and is heard as chopped speech.  Keep writing until the
     * whole PCM block is accepted, recovering an XRUN without discarding data.
     */
    while (remaining > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(hdl->playback_handle,
                                                   cursor,
                                                   remaining);
        if (written == -EINTR) {
            continue;
        }

        if (written == -EAGAIN) {
            if (++retry_count > 100 || snd_pcm_wait(hdl->playback_handle, 20) < 0) {
                PR_ERR("ALSA playback timed out waiting for writable buffer");
                return OPRT_TIMEOUT;
            }
            continue;
        }

        if (written < 0) {
            PR_WARN("ALSA playback interruption: %s", snd_strerror(written));
            if (snd_pcm_recover(hdl->playback_handle, (int)written, 1) < 0) {
                PR_ERR("ALSA playback recovery failed: %s", snd_strerror(written));
                return OPRT_COM_ERROR;
            }
            if (++retry_count > 20) {
                PR_ERR("ALSA playback recovery made no progress");
                return OPRT_TIMEOUT;
            }
            continue;
        }

        if (written == 0) {
            PR_ERR("ALSA playback made no progress");
            return OPRT_COM_ERROR;
        }

        cursor += (size_t)written * frame_size;
        remaining -= (snd_pcm_uframes_t)written;
        retry_count = 0;
    }

    return rt;
}

/**
 * @brief Set playback volume
 */
static OPERATE_RET __tdd_audio_alsa_set_volume(TDD_AUDIO_HANDLE_T handle, uint8_t volume)
{
    OPERATE_RET rt = OPRT_OK;
    TDD_AUDIO_ALSA_HANDLE_T *hdl = (TDD_AUDIO_ALSA_HANDLE_T *)handle;

    TUYA_CHECK_NULL_RETURN(hdl, OPRT_COM_ERROR);

    if (volume > 100) {
        volume = 100;
    }

    hdl->play_volume = volume;

    // Set volume through ALSA mixer if available
    if (hdl->mixer_elem) {
        long alsa_volume = hdl->mixer_min + (hdl->mixer_max - hdl->mixer_min) * volume / 100;
        int err = snd_mixer_selem_set_playback_volume_all(hdl->mixer_elem, alsa_volume);
        if (err < 0) {
            PR_ERR("Failed to set volume: %s", snd_strerror(err));
            return OPRT_COM_ERROR;
        }
        PR_DEBUG("Volume set to %d%% (ALSA: %ld)", volume, alsa_volume);
    } else {
        PR_WARN("Mixer not available, volume setting stored but not applied");
    }

    return rt;
}

/**
 * @brief Audio configuration command handler
 */
static OPERATE_RET __tdd_audio_alsa_config(TDD_AUDIO_HANDLE_T handle, TDD_AUDIO_CMD_E cmd, void *args)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_COM_ERROR);

    switch (cmd) {
    case TDD_AUDIO_CMD_SET_VOLUME: {
        TUYA_CHECK_NULL_GOTO(args, __EXIT);
        uint8_t volume = *(uint8_t *)args;
        TUYA_CALL_ERR_GOTO(__tdd_audio_alsa_set_volume(handle, volume), __EXIT);
    } break;

    case TDD_AUDIO_CMD_PLAY_STOP: {
        TDD_AUDIO_ALSA_HANDLE_T *hdl = (TDD_AUDIO_ALSA_HANDLE_T *)handle;
        if (hdl->playback_handle) {
            snd_pcm_drop(hdl->playback_handle);
            snd_pcm_prepare(hdl->playback_handle);
        }
        // Clear ref_buffer when playback stops
        if (hdl->ref_buffer && hdl->ref_buffer_size > 0) {
            pthread_mutex_lock(&hdl->ref_buffer_mutex);
            __ref_queue_reset(hdl);
            pthread_mutex_unlock(&hdl->ref_buffer_mutex);
        }
    } break;

    default:
        rt = OPRT_INVALID_PARM;
        break;
    }

__EXIT:
    return rt;
}

/**
 * @brief Close audio device
 */
static OPERATE_RET __tdd_audio_alsa_close(TDD_AUDIO_HANDLE_T handle)
{
    OPERATE_RET rt = OPRT_OK;
    TDD_AUDIO_ALSA_HANDLE_T *hdl = (TDD_AUDIO_ALSA_HANDLE_T *)handle;

    TUYA_CHECK_NULL_RETURN(hdl, OPRT_COM_ERROR);

    // Stop capture thread
    if (hdl->capture_running) {
        hdl->capture_running = 0;
        pthread_join(hdl->capture_thread, NULL);
    }

    // Close ALSA handles
    if (hdl->capture_handle) {
        snd_pcm_close(hdl->capture_handle);
        hdl->capture_handle = NULL;
    }

    if (hdl->playback_handle) {
        snd_pcm_close(hdl->playback_handle);
        hdl->playback_handle = NULL;
    }

    if (hdl->mixer_handle) {
        snd_mixer_close(hdl->mixer_handle);
        hdl->mixer_handle = NULL;
    }

    // Free buffers
    if (hdl->capture_buffer) {
        tal_free(hdl->capture_buffer);
        hdl->capture_buffer = NULL;
    }

    if (hdl->ref_buffer) {
        tal_free(hdl->ref_buffer);
        hdl->ref_buffer = NULL;
    }

    if (hdl->ref_queue) {
        tal_free(hdl->ref_queue);
        hdl->ref_queue = NULL;
    }

    if (hdl->out_buffer) {
        tal_free(hdl->out_buffer);
        hdl->out_buffer = NULL;
    }

    // Destroy mutex
    pthread_mutex_destroy(&hdl->ref_buffer_mutex);

    PR_INFO("ALSA audio device closed");

    return rt;
}

/**
 * @brief Register ALSA audio driver
 */
OPERATE_RET tdd_audio_alsa_register(char *name, TDD_AUDIO_ALSA_CFG_T cfg)
{
    OPERATE_RET rt = OPRT_OK;
    TDD_AUDIO_ALSA_HANDLE_T *_hdl = NULL;
    TDD_AUDIO_INTFS_T intfs = {0};
    TDD_AUDIO_INFO_T info = {0};

    // Allocate handle
    _hdl = (TDD_AUDIO_ALSA_HANDLE_T *)tal_malloc(sizeof(TDD_AUDIO_ALSA_HANDLE_T));
    TUYA_CHECK_NULL_RETURN(_hdl, OPRT_MALLOC_FAILED);
    memset(_hdl, 0, sizeof(TDD_AUDIO_ALSA_HANDLE_T));

    // Copy configuration
    memcpy(&_hdl->cfg, &cfg, sizeof(TDD_AUDIO_ALSA_CFG_T));

    // Set default play volume
    _hdl->play_volume = 50;

    info.sample_rate   = cfg.sample_rate;
    info.sample_ch_num = cfg.channels;
    info.sample_bits   = cfg.data_bits;
    info.sample_tm_ms   = AUDIO_PCM_FRAME_MS;

    // Setup interface functions
    intfs.open = __tdd_audio_alsa_open;
    intfs.play = __tdd_audio_alsa_play;
    intfs.config = __tdd_audio_alsa_config;
    intfs.close = __tdd_audio_alsa_close;

    // Register with TDL audio management
    TUYA_CALL_ERR_GOTO(tdl_audio_driver_register(name, (TDD_AUDIO_HANDLE_T)_hdl, &intfs, &info), __ERR);

    PR_INFO("ALSA audio driver registered: %s", name);

    return rt;

__ERR:
    if (_hdl) {
        tal_free(_hdl);
        _hdl = NULL;
    }

    return rt;
}

/**
 * @brief Register VAD audio feed callback
 * 
 * @param[in] cb Callback function to receive audio data for VAD processing
 */
void tdd_audio_alsa_register_vad_feed_cb(TDD_AUDIO_AFE_FEED_CB cb)
{
    __s_vad_feed_cb = cb;
    PR_INFO("VAD feed callback %s", cb ? "registered" : "cleared");
}

/**
 * @brief Register KWS audio feed callback
 * 
 * @param[in] cb Callback function to receive audio data for KWS processing
 */
void tdd_audio_alsa_register_kws_feed_cb(TDD_AUDIO_AFE_FEED_CB cb)
{
    __s_kws_feed_cb = cb;
    PR_INFO("KWS feed callback %s", cb ? "registered" : "cleared");
}

/**
 * @brief Unregister VAD audio feed callback
 */
void tdd_audio_alsa_unregister_vad_feed_cb(void)
{
    __s_vad_feed_cb = NULL;
    PR_INFO("VAD feed callback unregistered");
}

/**
 * @brief Unregister KWS audio feed callback
 */
void tdd_audio_alsa_unregister_kws_feed_cb(void)
{
    __s_kws_feed_cb = NULL;
    PR_INFO("KWS feed callback unregistered");
}

#endif /* ENABLE_AUDIO_ALSA */
