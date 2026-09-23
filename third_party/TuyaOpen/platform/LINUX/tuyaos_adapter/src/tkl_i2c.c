#include "tkl_i2c.h"

#include "tkl_output.h"
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
// Prefer the canonical userspace header if available.
// On some minimal sysroots it may be missing; in that case we fall back to
// local definitions for the small subset we need.
#ifdef __has_include
#if __has_include(<linux/i2c-dev.h>)
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#elif __has_include(<linux/i2c.h>)
#include <linux/i2c.h>
#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif
#ifndef I2C_TENBIT
#define I2C_TENBIT 0x0704
#endif
#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif
#ifndef I2C_SMBUS
#define I2C_SMBUS 0x0720
#endif
#else
#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif
#ifndef I2C_TENBIT
#define I2C_TENBIT 0x0704
#endif
#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif
#ifndef I2C_SMBUS
#define I2C_SMBUS 0x0720
#endif
// Minimal structs for I2C_RDWR and I2C_SMBUS fallback.
#ifndef __u8
typedef uint8_t __u8;
#endif
#ifndef __u16
typedef uint16_t __u16;
#endif
#ifndef __u32
typedef uint32_t __u32;
#endif
struct i2c_msg {
    __u16 addr;
    __u16 flags;
    __u16 len;
    __u8 *buf;
};

struct i2c_rdwr_ioctl_data {
    struct i2c_msg *msgs;
    __u32 nmsgs;
};

#define I2C_M_RD 0x0001

union i2c_smbus_data {
    __u8 byte;
    __u16 word;
    __u8 block[34];
};

struct i2c_smbus_ioctl_data {
    char read_write;
    __u8 command;
    int size;
    union i2c_smbus_data *data;
};

#define I2C_SMBUS_WRITE 0
#define I2C_SMBUS_READ 1
#define I2C_SMBUS_QUICK 0
#endif
#else
#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif
#ifndef I2C_TENBIT
#define I2C_TENBIT 0x0704
#endif
#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif
#ifndef I2C_SMBUS
#define I2C_SMBUS 0x0720
#endif
// Minimal structs for I2C_RDWR and I2C_SMBUS fallback.
#ifndef __u8
typedef uint8_t __u8;
#endif
#ifndef __u16
typedef uint16_t __u16;
#endif
#ifndef __u32
typedef uint32_t __u32;
#endif
struct i2c_msg {
    __u16 addr;
    __u16 flags;
    __u16 len;
    __u8 *buf;
};

struct i2c_rdwr_ioctl_data {
    struct i2c_msg *msgs;
    __u32 nmsgs;
};

#define I2C_M_RD 0x0001

union i2c_smbus_data {
    __u8 byte;
    __u16 word;
    __u8 block[34];
};

struct i2c_smbus_ioctl_data {
    char read_write;
    __u8 command;
    int size;
    union i2c_smbus_data *data;
};

#define I2C_SMBUS_WRITE 0
#define I2C_SMBUS_READ 1
#define I2C_SMBUS_QUICK 0
#endif
#endif

#ifndef TKL_I2C_MAX_PORT
#define TKL_I2C_MAX_PORT 6
#endif

typedef struct {
    int fd;
    bool initialized;

    // For a simple "write then repeated-start read" pattern.
    bool pending_valid;
    uint16_t pending_addr;
    uint8_t pending_buf[32];
    uint32_t pending_len;

    uint32_t last_count;
} tkl_i2c_ctx_t;

static tkl_i2c_ctx_t sg_i2c_ctx[TKL_I2C_MAX_PORT];
static pthread_mutex_t sg_i2c_lock = PTHREAD_MUTEX_INITIALIZER;

static int prv_open_bus(TUYA_I2C_NUM_E port)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", (int)port);

    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "tkl_i2c: open %s failed: %s\n", path, strerror(errno));
        return -1;
    }

    return fd;
}

