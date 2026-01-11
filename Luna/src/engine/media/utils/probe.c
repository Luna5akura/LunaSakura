// src/engine/media/utils/probe.c

#include "engine/media/utils/probe.h"
#include "engine/media/utils/ffmpeg_utils.h" // [新增]
#include "core/vm/vm.h" 

VideoMeta load_video_metadata(VM* vm, const char* filepath) {
    VideoMeta meta = {0};
    MediaContext ctx; 

    // 使用工具打开，仅请求视频流
    if (!media_open(&ctx, filepath, true, false)) {
        return meta; // success = false
    }

    if (!ctx.vid_ctx || !ctx.vid_stream) {
        fprintf(stderr, "[Probe] No video stream found in '%s'\n", filepath);
        goto cleanup;
    }

    AVCodecParameters* codecpar = ctx.vid_stream->codecpar;

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
        if (fabs(rotation) - 90.0 < 1.0 || fabs(rotation) - 270.0 < 1.0) {
            meta.width = codecpar->height;
            meta.height = codecpar->width;
        }
    }

    // --- FPS 计算 ---
    AVRational fps_rat = ctx.vid_stream->avg_frame_rate;
    if (fps_rat.den <= 0 || fps_rat.num <= 0) {
        fps_rat = ctx.vid_stream->r_frame_rate;
    }
    if (fps_rat.den > 0) {
        meta.fps = av_q2d(fps_rat);
    } else {
        meta.fps = 30.0;
    }

    // --- 时长计算 ---
    if (ctx.fmt_ctx->duration != AV_NOPTS_VALUE) {
        meta.duration = (double)ctx.fmt_ctx->duration / AV_TIME_BASE;
    } else if (ctx.vid_stream->duration != AV_NOPTS_VALUE) {
        meta.duration = ctx.vid_stream->duration * av_q2d(ctx.vid_stream->time_base);
    } else if (ctx.vid_stream->nb_frames > 0) {
        meta.duration = ctx.vid_stream->nb_frames / meta.fps;
    } else {
        meta.duration = 0.0;
    }

    meta.success = true;

cleanup:
    // 统一释放
    media_close(&ctx);
    return meta;
}