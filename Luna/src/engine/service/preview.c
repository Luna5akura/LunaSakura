// src/engine/service/preview.c

#include "preview.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "core/vm/vm.h"

static double get_clock() {
    return (double)SDL_GetTicks() / 1000.0;
}

// 独立的预览播放函数
void play_video_clip_preview(VM* vm, ObjClip* clip) {
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

    // --- 1. FFmpeg Init ---
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
    avcodec_parameters_to_context(dec_ctx, video_stream->codecpar);
    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) goto cleanup;

    // --- 2. Seek ---
    if (clip->in_point > 0) {
        i64 seek_target_ts = (i64)(clip->in_point / av_q2d(video_stream->time_base));
        av_seek_frame(fmt_ctx, video_stream_idx, seek_target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_ctx);
    }

    // --- 3. SDL Init ---
    // 检查 SDL 是否已初始化，防止重复初始化导致错误，也不要随意 Quit
    bool was_sdl_init = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
    if (!was_sdl_init) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) goto cleanup;
    }

    i32 width = dec_ctx->width;
    i32 height = dec_ctx->height;
    
    // 创建独立窗口
    window = SDL_CreateWindow("Clip Preview",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width / 2, height / 2,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) goto cleanup;
    
    u32 window_id = SDL_GetWindowID(window);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) goto cleanup;
    
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                width, height);

    // --- 4. Buffers ---
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    frame_yuv = av_frame_alloc();
    if (!pkt || !frame || !frame_yuv) goto cleanup;

    i32 num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    yuv_buffer = (u8*)av_malloc(num_bytes);
    av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, yuv_buffer,
                         AV_PIX_FMT_YUV420P, width, height, 1);

    // --- 5. Loop ---
    printf("[Preview] Playing... (Press ESC to close preview)\n");
    bool running = true;
    SDL_Event event;
    double start_time = get_clock();
    
    while (running && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                    // Sync
                    double pts_sec = frame->pts * av_q2d(video_stream->time_base);
                    if (pts_sec < clip->in_point) continue;
                    if (pts_sec >= clip->in_point + clip->duration) {
                        running = false;
                        break;
                    }
                    
                    double video_time = pts_sec - clip->in_point;
                    double real_time = get_clock() - start_time;
                    double delay = video_time - real_time;
                    if (delay > 0.001) SDL_Delay((u32)(delay * 1000));

                    // Render
                    AVFrame* render_frame = frame;
                    if (frame->format != AV_PIX_FMT_YUV420P) {
                        if (!sws_ctx) {
                            sws_ctx = sws_getContext(width, height, dec_ctx->pix_fmt,
                                                     width, height, AV_PIX_FMT_YUV420P,
                                                     SWS_BILINEAR, NULL, NULL, NULL);
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

                    // Event Handling (Scoped to this window)
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id) {
                             if (event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
                        }
                        if (event.type == SDL_KEYDOWN && event.key.windowID == window_id) {
                            if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                        }
                        // 如果有主循环在运行，非本窗口的事件可能会丢失。
                        // 在更复杂的架构中，需要有一个全局事件队列。
                        // 这里为了简单，我们只是"消费"掉事件。
                    }
                    if (!running) break;
                }
            }
        }
        av_packet_unref(pkt);
    }

cleanup:
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    
    // 不要调用 SDL_Quit，因为主程序还在运行
    
    if (yuv_buffer) av_free(yuv_buffer);
    if (frame) av_frame_free(&frame);
    if (frame_yuv) av_frame_free(&frame_yuv);
    if (pkt) av_packet_free(&pkt);
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (dec_ctx) avcodec_free_context(&dec_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    printf("[Preview] Closed.\n");
}