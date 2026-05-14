// src/engine/media/utils/image_loader.c

#include "engine/media/utils/image_loader.h"

#include "engine/media/utils/ffmpeg_utils.h"

#include <libswscale/swscale.h>
#include <stdlib.h>

bool image_probe_dimensions(const char* filepath, int* out_width, int* out_height) {
    MediaContext media;
    bool success = false;

    if (!out_width || !out_height) return false;
    *out_width = 0;
    *out_height = 0;

    media_ctx_init(&media);
    if (!media_open(&media, filepath, true, false) || !media.vid_ctx) {
        media_close(&media);
        return false;
    }

    if (media.vid_ctx->width > 0 && media.vid_ctx->height > 0) {
        *out_width = media.vid_ctx->width;
        *out_height = media.vid_ctx->height;
        success = true;
    }

    media_close(&media);
    return success;
}

bool image_load_rgba(const char* filepath, uint8_t** out_pixels, int* out_width, int* out_height) {
    MediaContext media;
    AVPacket* packet = NULL;
    AVFrame* frame = NULL;
    AVFrame* rgba_frame = NULL;
    struct SwsContext* sws = NULL;
    uint8_t* pixels = NULL;
    int stride = 0;
    bool success = false;

    if (!out_pixels || !out_width || !out_height) return false;
    *out_pixels = NULL;
    *out_width = 0;
    *out_height = 0;

    media_ctx_init(&media);
    if (!media_open(&media, filepath, true, false) || !media.vid_ctx) {
        media_close(&media);
        return false;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    if (!packet || !frame || !rgba_frame) goto cleanup;

    while (av_read_frame(media.fmt_ctx, packet) >= 0) {
        int ret;

        if (packet->stream_index != media.vid_stream_idx) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(media.vid_ctx, packet);
        av_packet_unref(packet);
        if (ret < 0) continue;

        ret = avcodec_receive_frame(media.vid_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) continue;
        if (ret < 0) goto cleanup;

        *out_width = frame->width;
        *out_height = frame->height;
        stride = frame->width * 4;
        pixels = (uint8_t*)malloc((size_t)stride * (size_t)frame->height);
        if (!pixels) goto cleanup;

        sws = sws_getContext(
            frame->width, frame->height, media.vid_ctx->pix_fmt,
            frame->width, frame->height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, NULL, NULL, NULL
        );
        if (!sws) goto cleanup;

        rgba_frame->format = AV_PIX_FMT_RGBA;
        rgba_frame->width = frame->width;
        rgba_frame->height = frame->height;
        if (av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, pixels,
                                 AV_PIX_FMT_RGBA, frame->width, frame->height, 1) < 0) {
            goto cleanup;
        }

        sws_scale(
            sws,
            (const uint8_t* const*)frame->data,
            frame->linesize,
            0,
            frame->height,
            rgba_frame->data,
            rgba_frame->linesize
        );

        *out_pixels = pixels;
        success = true;
        goto cleanup;
    }

cleanup:
    if (!success && pixels) free(pixels);
    if (sws) sws_freeContext(sws);
    if (rgba_frame) av_frame_free(&rgba_frame);
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    media_close(&media);
    return success;
}
