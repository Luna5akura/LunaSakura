// src/engine/compositor.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "engine/compositor.h"
#include "common.h"

// === Helper: Decoder Management ===
static ClipDecoder* create_decoder(ObjClip* clip) {
    ClipDecoder* dec = (ClipDecoder*)calloc(1, sizeof(ClipDecoder));
    dec->clip_ref = clip;
   
    // 1. Open Input
    if (avformat_open_input(&dec->fmt_ctx, clip->path->chars, NULL, NULL) < 0) {
        fprintf(stderr, "[Compositor] Error: Failed to open %s\n", clip->path->chars);
        goto fail;
    }
    if (avformat_find_stream_info(dec->fmt_ctx, NULL) < 0) goto fail;
    // 2. Find Stream
    dec->video_stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (dec->video_stream_idx < 0) goto fail;
   
    AVStream* stream = dec->fmt_ctx->streams[dec->video_stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
   
    // 3. Init Decoder
    dec->dec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec->dec_ctx, stream->codecpar);
   
    if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        dec->dec_ctx->thread_count = 0;
        dec->dec_ctx->thread_type = FF_THREAD_FRAME;
    }
    if (avcodec_open2(dec->dec_ctx, codec, NULL) < 0) goto fail;
    dec->raw_frame = av_frame_alloc();
    dec->current_pts_sec = -1.0;
    return dec;
fail:
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
    if (dec->dec_ctx) avcodec_free_context(&dec->dec_ctx);
    free(dec);
    return NULL;
}

static void free_decoder(ClipDecoder* dec) {
    if (!dec) return;
    if (dec->sws_ctx) sws_freeContext(dec->sws_ctx);
    if (dec->raw_frame) av_frame_free(&dec->raw_frame);
    if (dec->dec_ctx) avcodec_free_context(&dec->dec_ctx);
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
    free(dec);
}

static ClipDecoder* get_decoder(Compositor* comp, ObjClip* clip) {
    for (int i = 0; i < comp->decoder_count; i++) {
        if (comp->decoders[i]->clip_ref == clip) {
            comp->decoders[i]->active_this_frame = true;
            return comp->decoders[i];
        }
    }
    ClipDecoder* dec = create_decoder(clip);
    if (!dec) return NULL;
    if (comp->decoder_count >= comp->decoder_capacity) {
        int new_cap = comp->decoder_capacity == 0 ? 4 : comp->decoder_capacity * 2;
        comp->decoders = (ClipDecoder**)realloc(comp->decoders, new_cap * sizeof(ClipDecoder*));
        comp->decoder_capacity = new_cap;
    }
   
    dec->active_this_frame = true;
    comp->decoders[comp->decoder_count++] = dec;
    return dec;
}

