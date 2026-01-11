// src/engine/codec/encoder.c

#include "encoder.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

struct Encoder {
    AVFormatContext* fmt_ctx;
    AVCodecContext* codec_ctx;
    AVStream* stream;
    
    // 用于 RGB -> YUV 转换
    struct SwsContext* sws_ctx;
    AVFrame* yuv_frame;
    
    // 状态
    int64_t next_pts;
    int width;
    int height;
};

// 辅助：初始化 H.264 上下文
static int open_h264(Encoder* enc, double fps, int bitrate) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "[Encoder] H.264 encoder not found.\n");
        return -1;
    }

    enc->stream = avformat_new_stream(enc->fmt_ctx, NULL);
    enc->codec_ctx = avcodec_alloc_context3(codec);

    // 设置参数
    enc->codec_ctx->width = enc->width;
    enc->codec_ctx->height = enc->height;
    
    AVRational fps_rat = av_d2q(fps, 100000);
    enc->codec_ctx->time_base = av_inv_q(fps_rat);
    enc->codec_ctx->framerate = fps_rat;
    enc->stream->time_base = enc->codec_ctx->time_base;
    
    enc->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // H.264 最通用的格式
    enc->codec_ctx->gop_size = (int)fps;
    enc->codec_ctx->max_b_frames = 2;
    
    if (enc->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        enc->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 质量控制
    if (bitrate > 0) {
        enc->codec_ctx->bit_rate = bitrate;
    } else {
        // 默认使用 CRF (Constant Rate Factor)
        av_opt_set(enc->codec_ctx->priv_data, "preset", "medium", 0);
        av_opt_set(enc->codec_ctx->priv_data, "crf", "23", 0);
    }

    if (avcodec_open2(enc->codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "[Encoder] Failed to open codec.\n");
        return -1;
    }

    avcodec_parameters_from_context(enc->stream->codecpar, enc->codec_ctx);
    return 0;
}

Encoder* encoder_create(const char* filename, int width, int height, double fps, int bitrate) {
    Encoder* enc = (Encoder*)malloc(sizeof(Encoder));
    memset(enc, 0, sizeof(Encoder));
    enc->width = width;
    enc->height = height;

    // 1. 容器格式
    avformat_alloc_output_context2(&enc->fmt_ctx, NULL, NULL, filename);
    if (!enc->fmt_ctx) return NULL;

    // 2. 编解码器
    if (open_h264(enc, fps, bitrate) < 0) {
        encoder_finish(enc);
        return NULL;
    }

    // 3. 打开文件 IO
    if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&enc->fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "[Encoder] Could not open file '%s'\n", filename);
            encoder_finish(enc);
            return NULL;
        }
    }

    // 4. 写头
    if (avformat_write_header(enc->fmt_ctx, NULL) < 0) {
        encoder_finish(enc);
        return NULL;
    }

    // 5. 准备 YUV 帧缓存 (复用)
    enc->yuv_frame = av_frame_alloc();
    enc->yuv_frame->format = enc->codec_ctx->pix_fmt;
    enc->yuv_frame->width = width;
    enc->yuv_frame->height = height;
    av_frame_get_buffer(enc->yuv_frame, 32);

    return enc;
}

static void encode_internal(Encoder* enc, AVFrame* frame) {
    if (frame) {
        frame->pts = enc->next_pts++;
    }

    avcodec_send_frame(enc->codec_ctx, frame);
    
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(enc->codec_ctx, pkt) >= 0) {
        av_packet_rescale_ts(pkt, enc->codec_ctx->time_base, enc->stream->time_base);
        pkt->stream_index = enc->stream->index;
        av_interleaved_write_frame(enc->fmt_ctx, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

bool encoder_encode_rgb(Encoder* enc, uint8_t* rgb_buffer, int input_stride) {
    if (!enc->sws_ctx) {
        // 初始化 RGB -> YUV 转换器
        enc->sws_ctx = sws_getContext(
            enc->width, enc->height, AV_PIX_FMT_RGBA, // 假设输入是 RGBA (glReadPixels)
            enc->width, enc->height, enc->codec_ctx->pix_fmt,
            SWS_BILINEAR, NULL, NULL, NULL
        );
    }

    // 1. 转换像素格式
    const uint8_t* src_slice[] = { rgb_buffer };
    int src_stride[] = { input_stride };
    
    // 确保 Frame 可写
    av_frame_make_writable(enc->yuv_frame);
    
    sws_scale(enc->sws_ctx, src_slice, src_stride, 0, enc->height,
              enc->yuv_frame->data, enc->yuv_frame->linesize);

    // 2. 编码
    encode_internal(enc, enc->yuv_frame);
    return true;
}

bool encoder_encode_avframe(Encoder* enc, void* av_frame_ptr) {
    AVFrame* frame = (AVFrame*)av_frame_ptr;
    encode_internal(enc, frame);
    return true;
}

void encoder_finish(Encoder* enc) {
    if (!enc) return;

    // Flush encoder
    if (enc->codec_ctx) {
        encode_internal(enc, NULL);
    }

    // Write trailer
    if (enc->fmt_ctx) {
        av_write_trailer(enc->fmt_ctx);
        if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&enc->fmt_ctx->pb);
        }
        avformat_free_context(enc->fmt_ctx);
    }

    if (enc->codec_ctx) avcodec_free_context(&enc->codec_ctx);
    if (enc->yuv_frame) av_frame_free(&enc->yuv_frame);
    if (enc->sws_ctx) sws_freeContext(enc->sws_ctx);
    
    free(enc);
}