// src/engine/simple_player.c

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>  // <--- 🔴 必须添加这一行！
#include <SDL2/SDL.h>

#include "vm/object.h" // 必须引入，为了访问 ObjClip

// 播放视频的核心函数
void play_video_clip(ObjClip* clip) {
    const char* filename = clip->path->chars;
    // 1. FFmpeg: 打开文件
    AVFormatContext* fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        printf("[Error] Could not open file %s\n", filename);
        return;
    }
    avformat_find_stream_info(fmt_ctx, NULL);

    // 2. FFmpeg: 找到视频流和解码器
    int video_stream_idx = -1;
    AVCodecParameters* codec_par = NULL;
    
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            codec_par = fmt_ctx->streams[i]->codecpar;
            break;
        }
    }

    const AVCodec* codec = avcodec_find_decoder(codec_par->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codec_par);
    avcodec_open2(codec_ctx, codec, NULL);

    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    
    // 将秒转换为 FFmpeg 的内部时间戳 (Time Base)
    // 公式: timestamp = seconds / time_base
    int64_t seek_target = (int64_t)(clip->in_point / av_q2d(video_stream->time_base));
    
    if (clip->in_point > 0) {
        printf("[Preview] Seeking to %.2fs ...\n", clip->in_point);
        // AVSEEK_FLAG_BACKWARD 表示如果找不到精确帧，就找时间戳之前的关键帧
        if (av_seek_frame(fmt_ctx, video_stream_idx, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
            printf("[Error] Seek failed!\n");
        }
        // Seek 后通常需要清空一下解码器缓存
        avcodec_flush_buffers(codec_ctx);
    }

    // 3. SDL: 初始化
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("[Error] SDL init failed: %s\n", SDL_GetError());
        return;
    }

    int width = codec_ctx->width;
    int height = codec_ctx->height;

    SDL_Window* window = SDL_CreateWindow("Luna Preview", 
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          width, height, SDL_WINDOW_RESIZABLE);
    
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    
    // 创建纹理 (YUV420P 是最常见的视频格式)
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, 
                                             SDL_TEXTUREACCESS_STREAMING, 
                                             width, height);

    // 4. 准备图像转换 (SWS Context)
    // 即使源是 YUV420P，FFmpeg解码出来的 linesize 可能和 SDL 不一致，所以最好用 sws_scale 转一下
    struct SwsContext* sws_ctx = sws_getContext(
        width, height, codec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL
    );

    // 5. 循环解码播放
    AVFrame* frame = av_frame_alloc();           // 原始帧
    AVFrame* frame_yuv = av_frame_alloc();       // 转换给 SDL 用的帧
    
    // 分配 frame_yuv 的内存
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
    av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, buffer, 
                         AV_PIX_FMT_YUV420P, width, height, 1);

    AVPacket* packet = av_packet_alloc();
    SDL_Event event;
    int running = 1;

    printf("[Preview] Playing... (Press ESC to close window)\n");

    double start_play_time = (double)SDL_GetTicks() / 1000.0; // 记录物理开始时间

    while (running && av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_idx) {

            double current_time = ((double)SDL_GetTicks() / 1000.0) - start_play_time;
            if (current_time >= clip->duration) {
                printf("[Preview] Clip duration reached (%.2fs).\n", clip->duration);
                running = 0;
            }
            // 发送包给解码器
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                // 接收解码后的帧
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    
                    // 处理 SDL 事件 (防止窗口卡死)
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_QUIT || 
                           (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                            running = 0;
                        }

                        SDL_Delay(33); // ~30fps
                    }
                    if (!running) break;

                    // 转换图像格式 -> YUV420P
                    sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 
                              0, height, frame_yuv->data, frame_yuv->linesize);

                    // 更新 SDL 纹理
                    SDL_UpdateYUVTexture(texture, NULL, 
                                         frame_yuv->data[0], frame_yuv->linesize[0],
                                         frame_yuv->data[1], frame_yuv->linesize[1],
                                         frame_yuv->data[2], frame_yuv->linesize[2]);
                    
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);

                    // 简单的帧率控制 (40ms ≈ 25fps)，后面我们要写精确的时间轴同步
                    SDL_Delay(40); 
                }
            }
        }
        av_packet_unref(packet);
    }

    // 6. 清理内存
    av_free(buffer);
    av_frame_free(&frame);
    av_frame_free(&frame_yuv);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    sws_freeContext(sws_ctx);
    
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}