// === Core: Decoding ===
static int decode_frame_at_time(ClipDecoder* dec, double target_time) {
    AVStream* stream = dec->fmt_ctx->streams[dec->video_stream_idx];
    double time_base = av_q2d(stream->time_base);
   
    double diff = target_time - dec->current_pts_sec;
    bool need_seek = (diff < 0) || (diff > 2.0);
    if (need_seek) {
        int64_t target_ts = (int64_t)(target_time / time_base);
        if (av_seek_frame(dec->fmt_ctx, dec->video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD) < 0) {
            return -1;
        }
        avcodec_flush_buffers(dec->dec_ctx);
        dec->current_pts_sec = -1.0;
    }
    AVPacket* pkt = av_packet_alloc();
    bool found_frame = false;
    while (!found_frame && av_read_frame(dec->fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == dec->video_stream_idx) {
            if (avcodec_send_packet(dec->dec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec->dec_ctx, dec->raw_frame) == 0) {
                    double pts = dec->raw_frame->pts * time_base;
                    dec->current_pts_sec = pts;
                    if (pts >= target_time) {
                        found_frame = true;
                        break;
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return found_frame ? 0 : -1;
}

// === Core: Blending (Updated for Transform) ===
static int clamp(int v, int min, int max) {
    return v < min ? min : (v > max ? max : v);
}

static void blend_frame_to_canvas(Compositor* comp, ClipDecoder* dec, TimelineClip* tc) {
    int canvas_w = comp->timeline->width;
    int canvas_h = comp->timeline->height;
    float sx = tc->transform.scale_x;
    float sy = tc->transform.scale_y;
    if (sx <= 0.001f) sx = 1.0f;
    if (sy <= 0.001f) sy = 1.0f;
    int dst_w = (int)(dec->dec_ctx->width * sx);
    int dst_h = (int)(dec->dec_ctx->height * sy);
    int dst_x = (int)tc->transform.x;
    int dst_y = (int)tc->transform.y;
    if (dst_x >= canvas_w || dst_y >= canvas_h || dst_x + dst_w <= 0 || dst_y + dst_h <= 0) return;
    dec->sws_ctx = sws_getCachedContext(
        dec->sws_ctx,
        dec->dec_ctx->width, dec->dec_ctx->height, dec->dec_ctx->pix_fmt,
        dst_w, dst_h, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, NULL, NULL, NULL
    );
    if (!dec->sws_ctx) return;
    int scaled_stride = dst_w * 4;
    uint8_t* scaled_buffer = (uint8_t*)malloc(dst_h * scaled_stride);
    if (!scaled_buffer) return;
    uint8_t* dest_planes[4] = { scaled_buffer, NULL, NULL, NULL };
    int dest_linesizes[4] = { scaled_stride, 0, 0, 0 };
    sws_scale(dec->sws_ctx,
              (const uint8_t* const*)dec->raw_frame->data, dec->raw_frame->linesize,
              0, dec->dec_ctx->height,
              dest_planes, dest_linesizes);
    int draw_start_x = clamp(dst_x, 0, canvas_w);
    int draw_start_y = clamp(dst_y, 0, canvas_h);
    int draw_end_x = clamp(dst_x + dst_w, 0, canvas_w);
    int draw_end_y = clamp(dst_y + dst_h, 0, canvas_h);
    int draw_w = draw_end_x - draw_start_x;
    int draw_h = draw_end_y - draw_start_y;
    if (draw_w > 0 && draw_h > 0) {
        int src_offset_x = draw_start_x - dst_x;
        int src_offset_y = draw_start_y - dst_y;
        for (int row = 0; row < draw_h; row++) {
            int canvas_idx = ((draw_start_y + row) * canvas_w + draw_start_x) * 4;
            int src_idx = ((src_offset_y + row) * dst_w + src_offset_x) * 4;
            memcpy(comp->output_buffer + canvas_idx, scaled_buffer + src_idx, draw_w * 4);
        }
    }
    free(scaled_buffer);
}

// === Lifecycle ===
Compositor* compositor_create(Timeline* timeline) {
    Compositor* comp = (Compositor*)calloc(1, sizeof(Compositor));
    comp->timeline = timeline;
    comp->buffer_size = timeline->width * timeline->height * 4;
    comp->output_buffer = (uint8_t*)calloc(1, comp->buffer_size);
    return comp;
}

void compositor_free(Compositor* comp) {
    if (!comp) return;
    for (int i = 0; i < comp->decoder_count; i++) free_decoder(comp->decoders[i]);
    if (comp->decoders) free(comp->decoders);
    if (comp->output_buffer) free(comp->output_buffer);
    free(comp);
}

uint8_t* compositor_get_buffer(Compositor* comp) {
    return comp->output_buffer;
}

void compositor_render(Compositor* comp, double time) {
    // 1. Clear (Black)
    memset(comp->output_buffer, 0, comp->buffer_size);
   
    // 2. Set Background Color (opaque)
    uint8_t r = comp->timeline->background_color.r;
    uint8_t g = comp->timeline->background_color.g;
    uint8_t b = comp->timeline->background_color.b;
    uint8_t a = 255;
   
    uint32_t bg_pixel = r | (g << 8) | (b << 16) | (a << 24); // ABGR (Little Endian)
    // 简单的全屏填充
    uint32_t* ptr = (uint32_t*)comp->output_buffer;
    for(size_t i=0; i < comp->buffer_size/4; i++) ptr[i] = bg_pixel;
    // 3. Render Tracks
    for (int i = 0; i < comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if ((track->flags & 1) == 0) continue; // Not visible
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        double source_time = tc->source_in + (time - tc->timeline_start);
        ClipDecoder* dec = get_decoder(comp, tc->media);
        if (!dec) continue;
        if (decode_frame_at_time(dec, source_time) == 0) {
            blend_frame_to_canvas(comp, dec, tc);
        }
    }
    comp->frame_counter++;
}