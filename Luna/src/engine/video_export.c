// src/engine/video_export.c

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h> // For av_d2q
#include "video.h"
#include "vm/memory.h"
#include "vm/vm.h"  // Added for VM struct

// --- Helper: Initialize H.264 Encoder ---
static int open_encoder(VM* vm, AVFormatContext* out_fmt_ctx, AVCodecContext** enc_ctx,
                        AVStream** out_stream, int width, int height, double fps) {
   
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "[Export] Error: H.264 encoder not found.\n");
        return -1;
    }
    *out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!*out_stream) return -1;
    *enc_ctx = avcodec_alloc_context3(codec);
    vm->bytesAllocated += sizeof(AVCodecContext);
    if (!*enc_ctx) return -1;
    // Optimization: Convert double FPS to rational number to handle NTSC (29.97) correctly
    AVRational fps_rat = av_d2q(fps, 100000); // Max precision 1/100000
    (*enc_ctx)->width = width;
    (*enc_ctx)->height = height;
    (*enc_ctx)->time_base = av_inv_q(fps_rat); // Timebase is 1/FPS
    (*enc_ctx)->framerate = fps_rat;
   
    // Note: For production, libswscale is needed to ensure input frame format matches this.
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;
   
    // GOP Setup: 1 keyframe per second for reasonable seeking/scrubbing
    (*enc_ctx)->gop_size = (int)(fps + 0.5);
    (*enc_ctx)->max_b_frames = 2;
    if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // Quality/Speed trade-off
    av_opt_set((*enc_ctx)->priv_data, "preset", "medium", 0);
    av_opt_set((*enc_ctx)->priv_data, "crf", "23", 0);
    if (avcodec_open2(*enc_ctx, codec, NULL) < 0) {
        fprintf(stderr, "[Export] Error: Could not open encoder.\n");
        return -1;
    }
    avcodec_parameters_from_context((*out_stream)->codecpar, *enc_ctx);
    return 0;
}

// --- Export Logic ---
void export_video_clip(VM* vm, ObjClip* clip, const char* output_filename) {
    // Resources
    AVFormatContext* in_fmt_ctx = NULL;
    AVCodecContext* dec_ctx = NULL;
    AVFormatContext* out_fmt_ctx = NULL;
    AVCodecContext* enc_ctx = NULL;
    AVPacket* pkt = NULL;
    AVPacket* out_pkt = NULL;
    AVFrame* frame = NULL;
    int ret = 0;
    fprintf(stderr, "[Export] Processing '%s' -> '%s'\n", clip->path->chars, output_filename);
    // --- Input Setup ---
    if (avformat_open_input(&in_fmt_ctx, clip->path->chars, NULL, NULL) < 0) {
        fprintf(stderr, "[Export] Error: Could not open input.\n");
        goto cleanup;
    }
    if (avformat_find_stream_info(in_fmt_ctx, NULL) < 0) goto cleanup;
    int video_stream_idx = av_find_best_stream(in_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0) goto cleanup;
    AVStream* in_stream = in_fmt_ctx->streams[video_stream_idx];
    const AVCodec* dec = avcodec_find_decoder(in_stream->codecpar->codec_id);
    dec_ctx = avcodec_alloc_context3(dec);
    vm->bytesAllocated += sizeof(AVCodecContext);
    avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
    if (avcodec_open2(dec_ctx, dec, NULL) < 0) goto cleanup;
    // --- Output Setup ---
    avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, output_filename);
    if (!out_fmt_ctx) goto cleanup;
    AVStream* out_stream = NULL;
    if (open_encoder(vm, out_fmt_ctx, &enc_ctx, &out_stream,
                     clip->width, clip->height, clip->fps) < 0) {
        goto cleanup;
    }
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) goto cleanup;
    }
    if (avformat_write_header(out_fmt_ctx, NULL) < 0) goto cleanup;
    // --- Seeking ---
    // Calculate target timestamp in stream's timebase
    int64_t seek_target_us = (int64_t)(clip->in_point * AV_TIME_BASE);
    int64_t seek_target_ts = av_rescale_q(seek_target_us, AV_TIME_BASE_Q, in_stream->time_base);
   
    if (clip->in_point > 0) {
        // Seek to the nearest keyframe BEFORE the target
        av_seek_frame(in_fmt_ctx, video_stream_idx, seek_target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_ctx);
    }
    // --- Allocation ---
    pkt = av_packet_alloc();
    vm->bytesAllocated += sizeof(AVPacket);
    out_pkt = av_packet_alloc();
    vm->bytesAllocated += sizeof(AVPacket);
    frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    if (!pkt || !out_pkt || !frame) goto cleanup;
    int64_t encoded_frame_count = 0;
    int64_t total_frames = (int64_t)(clip->duration * clip->fps);
    bool encode_finished = false;
    // --- Main Transcode Loop ---
    while (av_read_frame(in_fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
           
            ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret < 0) {
                av_packet_unref(pkt);
                continue;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) goto cleanup;
                // Accuracy Logic: Drop frames decoded before the precise trim start point
                // Note: frame->pts might be unreliable in some containers, best_effort_timestamp is safer
                if (frame->best_effort_timestamp < seek_target_ts) {
                    av_frame_unref(frame);
                    continue;
                }
                // Trim Duration Check
                if (encoded_frame_count >= total_frames) {
                    encode_finished = true;
                    av_frame_unref(frame);
                    break;
                }
                // Prepare Frame for Encoder
                frame->pict_type = AV_PICTURE_TYPE_NONE;
                frame->pts = encoded_frame_count; // Force CFR (Constant Frame Rate)
                if (avcodec_send_frame(enc_ctx, frame) < 0) goto cleanup;
                while (1) {
                    ret = avcodec_receive_packet(enc_ctx, out_pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) goto cleanup;
                    av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_stream->time_base);
                    out_pkt->stream_index = out_stream->index;
                    av_interleaved_write_frame(out_fmt_ctx, out_pkt);
                    av_packet_unref(out_pkt);
                }
                encoded_frame_count++;
                if (encoded_frame_count % 30 == 0) {
                    printf("\r[Export] Progress: %.0f%%",
                           (double)encoded_frame_count / total_frames * 100.0);
                    fflush(stdout);
                }
            }
        }
        av_packet_unref(pkt);
        if (encode_finished) break;
    }
    // --- Flush Encoder ---
    avcodec_send_frame(enc_ctx, NULL);
    while (1) {
        ret = avcodec_receive_packet(enc_ctx, out_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
       
        av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_stream->time_base);
        out_pkt->stream_index = out_stream->index;
        av_interleaved_write_frame(out_fmt_ctx, out_pkt);
        av_packet_unref(out_pkt);
    }
    av_write_trailer(out_fmt_ctx);
    printf("\n[Export] Done.\n");
cleanup:
    if (dec_ctx) {
        avcodec_free_context(&dec_ctx);
        vm->bytesAllocated -= sizeof(AVCodecContext);
    }
    if (enc_ctx) {
        avcodec_free_context(&enc_ctx);
        vm->bytesAllocated -= sizeof(AVCodecContext);
    }
    if (frame) {
        av_frame_free(&frame);
        vm->bytesAllocated -= sizeof(AVFrame);
    }
    if (pkt) {
        av_packet_free(&pkt);
        vm->bytesAllocated -= sizeof(AVPacket);
    }
    if (out_pkt) {
        av_packet_free(&out_pkt);
        vm->bytesAllocated -= sizeof(AVPacket);
    }
    if (in_fmt_ctx) avformat_close_input(&in_fmt_ctx);
    if (out_fmt_ctx) {
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
    }
}