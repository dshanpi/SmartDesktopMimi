#include "tkl_spi.h"

#include "tuya_error_code.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#if defined(__linux__)
#ifdef __has_include
#if __has_include(<linux/spi/spidev.h>)
#include <linux/spi/spidev.h>
#else
// Fallback minimal definitions
#ifndef __u8
typedef uint8_t __u8;
#endif
#ifndef __u16
typedef uint16_t __u16;
#endif
#ifndef __u32
typedef uint32_t __u32;
#endif
#ifndef __u64
typedef uint64_t __u64;
#endif

#define SPI_CPHA 0x01
#define SPI_CPOL 0x02
#define SPI_MODE_0 (0|0)
#define SPI_MODE_1 (0|SPI_CPHA)
#define SPI_MODE_2 (SPI_CPOL|0)
#define SPI_MODE_3 (SPI_CPOL|SPI_CPHA)
#define SPI_LSB_FIRST 0x08

struct spi_ioc_transfer {
    __u64 tx_buf;
    __u64 rx_buf;

    __u32 len;
    __u32 speed_hz;

    __u16 delay_usecs;
    __u8 bits_per_word;
    __u8 cs_change;

    __u32 pad;
};

#ifndef _IOC_SIZEBITS
#include <asm/ioctl.h>
#endif

#ifndef SPI_IOC_MAGIC
#define SPI_IOC_MAGIC 'k'
#endif
#ifndef SPI_IOC_MESSAGE
#define SPI_IOC_MESSAGE(N) _IOW(SPI_IOC_MAGIC, 0, char[(N)*sizeof(struct spi_ioc_transfer)])
#endif
#ifndef SPI_IOC_WR_MODE
#define SPI_IOC_WR_MODE _IOW(SPI_IOC_MAGIC, 1, __u8)
#endif
#ifndef SPI_IOC_WR_BITS_PER_WORD
#define SPI_IOC_WR_BITS_PER_WORD _IOW(SPI_IOC_MAGIC, 3, __u8)
#endif
#ifndef SPI_IOC_WR_MAX_SPEED_HZ
#define SPI_IOC_WR_MAX_SPEED_HZ _IOW(SPI_IOC_MAGIC, 4, __u32)
#endif
#ifndef SPI_IOC_WR_LSB_FIRST
#define SPI_IOC_WR_LSB_FIRST _IOW(SPI_IOC_MAGIC, 6, __u8)
#endif
#endif
#else
// Compiler does not support __has_include; assume spidev.h exists on linux.
#include <linux/spi/spidev.h>
#endif
#endif

#ifndef TKL_SPI_MAX_PORT
#define TKL_SPI_MAX_PORT 6
#endif

typedef struct {
    int fd;
    bool initialized;
    uint32_t last_count;
    TUYA_SPI_BASE_CFG_T cfg;
} tkl_spi_ctx_t;

static tkl_spi_ctx_t sg_spi_ctx[TKL_SPI_MAX_PORT];
static pthread_mutex_t sg_spi_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *prv_spi_dev_path(TUYA_SPI_NUM_E port, char *buf, size_t buf_len)
{
    // Simple default mapping:
    // 0 -> spidev0.0, 1 -> spidev0.1, 2 -> spidev1.0, 3 -> spidev1.1, 4 -> spidev2.0, 5 -> spidev2.1
    int bus = (int)port / 2;
    int cs = (int)port % 2;
    snprintf(buf, buf_len, "/dev/spidev%d.%d", bus, cs);
    return buf;
}

static uint8_t prv_mode_to_linux(TUYA_SPI_MODE_E mode)
{
    switch (mode) {
    case TUYA_SPI_MODE0:
        return SPI_MODE_0;
    case TUYA_SPI_MODE1:
        return SPI_MODE_1;
    case TUYA_SPI_MODE2:
        return SPI_MODE_2;
    case TUYA_SPI_MODE3:
        return SPI_MODE_3;
    default:
        return SPI_MODE_0;
    }
}

static uint8_t prv_bits_to_linux(TUYA_SPI_DATABITS_E bits)
{
    switch (bits) {
    case TUYA_SPI_DATA_BIT16:
        return 16;
    case TUYA_SPI_DATA_BIT8:
    default:
        return 8;
    }
}

