// src/engine/video_probe.c

#include <stdio.h>
#include <math.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include "engine/video.h"
#include "core/vm/vm.h" 

// [移除] static Table metadata_cache; 以及相关的缓存逻辑
// [移除] #include "core/compiler/compiler_internal.h" (不再需要 compilingVM)

VideoMeta load_video_metadata(VM* vm, const char* filepath) {
    // 暂时移除缓存逻辑以防止 GC 崩溃
    // 如果后续需要缓存，必须将 metadata_cache 注册到 memory.c 的 markRoots 中

    VideoMeta meta = {0};
    AVFormatContext* fmt_ctx = NULL;

    // 1. 打开文件
    if (avformat_open_input(&fmt_ctx, filepath, NULL, NULL) < 0) {
        fprintf(stderr, "[Probe] Error: Could not open file '%s'\n", filepath);
        goto cleanup;
    }

    // 2. 获取流信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "[Probe] Error: Could not find stream info.\n");
        goto cleanup;
    }

    // 3. 查找最佳视频流
    i32 video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0) {
        fprintf(stderr, "[Probe] Error: No video stream found in '%s'\n", filepath);
        goto cleanup;
    }

    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    AVCodecParameters* codecpar = video_stream->codecpar;

    // --- 基础宽高 ---
    meta.width = codecpar->width;
    meta.height = codecpar->height;

    // --- 旋转检测 ---
    const AVPacketSideData *sd = av_packet_side_data_get(
        codecpar->coded_side_data,
        codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX
    );
    
    i32* display_matrix = NULL;
    if (sd) {
        display_matrix = (i32*)sd->data;
    }
    
    if (display_matrix) {
        double rotation = av_display_rotation_get(display_matrix);
        // 如果旋转了 90 或 270 度，交换宽高
        if (fabs(rotation) - 90.0 < 1.0 || fabs(rotation) - 270.0 < 1.0) {
            meta.width = codecpar->height;
            meta.height = codecpar->width;
        }
    }

    // --- FPS 计算 ---
    AVRational fps_rat = video_stream->avg_frame_rate;
    if (fps_rat.den <= 0 || fps_rat.num <= 0) {
        fps_rat = video_stream->r_frame_rate;
    }
    if (fps_rat.den > 0) {
        meta.fps = av_q2d(fps_rat);
    } else {
        meta.fps = 30.0;
    }

    // --- 时长计算 ---
    if (fmt_ctx->duration != AV_NOPTS_VALUE) {
        meta.duration = (double)fmt_ctx->duration / AV_TIME_BASE;
    } else if (video_stream->duration != AV_NOPTS_VALUE) {
        meta.duration = video_stream->duration * av_q2d(video_stream->time_base);
    } else if (video_stream->nb_frames > 0) {
        meta.duration = video_stream->nb_frames / meta.fps;
    } else {
        meta.duration = 0.0;
    }

    meta.success = true;

cleanup:
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    return meta;
}