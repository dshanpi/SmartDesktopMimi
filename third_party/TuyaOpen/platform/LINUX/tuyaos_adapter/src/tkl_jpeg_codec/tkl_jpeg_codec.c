#include "jpeg_codec/tkl_jpeg_codec.h"

#include "tuya_error_code.h"

#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf               setjmp_buffer;
} tkl_jpeg_error_mgr_t;

static void __jpeg_error_exit(j_common_ptr cinfo)
{
    tkl_jpeg_error_mgr_t *err = (tkl_jpeg_error_mgr_t *)cinfo->err;
    longjmp(err->setjmp_buffer, 1);
}

OPERATE_RET tkl_jpeg_codec_init(void)
{
    return OPRT_OK;
}

OPERATE_RET tkl_jpeg_codec_deinit(void)
{
    return OPRT_OK;
}

OPERATE_RET tkl_jpeg_codec_img_info_get(UINT8_T *jpeg_buf, UINT32_T jpeg_size, TKL_JPEG_CODEC_INFO_T *jpeg_info)
{
    if (!jpeg_buf || jpeg_size == 0 || !jpeg_info) {
        return OPRT_INVALID_PARM;
    }

    struct jpeg_decompress_struct cinfo;
    tkl_jpeg_error_mgr_t         jerr;

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = __jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_COM_ERROR;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)jpeg_buf, (unsigned long)jpeg_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_COM_ERROR;
    }

    jpeg_info->in_size = jpeg_size;
    jpeg_info->out_width = (UINT16_T)cinfo.image_width;
    jpeg_info->out_height = (UINT16_T)cinfo.image_height;
    jpeg_info->out_fmt = TUYA_FRAME_FMT_JPEG;

    jpeg_destroy_decompress(&cinfo);
    return OPRT_OK;
}

static inline uint16_t __rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

OPERATE_RET tkl_jpeg_codec_convert(UINT8_T *src_buf, UINT8_T *dst_buf, TKL_JPEG_CODEC_INFO_T *jpeg_codec_info,
                                  JPEG_DEC_OUT_FMT out_fmt)
{
    if (!src_buf || !dst_buf || !jpeg_codec_info || jpeg_codec_info->in_size == 0) {
        return OPRT_INVALID_PARM;
    }

    struct jpeg_decompress_struct cinfo;
    tkl_jpeg_error_mgr_t         jerr;

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = __jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_COM_ERROR;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)src_buf, (unsigned long)jpeg_codec_info->in_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_COM_ERROR;
    }

    if (out_fmt == JPEG_DEC_OUT_YUV422) {
        cinfo.out_color_space = JCS_YCbCr;
    } else {
        cinfo.out_color_space = JCS_RGB;
    }

    if (!jpeg_start_decompress(&cinfo)) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_COM_ERROR;
    }

    const uint32_t width = cinfo.output_width;
    const uint32_t height = cinfo.output_height;

    jpeg_codec_info->out_width = (UINT16_T)width;
    jpeg_codec_info->out_height = (UINT16_T)height;

    if (out_fmt == JPEG_DEC_OUT_YUV422 && (width % 2) != 0) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return OPRT_NOT_SUPPORTED;
    }

    const uint32_t components = cinfo.output_components;
    JSAMPARRAY row = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, width * components, 1);

    uint8_t *dst = (uint8_t *)dst_buf;

    for (uint32_t y = 0; y < height; y++) {
        if (jpeg_read_scanlines(&cinfo, row, 1) != 1) {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return OPRT_COM_ERROR;
        }

        const uint8_t *src = (const uint8_t *)row[0];

        if (out_fmt == JPEG_DEC_OUT_YUV422) {
            // Pack as UYVY: [U0][Y0][V0][Y1]
            for (uint32_t x = 0; x < width; x += 2) {
                const uint8_t y0 = src[(x + 0) * 3 + 0];
                const uint8_t u0 = src[(x + 0) * 3 + 1];
                const uint8_t v0 = src[(x + 0) * 3 + 2];

                const uint8_t y1 = src[(x + 1) * 3 + 0];
                const uint8_t u1 = src[(x + 1) * 3 + 1];
                const uint8_t v1 = src[(x + 1) * 3 + 2];

                const uint8_t u = (uint8_t)(((uint16_t)u0 + (uint16_t)u1) >> 1);
                const uint8_t v = (uint8_t)(((uint16_t)v0 + (uint16_t)v1) >> 1);

                *dst++ = u;
                *dst++ = y0;
                *dst++ = v;
                *dst++ = y1;
            }
        } else if (out_fmt == JPEG_DEC_OUT_RGB888) {
            memcpy(dst, src, width * 3);
            dst += width * 3;
        } else if (out_fmt == JPEG_DEC_OUT_RGB565) {
            uint16_t *dst16 = (uint16_t *)dst;
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t r = src[x * 3 + 0];
                const uint8_t g = src[x * 3 + 1];
                const uint8_t b = src[x * 3 + 2];
                dst16[x] = __rgb888_to_rgb565(r, g, b);
            }
            dst += width * 2;
        } else {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return OPRT_NOT_SUPPORTED;
        }
    }

    (void)jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return OPRT_OK;
}
