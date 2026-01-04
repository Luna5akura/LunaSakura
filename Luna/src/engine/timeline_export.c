// src/engine/timeline_export.c

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include "engine/timeline.h"
#include "engine/compositor.h"
#include "engine/video.h" // 假设这里有 VideoMeta 定义

// ... (复用 video_export.c 中的 open_encoder 实现) ...
// 请确保 open_encoder 函数可见，或在此处重新复制一遍

static int open_encoder(AVFormatContext* out_fmt_ctx, AVCodecContext** enc_ctx,
                        AVStream** out_stream, int width, int height, double fps) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return -1;
    *out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    *enc_ctx = avcodec_alloc_context3(codec);
    AVRational fps_rat = av_d2q(fps, 100000);
    (*enc_ctx)->width = width;
    (*enc_ctx)->height = height;
    (*enc_ctx)->time_base = av_inv_q(fps_rat);
    (*enc_ctx)->framerate = fps_rat;
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P; // H.264 标准输入
    (*enc_ctx)->gop_size = (int)(fps);
    if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set((*enc_ctx)->priv_data, "preset", "fast", 0);
    av_opt_set((*enc_ctx)->priv_data, "crf", "23", 0);
    if (avcodec_open2(*enc_ctx, codec, NULL) < 0) return -1;
    avcodec_parameters_from_context((*out_stream)->codecpar, *enc_ctx);
    return 0;
}

void export_timeline(Timeline* tl, const char* output_filename) {
    if (!tl) return;
    printf("[Export] GL Rendering to '%s'...\n", output_filename);

    // 1. 创建 Compositor (注意：当前线程必须有 GL Context)
    // 在 main.c 的调用链中，主线程已经有 Context 了。
    Compositor* comp = compositor_create(tl);
    
    // 2. FFmpeg Encoder Setup
    AVFormatContext* out_fmt_ctx = NULL;
    AVCodecContext* enc_ctx = NULL;
    AVStream* out_stream = NULL;
    avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, output_filename);
    if (open_encoder(out_fmt_ctx, &enc_ctx, &out_stream, tl->width, tl->height, tl->fps) < 0) {
        printf("[Export] Encoder init failed.\n");
        goto cleanup;
    }
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) goto cleanup;
    }
    avformat_write_header(out_fmt_ctx, NULL);

    // 3. SWS Context (RGBA -> YUV420P)
    struct SwsContext* sws_ctx = sws_getContext(
        tl->width, tl->height, AV_PIX_FMT_RGBA,
        tl->width, tl->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL
    );
    AVFrame* yuv_frame = av_frame_alloc();
    yuv_frame->format = enc_ctx->pix_fmt;
    yuv_frame->width = enc_ctx->width;
    yuv_frame->height = enc_ctx->height;
    av_frame_get_buffer(yuv_frame, 32);

    // 4. Render Loop
    AVPacket* pkt = av_packet_alloc();
    int total_frames = (int)(tl->duration * tl->fps);
    double step = 1.0 / tl->fps;
    
    for (int i = 0; i < total_frames; i++) {
        double t = i * step;
        
        // A. GPU Render
        compositor_render(comp, t);
        
        // B. Readback (RGBA)
        uint8_t* pixels = compositor_get_cpu_buffer(comp);
        
        // C. Convert to YUV
        const uint8_t* src_slice[] = { pixels };
        int src_stride[] = { tl->width * 4 };
        
        av_frame_make_writable(yuv_frame);
        sws_scale(sws_ctx, src_slice, src_stride, 0, tl->height,
                  yuv_frame->data, yuv_frame->linesize);
                  
        // D. Encode
        yuv_frame->pts = i;
        avcodec_send_frame(enc_ctx, yuv_frame);
        while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
            av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
            pkt->stream_index = out_stream->index;
            av_interleaved_write_frame(out_fmt_ctx, pkt);
            av_packet_unref(pkt);
        }
        
        if (i % 10 == 0) printf("\r[Export] %d / %d", i, total_frames);
    }
    
    // Flush
    avcodec_send_frame(enc_ctx, NULL);
    while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
        av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
        pkt->stream_index = out_stream->index;
        av_interleaved_write_frame(out_fmt_ctx, pkt);
        av_packet_unref(pkt);
    }
    av_write_trailer(out_fmt_ctx);
    printf("\n[Export] Done.\n");

cleanup:
    if (comp) compositor_free(comp); // Release GL resources
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (yuv_frame) av_frame_free(&yuv_frame);
    if (pkt) av_packet_free(&pkt);
    if (enc_ctx) avcodec_free_context(&enc_ctx);
    if (out_fmt_ctx) {
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt_ctx->pb);
        avformat_free_context(out_fmt_ctx);
    }
}