// src/engine/video_player.c

#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h> // for av_gettime_relative
#include <SDL2/SDL.h>
#include "common.h"
#include "engine/video.h"
#include "engine/compositor.h"
#include "engine/timeline.h"

// --- Helper: Frame Timing ---
static double get_clock() {
    return (double)av_gettime_relative() / 1000000.0;
}

// --- Main Player Logic ---
void play_video_clip(ObjClip* clip) {
    // Resources
    AVFormatContext* fmt_ctx = NULL;
    AVCodecContext* dec_ctx = NULL;
    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    AVFrame* frame_yuv = NULL; // Scratch buffer for pixel conversion
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
    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0) goto cleanup;
    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!decoder) goto cleanup;
    dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, video_stream->codecpar);
   
    // Threading Optimization: Enable multi-threaded decoding
    if (decoder->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        dec_ctx->thread_count = 0; // Auto-detect
        dec_ctx->thread_type = FF_THREAD_FRAME;
    }
    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) goto cleanup;
    // --- 2. Seek Handling ---
    // Convert in_point (seconds) to stream timebase
    i64 seek_target_ts = 0;
    if (clip->in_point > 0) {
        seek_target_ts = (i64)(clip->in_point / av_q2d(video_stream->time_base));
        av_seek_frame(fmt_ctx, video_stream_idx, seek_target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_ctx);
    }
    // --- 3. SDL Setup ---
   
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        fprintf(stderr, "[Error] SDL init failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    int width = dec_ctx->width;
    int height = dec_ctx->height;
    // Window Setup: Half size by default for HiDPI/Large videos
    window = SDL_CreateWindow("Luna Preview",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width / 2, height / 2,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) goto cleanup;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) goto cleanup;
    // Texture: Use YV12 (YUV 4:2:0) for hardware acceleration
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                width, height);
    // --- 4. Buffer Allocation ---
   
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    frame_yuv = av_frame_alloc(); // For sws_scale output
    if (!pkt || !frame || !frame_yuv) goto cleanup;
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    yuv_buffer = (u8*)av_malloc(num_bytes);
    av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, yuv_buffer,
                         AV_PIX_FMT_YUV420P, width, height, 1);
    // SWS Context: Initialize only if conversion is needed (Lazy init)
    // Note: Initialized inside loop or here if assuming constant format.
    // --- 5. Playback Loop ---
   
    printf("[Preview] Playing... (Press ESC to stop)\n");
    bool running = true;
    SDL_Event event;
   
    // Sync Logic: Video Clock
    double start_time = get_clock();
    // double pts_offset = 0; // 未使用，注释掉避免警告
    while (running && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
           
            if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                   
                    // Accuracy: Skip frames before in_point (result of inaccurate seek)
                    double pts_sec = frame->pts * av_q2d(video_stream->time_base);
                    if (pts_sec < clip->in_point) {
                        continue;
                    }
                    // Trim Check
                    if (pts_sec >= clip->in_point + clip->duration) {
                        running = false;
                        break;
                    }
                    // --- Sync Strategy: PTS vs Wall Clock ---
                    // Current video time relative to clip start
                    double video_time = pts_sec - clip->in_point;
                   
                    // Current real time elapsed since playback started
                    double real_time = get_clock() - start_time;
                    double delay = video_time - real_time;
                    // If video is ahead, wait.
                    if (delay > 0) {
                        // Use a precise wait loop for short delays, SDL_Delay for long ones
                         if (delay > 0.010) SDL_Delay((u32)(delay * 1000));
                    }
                    // If video is behind (delay < 0), we are lagging.
                    // Ideally drop frames, but for preview we just render ASAP.
                    // --- Input Handling ---
                    while (SDL_PollEvent(&event)) {
                        if (event.type == SDL_QUIT) running = false;
                        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
                    }
                    if (!running) break;
                    // --- Render ---
                    // Optimization: Direct render if format matches, otherwise convert
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
                }
            }
        }
        av_packet_unref(pkt);
    }
cleanup:
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    if (yuv_buffer) av_free(yuv_buffer);
    if (frame) av_frame_free(&frame);
    if (frame_yuv) av_frame_free(&frame_yuv);
    if (pkt) av_packet_free(&pkt);
   
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (dec_ctx) avcodec_free_context(&dec_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    printf("[Preview] Closed.\n");
}

void play_timeline(Timeline* tl) {
    if (!tl) return;
    printf("[Preview] Starting Timeline Playback (%dx%d @ %.2f fps)...\n",
           tl->width, tl->height, tl->fps);
    // 1. 初始化 SDL (如果尚未初始化)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL Error: %s\n", SDL_GetError());
        return;
    }
    SDL_Window* window = SDL_CreateWindow("Luna Timeline Preview",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          tl->width, tl->height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* screen_texture = SDL_CreateTexture(renderer,
                                                    SDL_PIXELFORMAT_ABGR8888, // Compositor 输出 RGBA
                                                    SDL_TEXTUREACCESS_STREAMING,
                                                    tl->width, tl->height);
    // 2. 创建合成器
    Compositor* comp = compositor_create(tl);
    // 3. 播放循环
    bool running = true;
    bool paused = false;
    double current_time = 0.0;
    uint64_t last_perf = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.key.keysym.sym == SDLK_SPACE) paused = !paused;
            }
        }
        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)((now - last_perf) * 1000 / SDL_GetPerformanceFrequency()) / 1000.0;
        last_perf = now;
        if (!paused) {
            current_time += dt;
            if (current_time > tl->duration) current_time = 0.0; // Loop
        }
        // 核心：合成器渲染
        compositor_render(comp, current_time);
       
        // 上传纹理
        void* pixels = compositor_get_buffer(comp);
        SDL_UpdateTexture(screen_texture, NULL, pixels, tl->width * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, screen_texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    // 4. 清理
    compositor_free(comp);
    SDL_DestroyTexture(screen_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    // SDL_Quit(); // 保持 SDL 开启以便后续调用
}
