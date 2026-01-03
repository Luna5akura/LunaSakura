#include <stdio.h>
#include <libavformat/avformat.h> // FFmpeg 核心头文件
#include "common.h"

// 定义一个简单的结构体来传出数据
typedef struct {
    double duration;
    int width;
    int height;
    double fps;
    bool success;
} VideoMeta;

// 这是一个 helper 函数，不暴露给 VM，只给 binding 层用
VideoMeta load_video_metadata(const char* filepath) {
    VideoMeta meta = {0, 0, 0, 0, false};
    AVFormatContext* fmt_ctx = NULL;

    // 1. 打开文件
    // FFmpeg log 稍微吵一点，可以关掉
    // av_log_set_level(AV_LOG_QUIET);

    if (avformat_open_input(&fmt_ctx, filepath, NULL, NULL) < 0) {
        printf("[FFmpeg] Could not open source file %s\n", filepath);
        return meta;
    }

    // 2. 检索流信息 (必须做，否则拿不到宽高)
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        printf("[FFmpeg] Could not find stream info\n");
        avformat_close_input(&fmt_ctx);
        return meta;
    }

    // 3. 找到视频流
    int video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {
        printf("[FFmpeg] Could not find a video stream.\n");
        avformat_close_input(&fmt_ctx);
        return meta;
    }

    // 4. 提取元数据
    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    
    // 宽、高
    meta.width = video_stream->codecpar->width;
    meta.height = video_stream->codecpar->height;
    
    // 计算 FPS (r_frame_rate 是有理数，分子/分母)
    if (video_stream->r_frame_rate.den > 0) {
        meta.fps = (double)video_stream->r_frame_rate.num / (double)video_stream->r_frame_rate.den;
    }

    // 计算时长 (duration 单位是 AV_TIME_BASE，通常是微秒)
    if (fmt_ctx->duration != AV_NOPTS_VALUE) {
        meta.duration = (double)fmt_ctx->duration / AV_TIME_BASE;
    } else {
        printf("[FFmpeg] Warning: Could not detect duration.\n");
    }

    meta.success = true;

    // 5. 关闭文件 (只是读取元数据，读完就关)
    avformat_close_input(&fmt_ctx);
    
    return meta;
}