static OPERATE_RET prv_set_slave(int fd, uint16_t dev_addr, TUYA_IIC_ADDR_MODE_E addr_mode)
{
    if (addr_mode == TUYA_IIC_ADDRESS_10BIT) {
        if (ioctl(fd, I2C_TENBIT, 1) < 0) {
            fprintf(stderr, "tkl_i2c: I2C_TENBIT set failed: %s\n", strerror(errno));
            return OPRT_COM_ERROR;
        }
    } else {
        (void)ioctl(fd, I2C_TENBIT, 0);
    }

    if (ioctl(fd, I2C_SLAVE, (int)dev_addr) < 0) {
        // Typical: ENXIO when no device responds at this address.
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

static OPERATE_RET prv_probe_addr(int fd)
{
    // SMBus "quick" sends only address + R/W + (no data).
    struct i2c_smbus_ioctl_data args;
    memset(&args, 0, sizeof(args));
    args.read_write = I2C_SMBUS_WRITE;
    args.command = 0;
    args.size = I2C_SMBUS_QUICK;
    args.data = NULL;

    if (ioctl(fd, I2C_SMBUS, &args) < 0) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_init(TUYA_I2C_NUM_E port, const TUYA_IIC_BASE_CFG_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    if ((int)port < 0 || port >= TUYA_I2C_NUM_MAX || port >= TKL_I2C_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_i2c_lock);

    tkl_i2c_ctx_t *ctx = &sg_i2c_ctx[port];
    if (ctx->initialized) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_OK;
    }

    int fd = prv_open_bus(port);
    if (fd < 0) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_COM_ERROR;
    }

    ctx->fd = fd;
    ctx->initialized = true;
    ctx->pending_valid = false;
    ctx->pending_len = 0;
    ctx->pending_addr = 0;
    ctx->last_count = 0;

    pthread_mutex_unlock(&sg_i2c_lock);

    return OPRT_OK;
}

OPERATE_RET tkl_i2c_deinit(TUYA_I2C_NUM_E port)
{
    if ((int)port < 0 || port >= TUYA_I2C_NUM_MAX || port >= TKL_I2C_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_i2c_lock);
    tkl_i2c_ctx_t *ctx = &sg_i2c_ctx[port];

    if (ctx->initialized) {
        close(ctx->fd);
        memset(ctx, 0, sizeof(*ctx));
        ctx->fd = -1;
    }

    pthread_mutex_unlock(&sg_i2c_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_i2c_master_send(TUYA_I2C_NUM_E port, uint16_t dev_addr, const void *data, uint32_t size,
                                BOOL_T xfer_pending)
{
    if ((int)port < 0 || port >= TUYA_I2C_NUM_MAX || port >= TKL_I2C_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_i2c_lock);
    tkl_i2c_ctx_t *ctx = &sg_i2c_ctx[port];
    if (!ctx->initialized) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = 0;

    // Special-case: "size == 0" is commonly used as an address probe.
    if (size == 0) {
        OPERATE_RET ret = prv_set_slave(ctx->fd, dev_addr, TUYA_IIC_ADDRESS_7BIT);
        if (ret != OPRT_OK) {
            pthread_mutex_unlock(&sg_i2c_lock);
            return ret;
        }

        ret = prv_probe_addr(ctx->fd);
        pthread_mutex_unlock(&sg_i2c_lock);
        return ret;
    }

    if (data == NULL) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_INVALID_PARM;
    }

    // If caller requests pending transfer (no STOP), we buffer it and defer to the
    // subsequent receive() to perform a combined I2C_RDWR transaction.
    if (xfer_pending) {
        uint32_t copy_len = size;
        if (copy_len > sizeof(ctx->pending_buf)) {
            copy_len = sizeof(ctx->pending_buf);
        }
        memcpy(ctx->pending_buf, data, copy_len);
        ctx->pending_len = copy_len;
        ctx->pending_addr = dev_addr;
        ctx->pending_valid = true;
        ctx->last_count = copy_len;
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_OK;
    }

    OPERATE_RET ret = prv_set_slave(ctx->fd, dev_addr, TUYA_IIC_ADDRESS_7BIT);
    if (ret != OPRT_OK) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return ret;
    }

    ssize_t wr = write(ctx->fd, data, size);
    if (wr < 0) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = (uint32_t)wr;
    pthread_mutex_unlock(&sg_i2c_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_i2c_master_receive(TUYA_I2C_NUM_E port, uint16_t dev_addr, void *data, uint32_t size,
                                   BOOL_T xfer_pending)
{
    (void)xfer_pending;

    if ((int)port < 0 || port >= TUYA_I2C_NUM_MAX || port >= TKL_I2C_MAX_PORT) {
        return OPRT_INVALID_PARM;
    }

    if (data == NULL || size == 0) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&sg_i2c_lock);
    tkl_i2c_ctx_t *ctx = &sg_i2c_ctx[port];
    if (!ctx->initialized) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = 0;

    // If we have a pending write, perform a combined transaction: write + read.
    if (ctx->pending_valid && ctx->pending_addr == dev_addr) {
        struct i2c_msg msgs[2];
        memset(msgs, 0, sizeof(msgs));

        msgs[0].addr = dev_addr;
        msgs[0].flags = 0;
        msgs[0].len = (__u16)ctx->pending_len;
        msgs[0].buf = (__u8 *)ctx->pending_buf;

        msgs[1].addr = dev_addr;
        msgs[1].flags = I2C_M_RD;
        msgs[1].len = (__u16)size;
        msgs[1].buf = (__u8 *)data;

        struct i2c_rdwr_ioctl_data xfer;
        xfer.msgs = msgs;
        xfer.nmsgs = 2;

        int rc = ioctl(ctx->fd, I2C_RDWR, &xfer);
        ctx->pending_valid = false;
        ctx->pending_len = 0;

        if (rc < 0) {
            pthread_mutex_unlock(&sg_i2c_lock);
            return OPRT_COM_ERROR;
        }

        ctx->last_count = size;
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_OK;
    }

    // Plain read.
    OPERATE_RET ret = prv_set_slave(ctx->fd, dev_addr, TUYA_IIC_ADDRESS_7BIT);
    if (ret != OPRT_OK) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return ret;
    }

    ssize_t rd = read(ctx->fd, data, size);
    if (rd < 0) {
        pthread_mutex_unlock(&sg_i2c_lock);
        return OPRT_COM_ERROR;
    }

    ctx->last_count = (uint32_t)rd;
    pthread_mutex_unlock(&sg_i2c_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_i2c_irq_init(TUYA_I2C_NUM_E port, TUYA_I2C_IRQ_CB cb)
{
    (void)port;
    (void)cb;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_irq_enable(TUYA_I2C_NUM_E port)
{
    (void)port;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_irq_disable(TUYA_I2C_NUM_E port)
{
    (void)port;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_set_slave_addr(TUYA_I2C_NUM_E port, uint16_t dev_addr)
{
    (void)port;
    (void)dev_addr;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_slave_send(TUYA_I2C_NUM_E port, const void *data, uint32_t size)
{
    (void)port;
    (void)data;
    (void)size;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_slave_receive(TUYA_I2C_NUM_E port, void *data, uint32_t size)
{
    (void)port;
    (void)data;
    (void)size;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_get_status(TUYA_I2C_NUM_E port, TUYA_IIC_STATUS_T *status)
{
    (void)port;
    if (status == NULL) {
        return OPRT_INVALID_PARM;
    }
    memset(status, 0, sizeof(*status));
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_i2c_reset(TUYA_I2C_NUM_E port)
{
    (void)port;
    return OPRT_OK;
}

int32_t tkl_i2c_get_data_count(TUYA_I2C_NUM_E port)
{
    if ((int)port < 0 || port >= TUYA_I2C_NUM_MAX || port >= TKL_I2C_MAX_PORT) {
        return -1;
    }

    pthread_mutex_lock(&sg_i2c_lock);
    int32_t cnt = (int32_t)sg_i2c_ctx[port].last_count;
    pthread_mutex_unlock(&sg_i2c_lock);

    return cnt;
}

OPERATE_RET tkl_i2c_ioctl(TUYA_I2C_NUM_E port, uint32_t cmd, void *args)
{
    (void)port;
    (void)cmd;
    (void)args;
    return OPRT_NOT_SUPPORTED;
}
