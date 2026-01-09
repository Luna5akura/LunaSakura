// src/engine/compositor.c

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>      // av_opt_set_... 需要这个
#include <libavutil/channel_layout.h> // AV_CHANNEL_LAYOUT_STEREO 需要这个
#include <libswscale/swscale.h> // 需要 swscale 进行格式转换
#include "compositor.h"
#include "core/memory.h"
#include "core/vm/vm.h"

#define MIX_SAMPLE_RATE 44100
#define MIX_CHANNELS 2
#define MIX_FORMAT AV_SAMPLE_FMT_FLT
#define SDL_MIX_FORMAT AUDIO_F32

// --- Forward Declarations ---
static ClipDecoder* create_decoder(VM* vm, ObjClip* clip);
static void draw_clip_rect(Compositor* comp, ClipDecoder* dec, TimelineClip* tc);
static void free_decoder(VM* vm, ClipDecoder* dec);

// 全局着色器
static GLuint global_shader_program = 0;

// --- Shader Sources ---
const char* VS_SOURCE =
"#version 330 core\n"
"layout (location = 0) in vec2 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"uniform mat4 u_projection;\n"
"uniform mat4 u_model;\n"
"void main() {\n"
" gl_Position = u_projection * u_model * vec4(aPos, 0.0, 1.0);\n"
" TexCoord = aTexCoord;\n"
"}\n";

// [修改] 使用简单的 RGB 采样器，因为我们在 CPU 端做 YUV->RGB 转换
const char* FS_SOURCE_RGB =
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D tex_rgb;\n"
"uniform float u_opacity;\n"
"void main() {\n"
" vec4 col = texture(tex_rgb, TexCoord);\n"
" FragColor = vec4(col.rgb, col.a * u_opacity);\n"
"}\n";

const char* VS_SCREEN =
"#version 330 core\n"
"layout (location = 0) in vec2 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"void main() {\n"
" gl_Position = vec4(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);\n"
" TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);\n"
"}\n";

const char* FS_SCREEN =
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D screenTexture;\n"
"void main() {\n"
" FragColor = texture(screenTexture, TexCoord);\n"
"}\n";

// --- Math Helpers ---
typedef struct { float m[16]; } mat4;
static mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    mat4 res = {0};
    res.m[0] = 2.0f / (right - left);
    res.m[5] = 2.0f / (top - bottom);
    res.m[10] = -2.0f / (far - near);
    res.m[12] = -(right + left) / (right - left);
    res.m[13] = -(top + bottom) / (top - bottom);
    res.m[14] = -(far + near) / (far - near);
    res.m[15] = 1.0f;
    return res;
}
static mat4 mat4_identity() {
    mat4 res = {0};
    res.m[0] = res.m[5] = res.m[10] = res.m[15] = 1.0f;
    return res;
}
static mat4 mat4_translate_scale(float x, float y, float sx, float sy) {
    mat4 res = mat4_identity();
    res.m[0] = sx;
    res.m[5] = sy;
    res.m[12] = x;
    res.m[13] = y;
    return res;
}

// --- GL Helpers ---
static GLuint compile_shader(const char* src, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    i32 success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader Compile Error: %s\n", infoLog);
    }
    return shader;
}

