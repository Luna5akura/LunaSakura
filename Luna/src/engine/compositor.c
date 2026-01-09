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


#define RING_BUFFER_SIZE 131072 // 128k samples (~1.5s stereo @ 44.1k)
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


static void rb_clear(ClipDecoder* dec) {
    dec->rb_head = 0;
    dec->rb_tail = 0;
    dec->rb_count = 0;
}

// 写入数据 (生产者)
static void rb_write(ClipDecoder* dec, float* data, i32 count) {
    for (i32 i = 0; i < count; i++) {
        dec->audio_ring_buffer[dec->rb_head] = data[i];
        dec->rb_head = (dec->rb_head + 1) % dec->rb_capacity;
    }
    dec->rb_count += count;
    if (dec->rb_count > dec->rb_capacity) dec->rb_count = dec->rb_capacity; // Should not happen if checked
}

// 读取数据 (消费者)
static void rb_read_mix(ClipDecoder* dec, float* out_buffer, i32 count, float volume) {
    for (i32 i = 0; i < count; i++) {
        float sample = dec->audio_ring_buffer[dec->rb_tail];
        dec->rb_tail = (dec->rb_tail + 1) % dec->rb_capacity;
        
        // [混音核心]：叠加 (+=) 并应用音量 (* volume)
        out_buffer[i] += sample * volume;
    }
    dec->rb_count -= count;
}



