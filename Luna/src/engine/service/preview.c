// src/engine/service/preview.c

#include "preview.h"
#include "engine/media/utils/ffmpeg_utils.h" // [新增]
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "core/vm/vm.h"

static double get_clock() {
    return (double)SDL_GetTicks() / 1000.0;
}

// 独立的预览播放函数
void play_video_clip_preview(VM* vm, ObjClip* clip) {
    MediaContext ctx;
    media_ctx_init(&ctx);

    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    AVFrame* frame_yuv = NULL;
    u8* yuv_buffer = NULL;
    struct SwsContext* sws_ctx = NULL;
    
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    SDL_Texture* texture = NULL;
    
    printf("[Preview] Opening '%s'...\n", clip->path->chars);

    // --- 1. FFmpeg Init [修改] ---
    if (!media_open(&ctx, clip->path->chars, true, false)) {
        fprintf(stderr, "[Error] Could not open file.\n");
        goto cleanup;
    }
    if (!ctx.vid_ctx) {
        fprintf(stderr, "[Error] No video stream.\n");
        goto cleanup;
    }

    // --- 2. Seek ---
    if (clip->in_point > 0) {
        i64 seek_target_ts = (i64)(clip->in_point / av_q2d(ctx.vid_stream->time_base));
        av_seek_frame(ctx.fmt_ctx, ctx.vid_stream_idx, seek_target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(ctx.vid_ctx);
    }

    // --- 3. SDL Init ---
    // ... [中间 SDL 初始化代码保持不变] ...
    bool was_sdl_init = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
    if (!was_sdl_init) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) goto cleanup;
    }

    i32 width = ctx.vid_ctx->width;
    i32 height = ctx.vid_ctx->height;

    // 创建窗口
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
    
    // [修改] 使用 ctx.fmt_ctx
    while (running && av_read_frame(ctx.fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == ctx.vid_stream_idx) {
            if (avcodec_send_packet(ctx.vid_ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx.vid_ctx, frame) == 0) {
                    // Sync
                    double pts_sec = frame->pts * av_q2d(ctx.vid_stream->time_base);
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
                            sws_ctx = sws_getContext(width, height, ctx.vid_ctx->pix_fmt,
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

                    // Event Handling
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id) {
                             if (event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
                        }
                        if (event.type == SDL_KEYDOWN && event.key.windowID == window_id) {
                            if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                        }
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
    
    if (yuv_buffer) av_free(yuv_buffer);
    if (frame) av_frame_free(&frame);
    if (frame_yuv) av_frame_free(&frame_yuv);
    if (pkt) av_packet_free(&pkt);
    if (sws_ctx) sws_freeContext(sws_ctx);
    
    media_close(&ctx); // [修改]
    printf("[Preview] Closed.\n");
}