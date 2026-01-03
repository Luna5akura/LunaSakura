// src/engine/exporter.c

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h> // 用于设置编码参数
#include "vm/object.h"

// 辅助：初始化编码器 (H.264)
static int prepare_encoder(AVFormatContext* out_fmt_ctx, AVCodecContext** enc_ctx, 
                           AVStream** out_stream, int width, int height, double fps) {
    // 1. 查找 H.264 编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        printf("Codec H.264 not found.\n");
        return -1;
    }

    // 2. 创建视频流
    *out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    
    // 3. 配置编码器上下文
    *enc_ctx = avcodec_alloc_context3(codec);
    (*enc_ctx)->width = width;
    (*enc_ctx)->height = height;
    (*enc_ctx)->time_base = (AVRational){1, (int)fps}; // 时间基
    (*enc_ctx)->framerate = (AVRational){(int)fps, 1}; // 帧率
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;          // 像素格式
    (*enc_ctx)->gop_size = 10;                         // 关键帧间隔
    (*enc_ctx)->max_b_frames = 1;

    // H.264 特定选项 (ultrafast 速度最快，crf 23 质量中等)
    av_opt_set((*enc_ctx)->priv_data, "preset", "ultrafast", 0);
    av_opt_set((*enc_ctx)->priv_data, "crf", "23", 0);

    // 4. 打开编码器
    if (avcodec_open2(*enc_ctx, codec, NULL) < 0) return -1;

    // 5. 复制参数到流
    avcodec_parameters_from_context((*out_stream)->codecpar, *enc_ctx);
    return 0;
}

// 核心导出函数
void export_video_clip(ObjClip* clip, const char* output_filename) {
    printf("[Export] Starting export to '%s'...\n", output_filename);

    // === 1. 准备输入 (和 Preview 类似) ===
    AVFormatContext* in_fmt_ctx = NULL;
    avformat_open_input(&in_fmt_ctx, clip->path->chars, NULL, NULL);
    avformat_find_stream_info(in_fmt_ctx, NULL);
    
    int video_stream_idx = -1;
    for (unsigned i=0; i<in_fmt_ctx->nb_streams; i++) {
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) video_stream_idx = i;
    }
    
    AVCodecParameters* in_codec_par = in_fmt_ctx->streams[video_stream_idx]->codecpar;
    const AVCodec* in_codec = avcodec_find_decoder(in_codec_par->codec_id);
    AVCodecContext* dec_ctx = avcodec_alloc_context3(in_codec);
    avcodec_parameters_to_context(dec_ctx, in_codec_par);
    avcodec_open2(dec_ctx, in_codec, NULL);

    // Seek 到入点
    AVStream* in_stream = in_fmt_ctx->streams[video_stream_idx];
    int64_t seek_target = (int64_t)(clip->in_point / av_q2d(in_stream->time_base));
    av_seek_frame(in_fmt_ctx, video_stream_idx, seek_target, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec_ctx);

    // === 2. 准备输出 (MP4) ===
    AVFormatContext* out_fmt_ctx = NULL;
    avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, output_filename);
    
    AVCodecContext* enc_ctx = NULL;
    AVStream* out_stream = NULL;
    
    // 准备编码器 (使用原视频的宽高和FPS)
    // 注意：clip->fps 可能是浮点，这里简化取整，严谨项目要用分数
    if (prepare_encoder(out_fmt_ctx, &enc_ctx, &out_stream, 
                        clip->width, clip->height, clip->fps) < 0) {
        printf("[Export] Failed to init encoder.\n");
        return;
    }

    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
            printf("[Export] Could not open output file.\n");
            return;
        }
    }
    
    // 写入文件头
    avformat_write_header(out_fmt_ctx, NULL);

    // === 3. 循环转码 ===
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    // 🔴 修正1: Packet 在循环外分配一次即可
    AVPacket* out_pkt = av_packet_alloc();
    
    int frame_count = 0;
    int total_frames = (int)(clip->duration * clip->fps);
    
    while (av_read_frame(in_fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            avcodec_send_packet(dec_ctx, pkt);
            while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                
                if (frame_count >= total_frames) break;

                // 🔴 修正2: 清除源视频的帧类型标记
                // 否则 libx264 会报 "specified frame type is not compatible"
                frame->pict_type = AV_PICTURE_TYPE_NONE; 
                frame->pts = frame_count; 
                
                avcodec_send_frame(enc_ctx, frame);
                
                // 🔴 修正3: 正确的 Packet 循环处理
                while (avcodec_receive_packet(enc_ctx, out_pkt) == 0) {
                    av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_stream->time_base);
                    out_pkt->stream_index = out_stream->index;
                    
                    av_interleaved_write_frame(out_fmt_ctx, out_pkt);
                    
                    // 关键点：只解引用数据，不释放结构体指针！
                    av_packet_unref(out_pkt); 
                }
                
                frame_count++;
                if (frame_count % 30 == 0) {
                    printf("\r[Export] Progress: %d / %d frames", frame_count, total_frames);
                    fflush(stdout);
                }
            }
        }
        av_packet_unref(pkt);
        if (frame_count >= total_frames) break;
    }

    // === 4. 收尾 (Flush Encoder) ===
    avcodec_send_frame(enc_ctx, NULL);
    // 🔴 修正4: 这里的 Flush 循环也要修
    while (avcodec_receive_packet(enc_ctx, out_pkt) == 0) {
        av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_stream->time_base);
        av_interleaved_write_frame(out_fmt_ctx, out_pkt);
        av_packet_unref(out_pkt); // 同样只 unref
    }
    
    // 🔴 修正5: 最后再彻底释放指针
    av_packet_free(&out_pkt); 
    av_packet_free(&pkt);
    av_frame_free(&frame);

    av_write_trailer(out_fmt_ctx);
    printf("\n[Export] Done! Saved to %s\n", output_filename);
    printf("\n[Export] Done! Saved to %s\n", output_filename);

    // 清理 (略去部分 free 以缩短代码)
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt_ctx->pb);
    avformat_free_context(out_fmt_ctx);
    avformat_close_input(&in_fmt_ctx);
}