// --- SDL Audio Callback ---
// 混音核心逻辑
static void audio_callback(void* userdata, Uint8* stream, int len) {
    Compositor* comp = (Compositor*)userdata;
    // 1. 初始化静音 (这是必须的，因为我们要用 += 进行叠加)
    memset(stream, 0, len);
    
    if (!comp->running || !comp->timeline) return;

    // 锁定数据，防止主线程同时写入 Ring Buffer
    if (SDL_TryLockMutex(comp->decoder_mutex) == 0) {
        float* out_buffer = (float*)stream;
        int total_floats_needed = len / sizeof(float);
        
        for (i32 i = 0; i < comp->decoder_count; i++) {
            ClipDecoder* dec = comp->decoders[i];
            
            // 只有活跃的 Decoder 参与混音
            if (!dec->active_this_frame || dec->audio_stream_idx < 0) continue;

            int available = dec->rb_count;
            if (available <= 0) continue; // 无数据，跳过

            // 计算实际能读取多少
            int to_read = (available > total_floats_needed) ? total_floats_needed : available;

            // 获取音量：这里我们使用传进来的 snapshot volume
            float vol = dec->current_volume;

            // 从 Ring Buffer 读取并叠加到 SDL 流
            rb_read_mix(dec, out_buffer, to_read, vol);
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
    dec->current_volume = 1.0f;
    dec->last_render_time = -100.0;

    // --- Video Init (简化，保持原有逻辑) ---
    if (avformat_open_input(&dec->fmt_ctx, clip->path->chars, NULL, NULL) < 0) { /* handle error */ }
    avformat_find_stream_info(dec->fmt_ctx, NULL);
    dec->video_stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    // ... (Video Codec Init) ...
    AVStream* v_stream = dec->fmt_ctx->streams[dec->video_stream_idx];
    const AVCodec* v_codec = avcodec_find_decoder(v_stream->codecpar->codec_id);
    dec->dec_ctx = avcodec_alloc_context3(v_codec);
    avcodec_parameters_to_context(dec->dec_ctx, v_stream->codecpar);
    dec->dec_ctx->thread_count = 1; // 单线程以稳定
    avcodec_open2(dec->dec_ctx, v_codec, NULL);
    
    dec->raw_frame = av_frame_alloc();
    dec->rgb_frame = av_frame_alloc();
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, dec->dec_ctx->width, dec->dec_ctx->height, 1);
    dec->rgb_buffer = (u8*)av_malloc(num_bytes);
    av_image_fill_arrays(dec->rgb_frame->data, dec->rgb_frame->linesize, dec->rgb_buffer, AV_PIX_FMT_RGBA, dec->dec_ctx->width, dec->dec_ctx->height, 1);
    
    glGenTextures(1, &dec->texture);
    glBindTexture(GL_TEXTURE_2D, dec->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dec->dec_ctx->width, dec->dec_ctx->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // --- Audio Init (新逻辑) ---
    dec->audio_stream_idx = -1;
    // 打开独立的 Audio Format Context
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
                    
                    // Init Resampler (To Stereo Float)
                    AVChannelLayout out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
                    dec->swr_ctx = NULL; 
                    swr_alloc_set_opts2(
                        &dec->swr_ctx,
                        &out_layout, AV_SAMPLE_FMT_FLT, MIX_SAMPLE_RATE,
                        &stream->codecpar->ch_layout,
                        stream->codecpar->format,
                        stream->codecpar->sample_rate,
                        0, NULL
                    );
                    swr_init(dec->swr_ctx);

                    // Init Ring Buffer
                    dec->rb_capacity = RING_BUFFER_SIZE;
                    dec->audio_ring_buffer = ALLOCATE(vm, float, dec->rb_capacity);
                    rb_clear(dec);
                    
                    clip->has_audio = true;
                    clip->audio_channels = stream->codecpar->ch_layout.nb_channels;
                    clip->audio_sample_rate = stream->codecpar->sample_rate;
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


    if (dec->audio_ring_buffer) FREE_ARRAY(vm, float, dec->audio_ring_buffer, dec->rb_capacity);
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    if (dec->dec_ctx_audio) avcodec_free_context(&dec->dec_ctx_audio);
    if (dec->fmt_ctx_audio) avformat_close_input(&dec->fmt_ctx_audio);
    
    FREE(vm, ClipDecoder, dec);
}

static void pump_audio_data(ClipDecoder* dec, double target_time, bool did_seek) {
    if (dec->audio_stream_idx < 0) return;

    // 1. 如果发生 Seek，或者缓冲区时间偏差太大，需要清空并跳转 FFmpeg
    // 注意：这里我们简单地利用 did_seek 标志
    if (did_seek) {
        rb_clear(dec);
        
        AVStream* stream = dec->fmt_ctx_audio->streams[dec->audio_stream_idx];
        i64 target_ts = (i64)(target_time / av_q2d(stream->time_base));
        
        // 向后 Seek 关键帧
        av_seek_frame(dec->fmt_ctx_audio, dec->audio_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec->dec_ctx_audio);
    }

    // 2. 填充缓冲区
    // 我们希望缓冲区保持在 75% 左右的满度，留一点余量
    // 每次 pump 最多读取几个包，防止阻塞 UI 线程太久
    int max_reads = 5; 
    
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    
    // 当缓冲区还有空间 (> 4096 floats) 且没读太多包时循环
    while ((dec->rb_capacity - dec->rb_count) > 8192 && max_reads > 0) {
        int ret = av_read_frame(dec->fmt_ctx_audio, pkt);
        if (ret < 0) break; // 文件结束或错误

        if (pkt->stream_index == dec->audio_stream_idx) {
            if (avcodec_send_packet(dec->dec_ctx_audio, pkt) == 0) {
                while (avcodec_receive_frame(dec->dec_ctx_audio, frame) == 0) {
                    
                    // 重采样计算
                    int out_samples = (int)av_rescale_rnd(
                        swr_get_delay(dec->swr_ctx, dec->dec_ctx_audio->sample_rate) + frame->nb_samples,
                        MIX_SAMPLE_RATE, dec->dec_ctx_audio->sample_rate, AV_ROUND_UP);

                    // 临时缓冲区 (Stereo Float)
                    float* convert_buf = NULL;
                    int linesize = 0;
                    av_samples_alloc((uint8_t**)&convert_buf, &linesize, 2, out_samples, AV_SAMPLE_FMT_FLT, 0);

                    // 执行转换
                    int converted = swr_convert(dec->swr_ctx, 
                                                (uint8_t**)&convert_buf, out_samples,
                                                (const uint8_t**)frame->data, frame->nb_samples);
                    
                    if (converted > 0) {
                        int total_floats = converted * 2; // L+R
                        // 只有空间足够才写入
                        if (dec->rb_capacity - dec->rb_count >= total_floats) {
                            rb_write(dec, convert_buf, total_floats);
                        }
                    }
                    
                    if (convert_buf) av_freep(&convert_buf);
                }
            }
            max_reads--; // 成功处理一个音频包
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
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

    mat4 proj = mat4_ortho(0.0f, (float)comp->timeline->width, (float)comp->timeline->height, 0.0f, -1.0f, 1.0f);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_projection"), 1, GL_FALSE, proj.m);

    // 锁定以保护 Ring Buffer 和 Decoder 状态
    SDL_LockMutex(comp->decoder_mutex);
    
    // 1. 重置所有解码器活跃状态
    for (i32 i = 0; i < comp->decoder_count; i++) {
        comp->decoders[i]->active_this_frame = false;
    }

    // 2. 获取当前时间点的 Clips
    TimelineClip* clips[100]; 
    i32 clip_count = 0;
    for (i32 i = 0; i < (i32)comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if ((track->flags & 1) == 0) continue;
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (tc) clips[clip_count++] = tc;
    }
    
    // Z-Sort
    for (i32 i = 0; i < clip_count - 1; i++) {
        for (i32 j = i + 1; j < clip_count; j++) {
            if (clips[i]->transform.z_index > clips[j]->transform.z_index) {
                TimelineClip* temp = clips[i];
                clips[i] = clips[j];
                clips[j] = temp;
            }
        }
    }
    
    // 3. 处理每个 Clip
    for (i32 i = 0; i < clip_count; i++) {
        TimelineClip* tc = clips[i];
        ClipDecoder* dec = get_decoder(comp, tc->media);
        if (dec) {
            dec->active_this_frame = true;
            
            // [修复] 从 ObjClip (tc->media) 获取音量，而不是 transform
            // 解码器结构体里的 current_volume 只是一个快照，用于传给音频线程
            dec->current_volume = (float)tc->media->volume;

            // 计算 Clip 内部时间
            double clip_relative_time = (time - tc->timeline_start) + tc->source_in;
            
            // [Seek Detection]
            // 如果时间跳变 > 0.2s 或倒流，视为 Seek
            bool did_seek = false;
            double time_delta = clip_relative_time - dec->last_render_time;
            if (time_delta < 0 || time_delta > 0.2) {
                did_seek = true;
            }
            dec->last_render_time = clip_relative_time;

            // [Audio Pumping]
            // 主线程负责解码音频并填入 Ring Buffer
            if (tc->media->has_audio) {
                pump_audio_data(dec, clip_relative_time, did_seek);
            }

            // [Video Decoding]
            decode_frame_at_time(dec, clip_relative_time, comp);
            
            // [Draw]
            if (dec->rgb_buffer) {
                glBindTexture(GL_TEXTURE_2D, dec->texture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dec->dec_ctx->width, dec->dec_ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, dec->rgb_buffer);
            }

            int x = (int)tc->transform.x;
            int y = (int)tc->transform.y;
            int w = (int)(dec->dec_ctx->width * tc->transform.scale_x);
            int h = (int)(dec->dec_ctx->height * tc->transform.scale_y);
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