static void init_gl_resources(Compositor* comp) {
    if (global_shader_program == 0) {
        GLuint vs = compile_shader(VS_SOURCE, GL_VERTEX_SHADER);
        GLuint fs = compile_shader(FS_SOURCE_RGB, GL_FRAGMENT_SHADER);
        global_shader_program = glCreateProgram();
        glAttachShader(global_shader_program, vs);
        glAttachShader(global_shader_program, fs);
        glLinkProgram(global_shader_program);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    comp->shader_program = global_shader_program;
    float quad[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &comp->vao);
    glGenBuffers(1, &comp->vbo);
    glBindVertexArray(comp->vao);
    glBindBuffer(GL_ARRAY_BUFFER, comp->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    glGenFramebuffers(1, &comp->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
 
    glGenTextures(1, &comp->output_texture);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, comp->timeline->width, comp->timeline->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->output_texture, 0);
 
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "Framebuffer Error!\n");
     
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// --- Helper: Audio Resampling & Buffering ---

// 尝试从音频流中解码并填充缓冲区
// 返回: 解码是否成功/有数据
static bool decode_audio_chunk(ClipDecoder* dec) {
    if (dec->audio_stream_idx < 0) return false;

    // 如果缓冲区还有大量剩余数据，暂不解码
    if (dec->audio_buffer_size - dec->audio_buffer_index > 4096) return true;

    // 移动剩余数据到头部 (简单的内存移动，非 RingBuffer，基础版够用)
    if (dec->audio_buffer_index > 0 && dec->audio_buffer_size > dec->audio_buffer_index) {
        size_t remaining = dec->audio_buffer_size - dec->audio_buffer_index;
        memmove(dec->audio_buffer, dec->audio_buffer + dec->audio_buffer_index, remaining);
        dec->audio_buffer_size = remaining;
        dec->audio_buffer_index = 0;
    } else if (dec->audio_buffer_index >= dec->audio_buffer_size) {
        dec->audio_buffer_size = 0;
        dec->audio_buffer_index = 0;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool got_data = false;

    // 读取循环：直到读到一个音频包或出错
    while (av_read_frame(dec->fmt_ctx_audio, pkt) >= 0) {
        if (pkt->stream_index == dec->audio_stream_idx) {
            if (avcodec_send_packet(dec->dec_ctx_audio, pkt) == 0) {
                while (avcodec_receive_frame(dec->dec_ctx_audio, frame) == 0) {
                    // Resample
                    // 计算输出采样数
                    int out_samples = (int)av_rescale_rnd(
                        swr_get_delay(dec->swr_ctx, dec->dec_ctx_audio->sample_rate) + frame->nb_samples,
                        MIX_SAMPLE_RATE, dec->dec_ctx_audio->sample_rate, AV_ROUND_UP);

                    // 确保缓冲区够大
                    size_t required_bytes = out_samples * MIX_CHANNELS * sizeof(float);
                    if (dec->audio_buffer_size + required_bytes > dec->audio_buffer_cap) {
                        dec->audio_buffer_cap = (dec->audio_buffer_size + required_bytes) * 2;
                        dec->audio_buffer = reallocate(NULL, dec->audio_buffer, 0, dec->audio_buffer_cap); // 使用标准 realloc 或 vm realloc
                    }

                    u8* out_ptr = dec->audio_buffer + dec->audio_buffer_size;
                    int converted = swr_convert(dec->swr_ctx, 
                                                &out_ptr, out_samples,
                                                (const uint8_t**)frame->data, frame->nb_samples);
                    
                    if (converted > 0) {
                        dec->audio_buffer_size += converted * MIX_CHANNELS * sizeof(float);
                        got_data = true;
                    }
                }
            }
        }
        av_packet_unref(pkt);
        if (got_data) break; // 每次只解一帧，避免阻塞太久
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    return got_data;
}

// --- SDL Audio Callback ---
// 混音核心逻辑
static void audio_callback(void* userdata, Uint8* stream, int len) {
    Compositor* comp = (Compositor*)userdata;
    memset(stream, 0, len);
    
    if (!comp->running || !comp->timeline) return;

    if (SDL_TryLockMutex(comp->decoder_mutex) == 0) {
        float* out_buffer = (float*)stream;
        
        for (i32 i = 0; i < comp->decoder_count; i++) {
            ClipDecoder* dec = comp->decoders[i];
            
            // 只有活跃的解码器才发声
            if (!dec->active_this_frame) continue; 
            if (dec->audio_stream_idx < 0) continue;

            decode_audio_chunk(dec);

            size_t bytes_to_mix = len;
            size_t bytes_available = dec->audio_buffer_size - dec->audio_buffer_index;
            if (bytes_to_mix > bytes_available) bytes_to_mix = bytes_available;

            float* src = (float*)(dec->audio_buffer + dec->audio_buffer_index);
            
            // [新增] 获取音量
            // 直接从解码器引用的 ObjClip 中读取音量
            // 这体现了 "Timeline 只是容器，音量属于 Clip" 的设计
            float vol = (float)dec->clip_ref->volume;

            // [修改] 混音循环
            for (size_t j = 0; j < bytes_to_mix / sizeof(float); j++) {
                out_buffer[j] += src[j] * vol; // 应用音量
            }
            
            dec->audio_buffer_index += bytes_to_mix;
        }
        
        SDL_UnlockMutex(comp->decoder_mutex);
    }
}


// --- Decoder Management ---

static ClipDecoder* get_decoder(Compositor* comp, ObjClip* clip) {
    for (i32 i = 0; i < comp->decoder_count; i++) {
        if (comp->decoders[i]->clip_ref == clip) {
            comp->decoders[i]->active_this_frame = true;
            return comp->decoders[i];
        }
    }
    ClipDecoder* dec = create_decoder(comp->vm, clip);
    if (!dec) return NULL;
    if (comp->decoder_count >= comp->decoder_capacity) {
        i32 new_cap = comp->decoder_capacity == 0 ? 4 : comp->decoder_capacity * 2;
        comp->decoders = GROW_ARRAY(comp->vm, ClipDecoder*, comp->decoders, comp->decoder_capacity, new_cap);
        comp->decoder_capacity = new_cap;
    }
    dec->active_this_frame = true;
    comp->decoders[comp->decoder_count++] = dec;
    return dec;
}

static ClipDecoder* create_decoder(VM* vm, ObjClip* clip) {
    ClipDecoder* dec = ALLOCATE(vm, ClipDecoder, 1);
    memset(dec, 0, sizeof(ClipDecoder));
    dec->clip_ref = clip;

    if (avformat_open_input(&dec->fmt_ctx, clip->path->chars, NULL, NULL) < 0) { FREE(vm, ClipDecoder, dec); return NULL; }
    if (avformat_find_stream_info(dec->fmt_ctx, NULL) < 0) { FREE(vm, ClipDecoder, dec); return NULL; }
    
    dec->video_stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (dec->video_stream_idx < 0) { FREE(vm, ClipDecoder, dec); return NULL; }
    
    AVStream* stream = dec->fmt_ctx->streams[dec->video_stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    
    dec->dec_ctx = avcodec_alloc_context3(codec);
    vm->bytesAllocated += sizeof(AVCodecContext);
    avcodec_parameters_to_context(dec->dec_ctx, stream->codecpar);
    
    // [修复] 禁用多线程以避免竞态，或者设置为自动但小心使用
    // 为了稳定性，先设为 1
    dec->dec_ctx->thread_count = 1;

    // [修复] 禁用所有硬件加速，强制软件解码
    dec->hw_accel = false;
    dec->hw_device_ctx = NULL;
    dec->hw_frames_ctx = NULL;

    if (avcodec_open2(dec->dec_ctx, codec, NULL) < 0) { FREE(vm, ClipDecoder, dec); return NULL; }
    
    dec->raw_frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    
    // RGB Frame for texture upload
    dec->rgb_frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    
    // Allocate buffer for rgb_frame
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, dec->dec_ctx->width, dec->dec_ctx->height, 1);
    dec->rgb_buffer = (u8*)av_malloc(num_bytes);
    av_image_fill_arrays(dec->rgb_frame->data, dec->rgb_frame->linesize, dec->rgb_buffer, AV_PIX_FMT_RGBA, dec->dec_ctx->width, dec->dec_ctx->height, 1);

    dec->current_pts_sec = -1.0;
    
    // OpenGL Texture
    glGenTextures(1, &dec->texture);
    glBindTexture(GL_TEXTURE_2D, dec->texture);
    // 初始化空纹理
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dec->dec_ctx->width, dec->dec_ctx->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    dec->audio_stream_idx = -1;
    if (avformat_open_input(&dec->fmt_ctx_audio, clip->path->chars, NULL, NULL) == 0) {
        avformat_find_stream_info(dec->fmt_ctx_audio, NULL);
        dec->audio_stream_idx = av_find_best_stream(dec->fmt_ctx_audio, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
        
        if (dec->audio_stream_idx >= 0) {
            AVStream* stream = dec->fmt_ctx_audio->streams[dec->audio_stream_idx];
            const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (codec) {
                dec->dec_ctx_audio = avcodec_alloc_context3(codec);
                avcodec_parameters_to_context(dec->dec_ctx_audio, stream->codecpar);
                if (avcodec_open2(dec->dec_ctx_audio, codec, NULL) == 0) {
                    // [修复] FFmpeg 5.0+ / 8.0 API 适配
                    
                    // 1. 准备输出布局 (Stereo)
                    AVChannelLayout out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
                    
                    // 2. 使用 swr_alloc_set_opts2 初始化重采样器
                    // dec->swr_ctx 必须先设为 NULL，或者复用已有的
                    dec->swr_ctx = NULL; 
                    int swr_ret = swr_alloc_set_opts2(
                        &dec->swr_ctx,
                        &out_layout, MIX_FORMAT, MIX_SAMPLE_RATE,
                        &stream->codecpar->ch_layout,       // [Fix] 使用 ch_layout
                        stream->codecpar->format,
                        stream->codecpar->sample_rate,
                        0, NULL
                    );

                    if (swr_ret < 0 || swr_init(dec->swr_ctx) < 0) {
                        fprintf(stderr, "Failed to init SwrContext\n");
                        // 处理错误，例如设置 swr_ctx = NULL 防止后续崩溃
                        if (dec->swr_ctx) {
                            swr_free(&dec->swr_ctx);
                            dec->swr_ctx = NULL;
                        }
                    } else {
                        // 初始 Buffer
                        dec->audio_buffer_cap = 16384; // 16KB
                        dec->audio_buffer = ALLOCATE(vm, u8, dec->audio_buffer_cap);
                        
                        // [Fix] 标记 Clip 属性: channels -> ch_layout.nb_channels
                        clip->has_audio = true;
                        clip->audio_channels = stream->codecpar->ch_layout.nb_channels;
                        clip->audio_sample_rate = stream->codecpar->sample_rate;
                    }
                }
            }
        }
    }

    return dec;
}

static void free_decoder(VM* vm, ClipDecoder* dec) {
    if (!dec) return;
    
    glDeleteTextures(1, &dec->texture);
    
    if (dec->raw_frame) av_frame_free(&dec->raw_frame);
    if (dec->rgb_frame) av_frame_free(&dec->rgb_frame);
    if (dec->rgb_buffer) av_free(dec->rgb_buffer);
    if (dec->sws_ctx) sws_freeContext(dec->sws_ctx);
    
    if (dec->dec_ctx) avcodec_free_context(&dec->dec_ctx);
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);


    if (dec->audio_buffer) FREE_ARRAY(vm, u8, dec->audio_buffer, dec->audio_buffer_cap);
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    if (dec->dec_ctx_audio) avcodec_free_context(&dec->dec_ctx_audio);
    if (dec->fmt_ctx_audio) avformat_close_input(&dec->fmt_ctx_audio);
    
    FREE(vm, ClipDecoder, dec);
}

// 返回 0 表示解码成功，其他表示未就绪或结束
static i32 decode_frame_at_time(ClipDecoder* dec, double target_time, Compositor* comp) {
    UNUSED(comp);
    AVStream* stream = dec->fmt_ctx->streams[dec->video_stream_idx];
    double time_base = av_q2d(stream->time_base);
    double diff = target_time - dec->current_pts_sec;

    // Seek if needed
    if (diff < 0 || diff > 1.0) { // 阈值放宽一点
        i64 target_ts = (i64)(target_time / time_base);
        av_seek_frame(dec->fmt_ctx, dec->video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec->dec_ctx);
        dec->current_pts_sec = -1.0;
    }

    AVPacket* pkt = av_packet_alloc();
    i32 ret = -1;

    // 如果当前帧已经足够接近目标时间，就不解码新的了
    if (dec->current_pts_sec >= 0 && (target_time - dec->current_pts_sec) >= 0 && (target_time - dec->current_pts_sec) < 0.05) {
        av_packet_free(&pkt);
        return 0; // Current frame is good
    }

    bool frame_decoded = false;
    while (av_read_frame(dec->fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == dec->video_stream_idx) {
            if (avcodec_send_packet(dec->dec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec->dec_ctx, dec->raw_frame) == 0) {
                    double pts = dec->raw_frame->pts * time_base;
                    dec->current_pts_sec = pts;
                    
                    // Convert to RGB immediately
                    if (!dec->sws_ctx) {
                        dec->sws_ctx = sws_getContext(dec->dec_ctx->width, dec->dec_ctx->height, dec->dec_ctx->pix_fmt,
                                                      dec->dec_ctx->width, dec->dec_ctx->height, AV_PIX_FMT_RGBA,
                                                      SWS_BILINEAR, NULL, NULL, NULL);
                    }
                    sws_scale(dec->sws_ctx, (const u8* const*)dec->raw_frame->data, dec->raw_frame->linesize,
                              0, dec->dec_ctx->height, dec->rgb_frame->data, dec->rgb_frame->linesize);

                    if (pts >= target_time) {
                        frame_decoded = true;
                        goto done;
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

done:
    av_packet_free(&pkt);
    return frame_decoded ? 0 : -1;
}

// 预取线程函数：只负责解码到 CPU 内存 (rgb_buffer)
static int prefetch_thread_func(void* data) {
    Compositor* comp = (Compositor*)data;
    while (comp->running) {
        SDL_LockMutex(comp->decoder_mutex);
        for (i32 i = 0; i < comp->decoder_count; i++) {
            ClipDecoder* dec = comp->decoders[i];
            // 简单的预取逻辑：保持解码器活跃
            if (dec->active_this_frame) {
                // decode_frame_at_time 会将数据解码到 dec->rgb_buffer
                // 注意：这里不涉及 OpenGL 调用
                // decode_frame_at_time(dec, dec->current_pts_sec + 0.03, comp); 
                // 暂时注释掉预取，依靠主线程按需解码更稳定，避免 seek 冲突
            }
        }
        SDL_UnlockMutex(comp->decoder_mutex);
        SDL_Delay(10);
    }
    return 0;
}

// --- Lifecycle ---
Compositor* compositor_create(VM* vm, Timeline* timeline) {
    Compositor* comp = ALLOCATE(vm, Compositor, 1);
    memset(comp, 0, sizeof(Compositor));
    comp->vm = vm;
    comp->timeline = timeline;
    init_gl_resources(comp);
 
    comp->decoder_mutex = SDL_CreateMutex();
    comp->running = true;
    
    // [修复] 暂时不启动预取线程，简化调试
    // comp->prefetch_thread = SDL_CreateThread(prefetch_thread_func, "Prefetch", comp);
    comp->prefetch_thread = NULL;
 
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL Audio Init Failed: %s\n", SDL_GetError());
    } else {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof(want));
        want.freq = MIX_SAMPLE_RATE;
        want.format = SDL_MIX_FORMAT;
        want.channels = MIX_CHANNELS;
        want.samples = 1024;
        want.callback = audio_callback;
        want.userdata = comp;

        comp->audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0); // 不允许 fallback
        if (comp->audio_device == 0) {
            fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
        } else {
            SDL_PauseAudioDevice(comp->audio_device, 0); // Start playing (silence if no clips)
            comp->audio_enabled = true;
        }
    }
    
    return comp;
}

void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    comp->running = false;
    
    if (comp->prefetch_thread) {
        int ret;
        SDL_WaitThread(comp->prefetch_thread, &ret);
    }
    SDL_DestroyMutex(comp->decoder_mutex);

    
    for (i32 i=0; i<comp->decoder_count; i++) free_decoder(vm, comp->decoders[i]);
    if (comp->decoders) FREE_ARRAY(vm, ClipDecoder*, comp->decoders, comp->decoder_capacity);

    if (comp->audio_device) {
        SDL_CloseAudioDevice(comp->audio_device);
    }
    
    glDeleteVertexArrays(1, &comp->vao);
    glDeleteBuffers(1, &comp->vbo);
    glDeleteFramebuffers(1, &comp->fbo);
    glDeleteTextures(1, &comp->output_texture);
    glDeleteProgram(comp->shader_program);
    if (comp->cpu_output_buffer) reallocate(vm, comp->cpu_output_buffer, comp->timeline->width * comp->timeline->height * 4, 0);
    FREE(vm, Compositor, comp);
}

// --- 渲染函数 ---
void compositor_render(Compositor* comp, double time) {
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glViewport(0, 0, comp->timeline->width, comp->timeline->height);
 
    u8 r = comp->timeline->background_color.r;
    u8 g = comp->timeline->background_color.g;
    u8 b = comp->timeline->background_color.b;
    glClearColor(r/255.0f, g/255.0f, b/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
 
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
 
    mat4 proj = mat4_ortho(0.0f, (float)comp->timeline->width,
                           (float)comp->timeline->height, 0.0f,
                           -1.0f, 1.0f);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_projection"), 1, GL_FALSE, proj.m);
    
    SDL_LockMutex(comp->decoder_mutex);
    
    TimelineClip* clips[100]; 
    i32 clip_count = 0;
    for (i32 i = 0; i < (i32)comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if ((track->flags & 1) == 0) continue;
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (tc) clips[clip_count++] = tc;
    }
    
    // Z-Index Sorting
    for (i32 i = 0; i < clip_count - 1; i++) {
        for (i32 j = i + 1; j < clip_count; j++) {
            if (clips[i]->transform.z_index > clips[j]->transform.z_index) {
                TimelineClip* temp = clips[i];
                clips[i] = clips[j];
                clips[j] = temp;
            }
        }
    }
    
    for (i32 i = 0; i < clip_count; i++) {
        TimelineClip* tc = clips[i];
        ClipDecoder* dec = get_decoder(comp, tc->media);
        if (dec) {
            double source_time = tc->source_in + (time - tc->timeline_start);
            
            // 尝试解码 (此时在主线程，安全)
            decode_frame_at_time(dec, source_time, comp);
            
            // 上传纹理
            if (dec->rgb_buffer) {
                glBindTexture(GL_TEXTURE_2D, dec->texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // 安全设置
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dec->dec_ctx->width, dec->dec_ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, dec->rgb_buffer);
            }

            // 计算逻辑坐标 (Top-Left Origin)
            int x = (int)tc->transform.x;
            int y = (int)tc->transform.y;
            int w = (int)(dec->dec_ctx->width * tc->transform.scale_x);
            int h = (int)(dec->dec_ctx->height * tc->transform.scale_y);
            
            // [关键修复] 计算 Scissor Y (Bottom-Left Origin)
            // 逻辑 Y 是从顶部开始的，OpenGL Scissor Y 是从底部开始的矩形左下角
            int scissor_y = comp->timeline->height - (y + h);
            
            glScissor(x, scissor_y, w, h);
            glEnable(GL_SCISSOR_TEST);
            
            draw_clip_rect(comp, dec, tc);
            
            glDisable(GL_SCISSOR_TEST);
        }
    }
    
    SDL_UnlockMutex(comp->decoder_mutex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    comp->cpu_buffer_stale = true;
}

void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height) {
    static GLuint blit_program = 0;
    if (blit_program == 0) {
        GLuint vs = compile_shader(VS_SCREEN, GL_VERTEX_SHADER);
        GLuint fs = compile_shader(FS_SCREEN, GL_FRAGMENT_SHADER);
        blit_program = glCreateProgram();
        glAttachShader(blit_program, vs);
        glAttachShader(blit_program, fs);
        glLinkProgram(blit_program);
    }
 
    glViewport(0, 0, window_width, window_height);
    glClear(GL_COLOR_BUFFER_BIT);
 
    glUseProgram(blit_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glUniform1i(glGetUniformLocation(blit_program, "screenTexture"), 0);
 
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void flip_buffer_vertical(u8* buffer, i32 width, i32 height) {
    i32 stride = width * 4;
    u8* row_buf = malloc(stride);
    for (i32 y = 0; y < height / 2; y++) {
        u8* top = buffer + y * stride;
        u8* bot = buffer + (height - 1 - y) * stride;
        memcpy(row_buf, top, stride);
        memcpy(top, bot, stride);
        memcpy(bot, row_buf, stride);
    }
    free(row_buf);
}

u8* compositor_get_cpu_buffer(Compositor* comp) {
    if (comp->cpu_buffer_stale) {
        size_t size = comp->timeline->width * comp->timeline->height * 4;
        if (!comp->cpu_output_buffer) {
            comp->cpu_output_buffer = reallocate(comp->vm, NULL, 0, size);
        }
     
        glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
        glReadPixels(0, 0, comp->timeline->width, comp->timeline->height, GL_RGBA, GL_UNSIGNED_BYTE, comp->cpu_output_buffer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
     
        flip_buffer_vertical(comp->cpu_output_buffer, comp->timeline->width, comp->timeline->height);
     
        comp->cpu_buffer_stale = false;
    }
    return comp->cpu_output_buffer;
}

static void draw_clip_rect(Compositor* comp, ClipDecoder* dec, TimelineClip* tc) {
    float vw = (float)dec->dec_ctx->width;
    float vh = (float)dec->dec_ctx->height;
    float sx = tc->transform.scale_x * vw;
    float sy = tc->transform.scale_y * vh;
    
    mat4 model = mat4_translate_scale(tc->transform.x, tc->transform.y, sx, sy);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_model"), 1, GL_FALSE, model.m);
    glUniform1f(glGetUniformLocation(comp->shader_program, "u_opacity"), tc->transform.opacity);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, dec->texture);
    // [修改] 采样器现在是 RGB
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_rgb"), 0);
    
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}