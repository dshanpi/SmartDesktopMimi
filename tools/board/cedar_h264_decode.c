/*
 * Minimal CedarX Annex-B H.264 decoder diagnostic.
 *
 * This intentionally bypasses CedarX's container parser so short raw .h264
 * files can be decoded into a tightly packed NV21 frame.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <memoryAdapter.h>
#include <vdecoder.h>

static int load_file(const char *path, unsigned char **data, int *length)
{
    FILE *file;
    long size;

    file = fopen(path, "rb");
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0 ||
        size > 16 * 1024 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        errno = EINVAL;
        return -1;
    }
    *data = malloc((size_t)size);
    if (!*data) {
        fclose(file);
        return -1;
    }
    if (fread(*data, 1, (size_t)size, file) != (size_t)size) {
        free(*data);
        *data = NULL;
        fclose(file);
        errno = EIO;
        return -1;
    }
    fclose(file);
    *length = (int)size;
    return 0;
}

static int save_nv21(const char *path, const VideoPicture *picture)
{
    FILE *file;
    int width;
    int height;
    int row;

    width = picture->nRightOffset - picture->nLeftOffset;
    height = picture->nBottomOffset - picture->nTopOffset;
    if (width <= 0 || height <= 0) {
        width = picture->nWidth;
        height = picture->nHeight;
    }
    file = fopen(path, "wb");
    if (!file)
        return -1;
    for (row = 0; row < height; ++row) {
        const char *source = picture->pData0 +
            (picture->nTopOffset + row) * picture->nLineStride +
            picture->nLeftOffset;
        if (fwrite(source, 1, (size_t)width, file) != (size_t)width)
            goto error;
    }
    for (row = 0; row < height / 2; ++row) {
        const char *source = picture->pData1 +
            (picture->nTopOffset / 2 + row) * picture->nLineStride +
            picture->nLeftOffset;
        if (fwrite(source, 1, (size_t)width, file) != (size_t)width)
            goto error;
    }
    if (fclose(file) != 0)
        return -1;
    printf("saved %s: %dx%d NV21, stride=%d, format=%d\n",
           path, width, height, picture->nLineStride,
           picture->ePixelFormat);
    return 0;

error:
    fclose(file);
    errno = EIO;
    return -1;
}

int main(int argc, char **argv)
{
    unsigned char *input = NULL;
    char *buffer = NULL;
    char *ring_buffer = NULL;
    int input_length = 0;
    int buffer_length = 0;
    int ring_length = 0;
    int result = 1;
    int attempt;
    struct ScMemOpsS *memops = NULL;
    VideoDecoder *decoder = NULL;
    VideoPicture *picture = NULL;
    VideoStreamInfo stream;
    VideoStreamDataInfo data;
    VConfig config;

    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT.h264 OUTPUT.nv21\n", argv[0]);
        return 2;
    }
    if (load_file(argv[1], &input, &input_length) != 0) {
        perror("load input");
        goto done;
    }
    memops = MemAdapterGetOpsS();
    if (!memops || CdcMemOpen(memops) != 0) {
        fprintf(stderr, "CdcMemOpen failed\n");
        goto done;
    }
    AddVDPlugin();
    decoder = CreateVideoDecoder();
    if (!decoder) {
        fprintf(stderr, "CreateVideoDecoder failed\n");
        goto done;
    }

    memset(&stream, 0, sizeof(stream));
    stream.eCodecFormat = VIDEO_CODEC_FORMAT_H264;
    stream.nWidth = 1920;
    stream.nHeight = 1080;
    stream.nFrameRate = 60000;
    stream.nFrameDuration = 16667;
    stream.bIsFramePackage = 0;

    memset(&config, 0, sizeof(config));
    config.eOutputPixelFormat = PIXEL_FORMAT_NV21;
    config.nDeInterlaceHoldingFrameBufferNum = 0;
    config.nDisplayHoldingFrameBufferNum = 1;
    config.nRotateHoldingFrameBufferNum = 0;
    config.nDecodeSmoothFrameBufferNum = 0;
    config.memops = memops;
    if (InitializeVideoDecoder(decoder, &stream, &config) != 0) {
        fprintf(stderr, "InitializeVideoDecoder failed\n");
        goto done;
    }
    if (RequestVideoStreamBuffer(decoder, input_length, &buffer,
                                 &buffer_length, &ring_buffer,
                                 &ring_length, 0) != 0 ||
        buffer_length + ring_length < input_length) {
        fprintf(stderr, "RequestVideoStreamBuffer failed\n");
        goto done;
    }
    if (buffer_length > input_length)
        buffer_length = input_length;
    memcpy(buffer, input, (size_t)buffer_length);
    if (input_length > buffer_length)
        memcpy(ring_buffer, input + buffer_length,
               (size_t)(input_length - buffer_length));

    memset(&data, 0, sizeof(data));
    data.pData = buffer;
    data.nLength = input_length;
    data.bIsFirstPart = 1;
    data.bIsLastPart = 1;
    data.bValid = 1;
    if (SubmitVideoStreamData(decoder, &data, 0) != 0) {
        fprintf(stderr, "SubmitVideoStreamData failed\n");
        goto done;
    }

    for (attempt = 0; attempt < 2000; ++attempt) {
        int decode_result = DecodeVideoStream(decoder, 1, 0, 0, 0);
        if (decode_result == VDECODE_RESULT_KEYFRAME_DECODED ||
            decode_result == VDECODE_RESULT_FRAME_DECODED) {
            picture = RequestPicture(decoder, 0);
            if (picture) {
                printf("picture buffer=%dx%d visible=(%d,%d)-(%d,%d)\n",
                       picture->nWidth, picture->nHeight,
                       picture->nLeftOffset, picture->nTopOffset,
                       picture->nRightOffset, picture->nBottomOffset);
                if (save_nv21(argv[2], picture) == 0)
                    result = 0;
                else
                    perror("save output");
                ReturnPicture(decoder, picture);
                picture = NULL;
                break;
            }
        } else if (decode_result < 0) {
            fprintf(stderr, "DecodeVideoStream failed: %d\n",
                    decode_result);
            break;
        }
        usleep(1000);
    }
    if (result != 0 && attempt == 2000)
        fprintf(stderr, "decoder timed out\n");

done:
    if (picture && decoder)
        ReturnPicture(decoder, picture);
    if (decoder)
        DestroyVideoDecoder(decoder);
    else if (memops)
        CdcMemClose(memops);
    free(input);
    return result;
}
