// src/engine/video_player.c

#include <stdio.h>
#include <glad/glad.h> // 必须在 SDL 之前包含
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
#include "vm/vm.h"  // Added for full struct VM definition

// --- Helper: Frame Timing ---
static double get_clock() {
    return (double)av_gettime_relative() / 1000000.0;
}

// --- 单个素材播放 (保持使用 SDL_Renderer 以获得最快启动速度) ---
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
    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0) goto cleanup;
    AVStream* video_stream = fmt_ctx->streams[video_stream_idx];
    const AVCodec* decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!decoder) goto cleanup;
    dec_ctx = avcodec_alloc_context3(decoder);
    vm->bytesAllocated += sizeof(AVCodecContext);  // Track
    avcodec_parameters_to_context(dec_ctx, video_stream->codecpar);
    
    if (decoder->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        dec_ctx->thread_count = 0;
        dec_ctx->thread_type = FF_THREAD_FRAME;
    }
    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) goto cleanup;

    // --- 2. Seek Handling ---
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
    vm->bytesAllocated += sizeof(AVPacket);  // Track
    frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    frame_yuv = av_frame_alloc(); 
    vm->bytesAllocated += sizeof(AVFrame);
    if (!pkt || !frame || !frame_yuv) goto cleanup;
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
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
                        if (event.type == SDL_QUIT) running = false;
                        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
                    }
                    if (!running) break;

                    AVFrame* render_frame = frame;
                    if (frame->format != AV_PIX_FMT_YUV420P) {
                        if (!sws_ctx) {
                            sws_ctx = sws_getContext(width, height, dec_ctx->pix_fmt,
                                                     width, height, AV_PIX_FMT_YUV420P,
                                                     SWS_BILINEAR, NULL, NULL, NULL);
                            vm->bytesAllocated += sizeof(struct SwsContext);  // Approximate
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
    // SDL_Quit(); // 注意：通常由 host 管理，这里如果是独立调用可能会导致 host 退出，暂且注释
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
    printf("[Preview] Closed.\n");
}

// --- 时间轴播放 (更新为支持 OpenGL Compositor) ---
void play_timeline(VM* vm, Timeline* tl) {
    if (!tl) return;
    printf("[Preview] Starting Timeline Playback (%dx%d @ %.2f fps)...\n",
           tl->width, tl->height, tl->fps);

    // 1. 初始化 SDL (GL模式)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL Error: %s\n", SDL_GetError());
        return;
    }

    // 设置 GL 属性
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("Luna Timeline Preview",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          tl->width, tl->height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    
    if (!window) {
        fprintf(stderr, "Failed to create GL window: %s\n", SDL_GetError());
        return;
    }

    // 创建 GL Context
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        return;
    }
    
    // 初始化 GLAD (必须在 context 创建后)
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        return;
    }

    // 开启垂直同步
    SDL_GL_SetSwapInterval(1);

    // 2. 创建合成器 (现在 GL 环境已就绪)
    Compositor* comp = compositor_create(vm, tl);
    if (!comp) {
        fprintf(stderr, "Failed to create compositor.\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        return;
    }

    // 3. 播放循环
    bool running = true;
    bool paused = false;
    double current_time = 0.0;
    uint64_t last_perf = SDL_GetPerformanceCounter();

    int win_w = tl->width;
    int win_h = tl->height;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.key.keysym.sym == SDLK_SPACE) paused = !paused;
            }
            else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = event.window.data1;
                win_h = event.window.data2;
                glViewport(0, 0, win_w, win_h);
            }
        }

        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)((now - last_perf) * 1000 / SDL_GetPerformanceFrequency()) / 1000.0;
        last_perf = now;

        if (!paused) {
            current_time += dt;
            if (current_time > tl->duration) current_time = 0.0; // Loop
        }

        // 核心：合成器渲染到 FBO
        compositor_render(comp, current_time);
        
        // 上屏：Blit FBO 到 屏幕
        compositor_blit_to_screen(comp, win_w, win_h);
        
        // 交换缓冲区
        SDL_GL_SwapWindow(window);
    }

    // 4. 清理
    compositor_free(vm, comp);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
}