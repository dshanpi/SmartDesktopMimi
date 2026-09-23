#include "camera/tkl_camera_v4l2.h"
#include "tuya_error_code.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

typedef struct {
    void *start;
    size_t length;
} v4l2_buf_t;

typedef struct {
    int fd;
    bool streaming;

    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t buffer_count;
    uint32_t fourcc;

    v4l2_buf_t *buffers;
    uint32_t buffers_num;
} tkl_v4l2_ctx_t;

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static OPERATE_RET v4l2_set_format_and_fps(tkl_v4l2_ctx_t *ctx)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = ctx->width;
    fmt.fmt.pix.height = ctx->height;
    fmt.fmt.pix.pixelformat = ctx->fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == -1) {
        return OPRT_COM_ERROR;
    }

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = (ctx->fps == 0) ? 30 : ctx->fps;
    (void)xioctl(ctx->fd, VIDIOC_S_PARM, &parm);

    ctx->width = fmt.fmt.pix.width;
    ctx->height = fmt.fmt.pix.height;

    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_open(TKL_CAMERA_V4L2_HANDLE_T *hdl, const TKL_CAMERA_V4L2_CFG_T *cfg)
{
    if (hdl == NULL || cfg == NULL || cfg->devnode == NULL) {
        return OPRT_INVALID_PARM;
    }

    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)calloc(1, sizeof(tkl_v4l2_ctx_t));
    if (!ctx) {
        return OPRT_MALLOC_FAILED;
    }

    ctx->fd = -1;
    ctx->width = cfg->width;
    ctx->height = cfg->height;
    ctx->fps = cfg->fps;
    ctx->buffer_count = (cfg->buffer_count == 0) ? 4 : cfg->buffer_count;

    switch (cfg->pixfmt) {
    case TKL_CAMERA_V4L2_PIXFMT_YUYV:
        ctx->fourcc = V4L2_PIX_FMT_YUYV;
        break;
    case TKL_CAMERA_V4L2_PIXFMT_MJPEG:
        ctx->fourcc = V4L2_PIX_FMT_MJPEG;
        break;
    default:
        free(ctx);
        return OPRT_INVALID_PARM;
    }

    ctx->fd = open(cfg->devnode, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (ctx->fd < 0) {
        free(ctx);
        return OPRT_COM_ERROR;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) == -1) {
        close(ctx->fd);
        free(ctx);
        return OPRT_COM_ERROR;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        close(ctx->fd);
        free(ctx);
        return OPRT_NOT_SUPPORTED;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        close(ctx->fd);
        free(ctx);
        return OPRT_NOT_SUPPORTED;
    }

    OPERATE_RET rt = v4l2_set_format_and_fps(ctx);
    if (rt != OPRT_OK) {
        close(ctx->fd);
        free(ctx);
        return rt;
    }

    *hdl = (TKL_CAMERA_V4L2_HANDLE_T)ctx;
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_start(TKL_CAMERA_V4L2_HANDLE_T hdl)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx || ctx->fd < 0) {
        return OPRT_INVALID_PARM;
    }
    if (ctx->streaming) {
        return OPRT_OK;
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = ctx->buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->fd, VIDIOC_REQBUFS, &req) == -1) {
        return OPRT_COM_ERROR;
    }

    if (req.count < 2) {
        return OPRT_COM_ERROR;
    }

    ctx->buffers = (v4l2_buf_t *)calloc(req.count, sizeof(v4l2_buf_t));
    if (!ctx->buffers) {
        return OPRT_MALLOC_FAILED;
    }
    ctx->buffers_num = req.count;

    for (uint32_t i = 0; i < ctx->buffers_num; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            (void)tkl_camera_v4l2_stop(hdl);
            return OPRT_COM_ERROR;
        }

        ctx->buffers[i].length = buf.length;
        ctx->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buf.m.offset);
        if (ctx->buffers[i].start == MAP_FAILED) {
            ctx->buffers[i].start = NULL;
            (void)tkl_camera_v4l2_stop(hdl);
            return OPRT_COM_ERROR;
        }

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            (void)tkl_camera_v4l2_stop(hdl);
            return OPRT_COM_ERROR;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(ctx->fd, VIDIOC_STREAMON, &type) == -1) {
        (void)tkl_camera_v4l2_stop(hdl);
        return OPRT_COM_ERROR;
    }

    ctx->streaming = true;
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_stop(TKL_CAMERA_V4L2_HANDLE_T hdl)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx || ctx->fd < 0) {
        return OPRT_INVALID_PARM;
    }

    if (ctx->streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)xioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
        ctx->streaming = false;
    }

    if (ctx->buffers) {
        for (uint32_t i = 0; i < ctx->buffers_num; i++) {
            if (ctx->buffers[i].start && ctx->buffers[i].length) {
                (void)munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            }
        }
        free(ctx->buffers);
        ctx->buffers = NULL;
        ctx->buffers_num = 0;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_close(TKL_CAMERA_V4L2_HANDLE_T hdl)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }

    (void)tkl_camera_v4l2_stop(hdl);

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    free(ctx);
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_dequeue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint8_t **data, uint32_t *len, uint32_t *index)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx || !data || !len || !index) {
        return OPRT_INVALID_PARM;
    }
    if (!ctx->streaming) {
        return OPRT_INVALID_PARM;
    }

    for (;;) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(ctx->fd, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(ctx->fd + 1, &fds, NULL, NULL, &tv);
        if (r == -1) {
            if (errno == EINTR) {
                continue;
            }
            return OPRT_COM_ERROR;
        }
        if (r == 0) {
            return OPRT_TIMEOUT;
        }

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(ctx->fd, VIDIOC_DQBUF, &buf) == -1) {
            if (errno == EAGAIN) {
                continue;
            }
            return OPRT_COM_ERROR;
        }

        if (buf.index >= ctx->buffers_num) {
            return OPRT_COM_ERROR;
        }

        *data = (uint8_t *)ctx->buffers[buf.index].start;
        *len = buf.bytesused;
        *index = buf.index;
        return OPRT_OK;
    }
}

OPERATE_RET tkl_camera_v4l2_queue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint32_t index)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }
    if (index >= ctx->buffers_num) {
        return OPRT_INVALID_PARM;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;

    if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}
