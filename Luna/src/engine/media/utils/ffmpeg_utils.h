// src/engine/media/utils/ffmpeg_utils.h

#pragma once

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <stdbool.h>

// 统一封装 FFmpeg 上下文，简化 Decoder 结构体
typedef struct {
    const char* filepath;
    AVFormatContext* fmt_ctx;
    
    // Video
    int vid_stream_idx;
    AVCodecContext* vid_ctx;
    AVStream* vid_stream;
    
    // Audio
    int aud_stream_idx;
    AVCodecContext* aud_ctx;
    AVStream* aud_stream;
} MediaContext;

// 初始化上下文（置零）
void media_ctx_init(MediaContext* ctx);

// 打开媒体文件并初始化解码器
bool media_open(MediaContext* ctx, const char* filepath, bool open_video, bool open_audio);

// 关闭并释放所有资源
void media_close(MediaContext* ctx);