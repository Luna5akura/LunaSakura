// src/engine/service/transcoder.c

#include "engine/service/transcoder.h"
#include "engine/media/utils/ffmpeg_utils.h" // [新增]
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "core/memory.h"
#include "core/vm/vm.h"

// --- Helper: Initialize H.264 Encoder ---
static i32 open_encoder_internal(VM* vm, AVFormatContext* out_fmt_ctx, AVCodecContext** enc_ctx,
                        AVStream** out_stream, i32 width, i32 height, double fps) {
 
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "[Transcoder] Error: H.264 encoder not found.\n");
        return -1;
    }
    *out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!*out_stream) return -1;
    
    *enc_ctx = avcodec_alloc_context3(codec);
    vm->bytesAllocated += sizeof(AVCodecContext);
    
    if (!*enc_ctx) return -1;

    // Optimization: Convert double FPS to rational number
    AVRational fps_rat = av_d2q(fps, 100000); 
    (*enc_ctx)->width = width;
    (*enc_ctx)->height = height;
    (*enc_ctx)->time_base = av_inv_q(fps_rat); // Timebase is 1/FPS
    (*enc_ctx)->framerate = fps_rat;
 
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;
 
    // GOP Setup: 1 keyframe per second
    (*enc_ctx)->gop_size = (i32)(fps + 0.5);
    (*enc_ctx)->max_b_frames = 2;
    
    if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Quality/Speed trade-off
    av_opt_set((*enc_ctx)->priv_data, "preset", "medium", 0);
    av_opt_set((*enc_ctx)->priv_data, "crf", "23", 0);

    if (avcodec_open2(*enc_ctx, codec, NULL) < 0) {
        fprintf(stderr, "[Transcoder] Error: Could not open encoder.\n");
        return -1;
    }
    
    avcodec_parameters_from_context((*out_stream)->codecpar, *enc_ctx);
    return 0;
}

// --- Export Logic ---
void transcode_clip(VM* vm, ObjClip* clip, const char* output_filename) {
    // Resources
    MediaContext in_media; // [修改] 使用 MediaContext
    media_ctx_init(&in_media);

    AVFormatContext* out_fmt_ctx = NULL;
    AVCodecContext* enc_ctx = NULL;
    AVPacket* pkt = NULL;
    AVPacket* out_pkt = NULL;
    AVFrame* frame = NULL;
    
    i32 ret = 0;
    fprintf(stderr, "[Transcoder] Processing '%s' -> '%s'\n", clip->path->chars, output_filename);

    // --- Input Setup [修改] ---
    // 仅需视频流进行转码
    if (!media_open(&in_media, clip->path->chars, true, false)) {
        fprintf(stderr, "[Transcoder] Error: Could not open input file.\n");
        goto cleanup;
    }
    if (!in_media.vid_ctx) {
        fprintf(stderr, "[Transcoder] Error: No video stream found.\n");
        goto cleanup;
    }

    // --- Output Setup ---
    avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, output_filename);
    if (!out_fmt_ctx) goto cleanup;
    
    AVStream* out_stream = NULL;
    if (open_encoder_internal(vm, out_fmt_ctx, &enc_ctx, &out_stream,
                     clip->width, clip->height, clip->fps) < 0) {
        goto cleanup;
    }
    
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) goto cleanup;
    }
    if (avformat_write_header(out_fmt_ctx, NULL) < 0) goto cleanup;

    // --- Seeking ---
    i64 seek_target_us = (i64)(clip->in_point * AV_TIME_BASE);
    i64 seek_target_ts = av_rescale_q(seek_target_us, AV_TIME_BASE_Q, in_media.vid_stream->time_base);
 
    if (clip->in_point > 0) {
        av_seek_frame(in_media.fmt_ctx, in_media.vid_stream_idx, seek_target_ts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        avcodec_flush_buffers(in_media.vid_ctx);
    }

    // --- Allocation ---
    pkt = av_packet_alloc();
    vm->bytesAllocated += sizeof(AVPacket);
    out_pkt = av_packet_alloc();
    vm->bytesAllocated += sizeof(AVPacket);
    frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    
    if (!pkt || !out_pkt || !frame) goto cleanup;

    i64 encoded_frame_count = 0;
    i64 total_frames = (i64)(clip->duration * clip->fps);
    bool encode_finished = false;

    // --- Main Transcode Loop [修改] ---
    while (av_read_frame(in_media.fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == in_media.vid_stream_idx) {
         
            ret = avcodec_send_packet(in_media.vid_ctx, pkt);
            if (ret < 0) {
                av_packet_unref(pkt);
                continue;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(in_media.vid_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) goto cleanup;

                // Accuracy Logic
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
                frame->pts = encoded_frame_count; // Force CFR
                
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
                    printf("\r[Transcoder] Progress: %.0f%%",
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
    printf("\n[Transcoder] Done.\n");

cleanup:
    media_close(&in_media); // [修改] 统一释放输入端

    if (enc_ctx) { avcodec_free_context(&enc_ctx); vm->bytesAllocated -= sizeof(AVCodecContext); }
    if (frame) { av_frame_free(&frame); vm->bytesAllocated -= sizeof(AVFrame); }
    if (pkt) { av_packet_free(&pkt); vm->bytesAllocated -= sizeof(AVPacket); }
    if (out_pkt) { av_packet_free(&out_pkt); vm->bytesAllocated -= sizeof(AVPacket); }
    
    if (out_fmt_ctx) {
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
    }
}