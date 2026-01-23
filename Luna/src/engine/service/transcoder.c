// src/engine/service/transcoder.c

#include "engine/service/transcoder.h"
#include "engine/media/utils/ffmpeg_utils.h"
#include "engine/media/codec/encoder.h" // [新增] 引入 Encoder
#include "core/memory.h"
#include "core/vm/vm.h"

// [删除] open_encoder_internal 函数 (完全重复)

void transcode_clip(VM* vm, Clip* clip, const char* output_filename) {
    // 1. 输入环境 (保持不变)
    MediaContext in_media;
    media_ctx_init(&in_media);
    
    // 2. 输出环境 (改用 Encoder)
    Encoder* encoder = NULL;

    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    i32 ret = 0;

    fprintf(stderr, "[Transcoder] Processing '%s' -> '%s'\n", clip->path, output_filename);

    // --- Input Setup ---
    if (!media_open(&in_media, clip->path, true, false)) {
        fprintf(stderr, "[Transcoder] Error: Could not open input file.\n");
        goto cleanup;
    }
    if (!in_media.vid_ctx) {
        fprintf(stderr, "[Transcoder] Error: No video stream found.\n");
        goto cleanup;
    }

    // --- Output Setup (大幅简化) ---
    // 直接使用 Encoder 模块，统一管理 CRF、GOP 和 H.264 参数
    encoder = encoder_create(output_filename, clip->width, clip->height, clip->fps, 0);
    if (!encoder) {
        fprintf(stderr, "[Transcoder] Error: Could not create encoder.\n");
        goto cleanup;
    }

    // --- Seeking (保持不变) ---
    i64 seek_target_us = (i64)(clip->in_point * AV_TIME_BASE);
    i64 seek_target_ts = av_rescale_q(seek_target_us, AV_TIME_BASE_Q, in_media.vid_stream->time_base);
 
    if (clip->in_point > 0) {
        av_seek_frame(in_media.fmt_ctx, in_media.vid_stream_idx, seek_target_ts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        avcodec_flush_buffers(in_media.vid_ctx);
    }

    // --- Allocation ---
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) goto cleanup;

    i64 encoded_frame_count = 0;
    i64 total_frames = (i64)(clip->duration * clip->fps);
    bool encode_finished = false;

    // --- Main Loop ---
    while (av_read_frame(in_media.fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == in_media.vid_stream_idx) {
            ret = avcodec_send_packet(in_media.vid_ctx, pkt);
            if (ret < 0) { av_packet_unref(pkt); continue; }
            
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

                // [修改] Encode Logic
                // 直接传递 AVFrame 给 Encoder，Encoder 内部会处理 PTS 和写入
                if (!encoder_encode_avframe(encoder, frame)) {
                    fprintf(stderr, "[Transcoder] Encode failed.\n");
                    goto cleanup;
                }
                
                encoded_frame_count++;
                if (encoded_frame_count % 30 == 0) {
                    printf("\r[Transcoder] Progress: %.0f%%", (double)encoded_frame_count / total_frames * 100.0);
                    fflush(stdout);
                }
            }
        }
        av_packet_unref(pkt);
        if (encode_finished) break;
    }

    printf("\n[Transcoder] Done.\n");

cleanup:
    // 资源释放
    media_close(&in_media);
    
    // [修改] Encoder 负责释放输出端的所有资源 (ctx, stream, buffer 等)
    encoder_finish(encoder);

    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    // 移除了 out_pkt, out_fmt_ctx, enc_ctx 的手动释放，因为它们现在由 Encoder 管理
}