// src/engine/media/utils/ffmpeg_utils.c

#include "ffmpeg_utils.h"

void media_ctx_init(MediaContext* ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(MediaContext));
    ctx->vid_stream_idx = -1;
    ctx->aud_stream_idx = -1;
}

static int open_codec_context(AVFormatContext* fmt_ctx, enum AVMediaType type, int* stream_idx, 
                              AVCodecContext** dec_ctx, AVStream** out_stream) {
    // 1. 查找最佳流
    int ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        return ret; // 未找到流，返回错误码
    }
    *stream_idx = ret;
    AVStream* st = fmt_ctx->streams[*stream_idx];
    if (out_stream) *out_stream = st;

    // 2. 查找解码器
    const AVCodec* decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder) {
        fprintf(stderr, "[FFmpegUtils] Failed to find decoder for stream %d\n", *stream_idx);
        return -1;
    }

    // 3. 分配上下文
    *dec_ctx = avcodec_alloc_context3(decoder);
    if (!*dec_ctx) return -1;

    // 4. 复制参数
    if (avcodec_parameters_to_context(*dec_ctx, st->codecpar) < 0) {
        return -1;
    }

    // 5. 打开解码器
    if (avcodec_open2(*dec_ctx, decoder, NULL) < 0) {
        fprintf(stderr, "[FFmpegUtils] Failed to open codec for stream %d\n", *stream_idx);
        return -1;
    }

    return 0;
}

bool media_open(MediaContext* ctx, const char* filepath, bool open_video, bool open_audio) {
    media_ctx_init(ctx);
    ctx->filepath = filepath; // 注意：这里只保存指针，调用者需保证 filepath 生命周期

    // 1. 打开文件
    if (avformat_open_input(&ctx->fmt_ctx, filepath, NULL, NULL) < 0) {
        fprintf(stderr, "[FFmpegUtils] Could not open file: %s\n", filepath);
        return false;
    }

    // 2. 检索流信息
    if (avformat_find_stream_info(ctx->fmt_ctx, NULL) < 0) {
        fprintf(stderr, "[FFmpegUtils] Could not find stream info: %s\n", filepath);
        media_close(ctx);
        return false;
    }

    // 3. 初始化视频
    if (open_video) {
        if (open_codec_context(ctx->fmt_ctx, AVMEDIA_TYPE_VIDEO, 
                              &ctx->vid_stream_idx, &ctx->vid_ctx, &ctx->vid_stream) < 0) {
            // 如果必须有视频但失败了，视业务逻辑决定是否返回 false。
            // 这里为了宽容度，只是打印警告，允许只有音频的情况（或者反之）
            // fprintf(stderr, "[FFmpegUtils] Warning: No video stream found in %s\n", filepath);
        }
    }

    // 4. 初始化音频
    if (open_audio) {
        open_codec_context(ctx->fmt_ctx, AVMEDIA_TYPE_AUDIO, 
                           &ctx->aud_stream_idx, &ctx->aud_ctx, &ctx->aud_stream);
    }

    return true;
}

void media_close(MediaContext* ctx) {
    if (!ctx) return;
    if (ctx->vid_ctx) avcodec_free_context(&ctx->vid_ctx);
    if (ctx->aud_ctx) avcodec_free_context(&ctx->aud_ctx);
    if (ctx->fmt_ctx) avformat_close_input(&ctx->fmt_ctx);
    
    // 重置结构体防止悬垂指针
    media_ctx_init(ctx);
}