static OPERATE_RET prv_apply_cfg(int fd, const TUYA_SPI_BASE_CFG_T *cfg)
{
    uint8_t mode = prv_mode_to_linux(cfg->mode);
    uint8_t bits = prv_bits_to_linux(cfg->databits);
    uint32_t speed = cfg->freq_hz;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        return OPRT_COM_ERROR;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        return OPRT_COM_ERROR;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        return OPRT_COM_ERROR;
    }

#ifdef SPI_IOC_WR_LSB_FIRST
    uint8_t lsb_first = (cfg->bitorder == TUYA_SPI_ORDER_LSB2MSB) ? 1 : 0;
    (void)ioctl(fd, SPI_IOC_WR_LSB_FIRST, &lsb_first);
#endif

    return OPRT_OK;
}

OPERATE_RET tkl_spi_init(TUYA_SPI_NUM_E port, const TUYA_SPI_BASE_CFG_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    if ((int)port < 0 || port >= TUYA_SPI_NUM_MAX || port >= TKL_SPI_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }

    // Linux adapter only supports master mode via spidev.
    if (cfg->role != TUYA_SPI_ROLE_MASTER && cfg->role != TUYA_SPI_ROLE_MASTER_SIMPLEX) {
        return OPRT_NOT_SUPPORTED;
    }

    pthread_mutex_lock(&sg_spi_lock);

    tkl_spi_ctx_t *ctx = &sg_spi_ctx[port];
    if (ctx->initialized) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_OK;
    }

    char path[32];
    prv_spi_dev_path(port, path, sizeof(path));

    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "tkl_spi: open %s failed: %s\n", path, strerror(errno));
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    OPERATE_RET rt = prv_apply_cfg(fd, cfg);
    if (rt != OPRT_OK) {
        fprintf(stderr, "tkl_spi: apply cfg failed on %s: %s\n", path, strerror(errno));
        close(fd);
        pthread_mutex_unlock(&sg_spi_lock);
        return rt;
    }

    ctx->fd = fd;
    ctx->initialized = true;
    ctx->last_count = 0;
    ctx->cfg = *cfg;

    pthread_mutex_unlock(&sg_spi_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_spi_deinit(TUYA_SPI_NUM_E port)
{
    if ((int)port < 0 || port >= TUYA_SPI_NUM_MAX || port >= TKL_SPI_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_spi_lock);
    tkl_spi_ctx_t *ctx = &sg_spi_ctx[port];
    if (ctx->initialized) {
        close(ctx->fd);
        memset(ctx, 0, sizeof(*ctx));
        ctx->fd = -1;
    }
    pthread_mutex_unlock(&sg_spi_lock);

    return OPRT_OK;
}

OPERATE_RET tkl_spi_send(TUYA_SPI_NUM_E port, void *data, uint32_t size)
{
    if ((int)port < 0 || port >= TUYA_SPI_NUM_MAX || port >= TKL_SPI_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }
    if (data == NULL || size == 0) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_spi_lock);
    tkl_spi_ctx_t *ctx = &sg_spi_ctx[port];
    if (!ctx->initialized) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ssize_t wr = write(ctx->fd, data, size);
    if (wr < 0) {
        ctx->last_count = 0;
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = (uint32_t)wr;
    pthread_mutex_unlock(&sg_spi_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_spi_recv(TUYA_SPI_NUM_E port, void *data, uint32_t size)
{
    if ((int)port < 0 || port >= TUYA_SPI_NUM_MAX || port >= TKL_SPI_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }
    if (data == NULL || size == 0) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_spi_lock);
    tkl_spi_ctx_t *ctx = &sg_spi_ctx[port];
    if (!ctx->initialized) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ssize_t rd = read(ctx->fd, data, size);
    if (rd < 0) {
        ctx->last_count = 0;
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = (uint32_t)rd;
    pthread_mutex_unlock(&sg_spi_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_spi_transfer(TUYA_SPI_NUM_E port, void *send_buf, void *receive_buf, uint32_t length)
{
    if ((int)port < 0 || port >= TUYA_SPI_NUM_MAX || port >= TKL_SPI_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }
    if (send_buf == NULL || receive_buf == NULL || length == 0) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_spi_lock);
    tkl_spi_ctx_t *ctx = &sg_spi_ctx[port];
    if (!ctx->initialized) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = 0;

    struct spi_ioc_transfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    xfer.tx_buf = (uintptr_t)send_buf;
    xfer.rx_buf = (uintptr_t)receive_buf;
    xfer.len = length;
    xfer.speed_hz = ctx->cfg.freq_hz;
    xfer.bits_per_word = prv_bits_to_linux(ctx->cfg.databits);

    int rc = ioctl(ctx->fd, SPI_IOC_MESSAGE(1), &xfer);
    if (rc < 0) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = length;
    pthread_mutex_unlock(&sg_spi_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_spi_transfer_with_length(TUYA_SPI_NUM_E port, void *send_buf, uint32_t send_len, void *receive_buf,
                                         uint32_t receive_len)
{
    if ((int)port < 0 || port >= TUYA_SPI_NUM_MAX || port >= TKL_SPI_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_spi_lock);
    tkl_spi_ctx_t *ctx = &sg_spi_ctx[port];
    if (!ctx->initialized) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = 0;

    if (send_len == 0 && receive_len == 0) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_INVALID_PARM;
    }

    if (receive_len == 0) {
        pthread_mutex_unlock(&sg_spi_lock);
        return tkl_spi_send(port, send_buf, send_len);
    }

    if (send_len == 0) {
        pthread_mutex_unlock(&sg_spi_lock);
        return tkl_spi_recv(port, receive_buf, receive_len);
    }

    struct spi_ioc_transfer xfers[2];
    memset(xfers, 0, sizeof(xfers));

    uint32_t speed = ctx->cfg.freq_hz;
    uint8_t bits = prv_bits_to_linux(ctx->cfg.databits);

    xfers[0].tx_buf = (uintptr_t)send_buf;
    xfers[0].rx_buf = 0;
    xfers[0].len = send_len;
    xfers[0].speed_hz = speed;
    xfers[0].bits_per_word = bits;

    xfers[1].tx_buf = 0;
    xfers[1].rx_buf = (uintptr_t)receive_buf;
    xfers[1].len = receive_len;
    xfers[1].speed_hz = speed;
    xfers[1].bits_per_word = bits;

    int rc = ioctl(ctx->fd, SPI_IOC_MESSAGE(2), xfers);
    if (rc < 0) {
        pthread_mutex_unlock(&sg_spi_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = send_len + receive_len;
    pthread_mutex_unlock(&sg_spi_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_spi_abort_transfer(TUYA_SPI_NUM_E port)
{
    (void)port;
    return OPRT_OK;
}

OPERATE_RET tkl_spi_get_status(TUYA_SPI_NUM_E port, TUYA_SPI_STATUS_T *status)
{
    (void)port;
    if (status == NULL) {
        return OPRT_INVALID_PARM;
    }
    memset(status, 0, sizeof(*status));
    return OPRT_OK;
}

OPERATE_RET tkl_spi_irq_init(TUYA_SPI_NUM_E port, TUYA_SPI_IRQ_CB cb)
{
    (void)port;
    (void)cb;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_spi_irq_enable(TUYA_SPI_NUM_E port)
{
    (void)port;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_spi_irq_disable(TUYA_SPI_NUM_E port)
{
    (void)port;
    return OPRT_NOT_SUPPORTED;
}

int32_t tkl_spi_get_data_count(TUYA_SPI_NUM_E port)
{
    if ((int)port < 0 || port >= TUYA_SPI_NUM_MAX || port >= TKL_SPI_MAX_PORT) {
        return -1;
    }

    pthread_mutex_lock(&sg_spi_lock);
    int32_t cnt = (int32_t)sg_spi_ctx[port].last_count;
    pthread_mutex_unlock(&sg_spi_lock);

    return cnt;
}

OPERATE_RET tkl_spi_ioctl(TUYA_SPI_NUM_E port, uint32_t cmd, void *args)
{
    (void)port;
    (void)cmd;
    (void)args;
    return OPRT_NOT_SUPPORTED;
}

uint32_t tkl_spi_get_max_dma_data_length(void)
{
    // Not meaningful for Linux spidev.
    return 0;
}
