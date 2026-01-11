// src/engine/media/utils/ffmpeg_utils.h

#pragma once

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdbool.h>

// 统一封装输入文件的上下文
typedef struct {
    AVFormatContext* fmt_ctx;
    
    // 视频部分
    AVCodecContext* vid_ctx;
    int vid_stream_idx;
    AVStream* vid_stream; // 便捷访问
    
    // 音频部分
    AVCodecContext* aud_ctx;
    int aud_stream_idx;
    AVStream* aud_stream; // 便捷访问

    const char* filepath;
} MediaContext;

// 初始化 MediaContext (清零)
void media_ctx_init(MediaContext* ctx);

// 打开媒体文件并初始化解码器
// open_video: 是否尝试打开视频流
// open_audio: 是否尝试打开音频流
bool media_open(MediaContext* ctx, const char* filepath, bool open_video, bool open_audio);

// 关闭并释放资源
void media_close(MediaContext* ctx);