// src/engine/video_player.c

#include <stdio.h>
// 移除 glad 引用，因为 play_video_clip 使用 SDL 2D Renderer，不直接操作 GL
// #include <glad/glad.h> 
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include "video.h"
#include "compositor.h"
#include "timeline.h"
#include "vm/memory.h"
#include "vm/vm.h"

// --- Helper: Frame Timing ---
static double get_clock() {
    return (double)av_gettime_relative() / 1000000.0;
}

// --- 单个素材播放 (弹出独立窗口预览原始素材) ---
// [修改] 增加 SDL 初始化检查，避免与 Host 冲突
void play_video_clip(VM* vm, ObjClip* clip) {
    // Resources
    AVFormatContext* fmt_ctx = NULL;
    AVCodecContext* dec_ctx = NULL;
    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    AVFrame* frame_yuv = NULL;
    u8* yuv_buffer = NULL;
    struct SwsContext* sws_ctx = NULL;

    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    SDL_Texture* texture = NULL;

    const char* filename = clip->path->chars;
    printf("[Preview] Opening '%s'...\n", filename);

    // --- 1. FFmpeg Setup ---
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        fprintf(stderr, "[Error] Could not open file.\n");
        goto cleanup;
    }
    
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) goto cleanup;

    i32 video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0) goto cleanup;

    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!decoder) goto cleanup;

    dec_ctx = avcodec_alloc_context3(decoder);
    vm->bytesAllocated += sizeof(AVCodecContext); // Track
    avcodec_parameters_to_context(dec_ctx, video_stream->codecpar);

    if (decoder->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        dec_ctx->thread_count = 0;
        dec_ctx->thread_type = FF_THREAD_FRAME;
    }

    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) goto cleanup;

    // --- 2. Seek Handling ---
    if (clip->in_point > 0) {
        i64 seek_target_ts = (i64)(clip->in_point / av_q2d(video_stream->time_base));
        av_seek_frame(fmt_ctx, video_stream_idx, seek_target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_ctx);
    }

    // --- 3. SDL Setup (Context Aware) ---
    // [修复] 检查 SDL 是否已初始化，避免与 main.c 冲突
    bool existing_sdl = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
    if (!existing_sdl) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
            fprintf(stderr, "[Error] SDL init failed: %s\n", SDL_GetError());
            goto cleanup;
        }
    }

    i32 width = dec_ctx->width;
    i32 height = dec_ctx->height;

    // 使用 SDL_Renderer (2D API)，这与 main.c 的 OpenGL Context 是独立的，通常可以共存
    // 如果是 OpenGL 模式，需要共享 Context，但这里我们只做简单的弹窗预览
    window = SDL_CreateWindow("Luna Clip Preview",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width / 2, height / 2,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) goto cleanup;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) goto cleanup;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                width, height);

    // --- 4. Buffer Allocation ---
    pkt = av_packet_alloc();
    vm->bytesAllocated += sizeof(AVPacket); // Track
    frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    frame_yuv = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    if (!pkt || !frame || !frame_yuv) goto cleanup;

    i32 num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    yuv_buffer = (u8*)av_malloc(num_bytes);
    if (yuv_buffer) vm->bytesAllocated += num_bytes;

    av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, yuv_buffer,
                         AV_PIX_FMT_YUV420P, width, height, 1);

    // --- 5. Playback Loop ---
    printf("[Preview] Playing... (Press ESC to stop)\n");
    bool running = true;
    SDL_Event event;
    double start_time = get_clock();

    while (running && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                    double pts_sec = frame->pts * av_q2d(video_stream->time_base);
                    
                    if (pts_sec < clip->in_point) continue;
                    if (pts_sec >= clip->in_point + clip->duration) {
                        running = false;
                        break;
                    }

                    double video_time = pts_sec - clip->in_point;
                    double real_time = get_clock() - start_time;
                    double delay = video_time - real_time;

                    if (delay > 0) {
                         if (delay > 0.010) SDL_Delay((u32)(delay * 1000));
                    }

                    while (SDL_PollEvent(&event)) {
                        // [注意] 这里只处理当前窗口的事件，但在 SDL 中 PollEvent 是全局的
                        // 在多窗口环境下，需要检查 event.window.windowID
                        if (event.type == SDL_QUIT) running = false; // 这可能会关闭主程序，需注意
                        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
                        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
                            if (SDL_GetWindowID(window) == event.window.windowID) running = false;
                        }
                    }
                    if (!running) break;

                    AVFrame* render_frame = frame;
                    if (frame->format != AV_PIX_FMT_YUV420P) {
                        if (!sws_ctx) {
                            sws_ctx = sws_getContext(width, height, dec_ctx->pix_fmt,
                                                     width, height, AV_PIX_FMT_YUV420P,
                                                     SWS_BILINEAR, NULL, NULL, NULL);
                            vm->bytesAllocated += sizeof(struct SwsContext);
                        }
                        sws_scale(sws_ctx, (const u8* const*)frame->data, frame->linesize,
                                  0, height, frame_yuv->data, frame_yuv->linesize);
                        render_frame = frame_yuv;
                    }

                    SDL_UpdateYUVTexture(texture, NULL,
                                         render_frame->data[0], render_frame->linesize[0],
                                         render_frame->data[1], render_frame->linesize[1],
                                         render_frame->data[2], render_frame->linesize[2]);

                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);
                }
            }
        }
        av_packet_unref(pkt);
    }

cleanup:
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    
    // [修复] 不要调用 SDL_Quit，因为 main.c 还在运行
    // if (!existing_sdl) SDL_Quit(); 

    if (yuv_buffer) {
        av_free(yuv_buffer);
        vm->bytesAllocated -= num_bytes;
    }
    if (frame) {
        av_frame_free(&frame);
        vm->bytesAllocated -= sizeof(AVFrame);
    }
    if (frame_yuv) {
        av_frame_free(&frame_yuv);
        vm->bytesAllocated -= sizeof(AVFrame);
    }
    if (pkt) {
        av_packet_free(&pkt);
        vm->bytesAllocated -= sizeof(AVPacket);
    }
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        vm->bytesAllocated -= sizeof(struct SwsContext);
    }
    if (dec_ctx) {
        avcodec_free_context(&dec_ctx);
        vm->bytesAllocated -= sizeof(AVCodecContext);
    }
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    printf("[Preview] Clip Closed.\n");
}

// [已删除] play_timeline 函数
// 该功能已移交至 main.c (Host) 处理，避免 GL Context 冲突。