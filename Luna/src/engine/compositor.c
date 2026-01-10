// src/engine/compositor.c

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include "compositor.h"
#include "core/memory.h"
#include "core/vm/vm.h"

#define MAX_QUEUE_SIZE 8
#define AUDIO_RB_SIZE 131072
#define MIX_SAMPLE_RATE 44100

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

// [Fix] 正确的 YUV 转 RGB Shader
const char* FS_SOURCE_YUV =
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D tex_y;\n"
"uniform sampler2D tex_u;\n"
"uniform sampler2D tex_v;\n"
"uniform float u_opacity;\n"
"void main() {\n"
" float y = texture(tex_y, TexCoord).r;\n"
" float u = texture(tex_u, TexCoord).r - 0.5;\n"
" float v = texture(tex_v, TexCoord).r - 0.5;\n"
" float r = y + 1.402 * v;\n"
" float g = y - 0.344136 * u - 0.714136 * v;\n"
" float b = y + 1.772 * u;\n"
" FragColor = vec4(r, g, b, u_opacity);\n"
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

static mat4 mat4_translate_scale(float x, float y, float sx, float sy) {
    mat4 res = {0};
    res.m[0] = sx; res.m[5] = sy; res.m[10] = 1.0f; res.m[15] = 1.0f;
    res.m[12] = x; res.m[13] = y;
    return res;
}

static GLuint compile_shader(const char* src, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    i32 success; char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) { glGetShaderInfoLog(shader, 512, NULL, infoLog); fprintf(stderr, "Shader Err: %s\n", infoLog); }
    return shader;
}

// --- Frame Queue Helpers ---

static void fq_push(FrameQueue* q, AVFrame* frame, double pts) {
    DecodedFrame* node = (DecodedFrame*)malloc(sizeof(DecodedFrame));
    node->frame = frame;
    node->pts = pts;
    node->next = NULL;
    if (q->tail) q->tail->next = node;
    else q->head = node;
    q->tail = node;
    q->count++;
}

static AVFrame* fq_pop(FrameQueue* q, double* out_pts) {
    if (!q->head) return NULL;
    DecodedFrame* node = q->head;
    AVFrame* frame = node->frame;
    if (out_pts) *out_pts = node->pts;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    free(node);
    return frame;
}

static void fq_clear(FrameQueue* q) {
    while (q->head) {
        AVFrame* f = fq_pop(q, NULL);
        av_frame_free(&f);
    }
}

// --- Decoder Thread ---

static int decoder_thread_func(void* data) {
    ClipDecoder* dec = (ClipDecoder*)data;
    
    dec->fmt_ctx = avformat_alloc_context();
    if (avformat_open_input(&dec->fmt_ctx, dec->file_path_copy, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(dec->fmt_ctx, NULL);

    dec->video_stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (dec->video_stream_idx >= 0) {
        AVStream* st = dec->fmt_ctx->streams[dec->video_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        dec->vid_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec->vid_ctx, st->codecpar);
        if (avcodec_open2(dec->vid_ctx, codec, NULL) < 0) dec->vid_ctx = NULL;
    }

    dec->audio_stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (dec->audio_stream_idx >= 0) {
        AVStream* st = dec->fmt_ctx->streams[dec->audio_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        dec->aud_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec->aud_ctx, st->codecpar);
        if (avcodec_open2(dec->aud_ctx, codec, NULL) == 0) {
            AVChannelLayout out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
            swr_alloc_set_opts2(&dec->swr_ctx, &out_layout, AV_SAMPLE_FMT_FLT, MIX_SAMPLE_RATE,
                &st->codecpar->ch_layout, st->codecpar->format, st->codecpar->sample_rate, 0, NULL);
            swr_init(dec->swr_ctx);
        } else {
            dec->aud_ctx = NULL; // Failed
        }
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (dec->thread_running) {
        bool seeking = false;
        double seek_tgt = 0;
        
        SDL_LockMutex(dec->mutex);
        if (dec->seek_requested) {
            seeking = true;
            seek_tgt = dec->seek_target_time;
            dec->seek_requested = false;
            fq_clear(&dec->video_queue);
            dec->rb_head = dec->rb_tail = dec->rb_count = 0;
        }
        SDL_UnlockMutex(dec->mutex);

        if (seeking) {
            int64_t ts = (int64_t)(seek_tgt * AV_TIME_BASE);
            av_seek_frame(dec->fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
            if (dec->vid_ctx) avcodec_flush_buffers(dec->vid_ctx);
            if (dec->aud_ctx) avcodec_flush_buffers(dec->aud_ctx);
        }

        SDL_LockMutex(dec->mutex);
        bool queue_full = (dec->video_queue.count >= MAX_QUEUE_SIZE);
        SDL_UnlockMutex(dec->mutex);

        if (queue_full) {
            SDL_LockMutex(dec->mutex);
            SDL_CondWaitTimeout(dec->cond_can_produce, dec->mutex, 20);
            SDL_UnlockMutex(dec->mutex);
            continue;
        }

        int ret = av_read_frame(dec->fmt_ctx, pkt);
        if (ret < 0) {
            SDL_Delay(10); 
            continue;
        }

        if (pkt->stream_index == dec->video_stream_idx && dec->vid_ctx) {
            if (avcodec_send_packet(dec->vid_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec->vid_ctx, frame) == 0) {
                    AVFrame* cloned = av_frame_alloc();
                    av_frame_ref(cloned, frame);
                    
                    int64_t pts_val = frame->best_effort_timestamp;
                    if (pts_val == AV_NOPTS_VALUE) pts_val = frame->pts;
                    if (pts_val == AV_NOPTS_VALUE) pts_val = frame->pkt_dts;
                    
                    // [Fix] 捕获起始 PTS
                    SDL_LockMutex(dec->mutex);
                    if (!dec->has_start_pts && pts_val != AV_NOPTS_VALUE) {
                        dec->start_pts = pts_val;
                        dec->has_start_pts = true;
                    }
                    
                    double pts = 0.0;
                    if (pts_val != AV_NOPTS_VALUE && dec->has_start_pts) {
                        pts = (pts_val - dec->start_pts) * av_q2d(dec->fmt_ctx->streams[dec->video_stream_idx]->time_base);
                    } else if (dec->video_queue.tail) {
                        pts = dec->video_queue.tail->pts + 0.033;
                    }
                    if (pts < 0) pts = 0;
                    
                    fq_push(&dec->video_queue, cloned, pts);
                    SDL_UnlockMutex(dec->mutex);
                }
            }
        }
        else if (pkt->stream_index == dec->audio_stream_idx && dec->aud_ctx && dec->swr_ctx) {
             if (avcodec_send_packet(dec->aud_ctx, pkt) == 0) {
                 while (avcodec_receive_frame(dec->aud_ctx, frame) == 0) {
                     uint8_t* out_data[2] = {0};
                     int out_samples = av_rescale_rnd(swr_get_delay(dec->swr_ctx, dec->aud_ctx->sample_rate) + frame->nb_samples,
                                                      MIX_SAMPLE_RATE, dec->aud_ctx->sample_rate, AV_ROUND_UP);
                     av_samples_alloc(out_data, NULL, 2, out_samples, AV_SAMPLE_FMT_FLT, 0);
                     int len = swr_convert(dec->swr_ctx, out_data, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
                     
                     if (len > 0) {
                         SDL_LockMutex(dec->mutex);
                         int floats_to_write = len * 2;
                         int available = dec->rb_capacity - dec->rb_count;
                         if (available >= floats_to_write) {
                             float* raw = (float*)out_data[0];
                             for (int i=0; i<floats_to_write; i++) {
                                 dec->audio_ring_buffer[dec->rb_head] = raw[i];
                                 dec->rb_head = (dec->rb_head + 1) % dec->rb_capacity;
                             }
                             dec->rb_count += floats_to_write;
                         }
                         SDL_UnlockMutex(dec->mutex);
                     }
                     av_freep(&out_data[0]);
                 }
             }
        }
        av_packet_unref(pkt);
    }
    
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (dec->vid_ctx) avcodec_free_context(&dec->vid_ctx);
    if (dec->aud_ctx) avcodec_free_context(&dec->aud_ctx);
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    avformat_close_input(&dec->fmt_ctx);
    return 0;
}

// --- Decoder Management ---

static ClipDecoder* create_decoder(VM* vm, ObjClip* clip) {
    UNUSED(vm);
    ClipDecoder* dec = (ClipDecoder*)malloc(sizeof(ClipDecoder));
    memset(dec, 0, sizeof(ClipDecoder));
    dec->start_pts = 0;
    dec->has_start_pts = false;
    dec->clip_ref = clip;
    dec->file_path_copy = strdup(clip->path->chars);
    dec->mutex = SDL_CreateMutex();
    dec->cond_can_produce = SDL_CreateCond();
    
    dec->rb_capacity = AUDIO_RB_SIZE;
    dec->audio_ring_buffer = (float*)malloc(sizeof(float) * dec->rb_capacity);
    
    glGenTextures(1, &dec->tex_y);
    glGenTextures(1, &dec->tex_u);
    glGenTextures(1, &dec->tex_v);
    
    GLenum params[] = {dec->tex_y, dec->tex_u, dec->tex_v};
    for(int i=0; i<3; i++) {
        glBindTexture(GL_TEXTURE_2D, params[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // [Fix] 确保纹理上传对齐，防止倾斜
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    dec->thread_running = true;
    dec->thread = SDL_CreateThread(decoder_thread_func, "DecoderThread", dec);
    
    return dec;
}

static void free_decoder(VM* vm, ClipDecoder* dec) {
    UNUSED(vm);
    if (!dec) return;
    
    dec->thread_running = false;
    SDL_CondSignal(dec->cond_can_produce);
    SDL_WaitThread(dec->thread, NULL);
    
    SDL_DestroyMutex(dec->mutex);
    SDL_DestroyCond(dec->cond_can_produce);
    
    glDeleteTextures(1, &dec->tex_y);
    glDeleteTextures(1, &dec->tex_u);
    glDeleteTextures(1, &dec->tex_v);
    
    fq_clear(&dec->video_queue);
    free(dec->audio_ring_buffer);
    free(dec->file_path_copy);
    free(dec);
}

// --- Audio Callback ---

static void audio_callback(void* userdata, Uint8* stream, int len) {
    Compositor* comp = (Compositor*)userdata;
    memset(stream, 0, len); 
    
    if (!comp->timeline) return;
    
    if (SDL_TryLockMutex(comp->mix_mutex) == 0) {
        float* out = (float*)stream;
        int needed = len / sizeof(float);
        
        for (int i=0; i<comp->decoder_count; i++) {
            ClipDecoder* dec = comp->decoders[i];
            if (SDL_TryLockMutex(dec->mutex) == 0) {
                if (dec->active_this_frame && dec->rb_count > 0) {
                    int read_amt = (dec->rb_count > needed) ? needed : dec->rb_count;
                    float vol = dec->current_volume;
                    for (int k=0; k<read_amt; k++) {
                        out[k] += dec->audio_ring_buffer[dec->rb_tail] * vol;
                        dec->rb_tail = (dec->rb_tail + 1) % dec->rb_capacity;
                    }
                    dec->rb_count -= read_amt;
                }
                SDL_UnlockMutex(dec->mutex);
            }
        }
        SDL_UnlockMutex(comp->mix_mutex);
    }
}

// --- Render Logic ---

static void update_decoder_video(ClipDecoder* dec, double timeline_time) {
    SDL_LockMutex(dec->mutex);
    
    double diff = timeline_time - dec->current_pts;
    if (diff < -0.1 || diff > 1.0) {
        dec->seek_requested = true;
        dec->seek_target_time = timeline_time;
        dec->current_pts = timeline_time;
        SDL_CondSignal(dec->cond_can_produce);
        SDL_UnlockMutex(dec->mutex);
        return;
    }
    
    AVFrame* best_frame = NULL;
    while (dec->video_queue.head) {
        double f_pts = dec->video_queue.head->pts;
        if (f_pts < timeline_time - 0.05) {
            AVFrame* drop = fq_pop(&dec->video_queue, NULL);
            av_frame_free(&drop);
            SDL_CondSignal(dec->cond_can_produce);
        } else if (f_pts <= timeline_time + 0.05) {
            if (best_frame) av_frame_free(&best_frame);
            best_frame = fq_pop(&dec->video_queue, &dec->current_pts);
            SDL_CondSignal(dec->cond_can_produce);
        } else {
            break;
        }
    }
    SDL_UnlockMutex(dec->mutex);

    if (best_frame) {
        if (dec->clip_ref->width == 0 || dec->clip_ref->height == 0) {
            dec->clip_ref->width = best_frame->width;
            dec->clip_ref->height = best_frame->height;
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, dec->tex_y);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, best_frame->linesize[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, best_frame->width, best_frame->height, 
                     0, GL_RED, GL_UNSIGNED_BYTE, best_frame->data[0]);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, dec->tex_u);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, best_frame->linesize[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, best_frame->width/2, best_frame->height/2, 
                     0, GL_RED, GL_UNSIGNED_BYTE, best_frame->data[1]);
        
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, dec->tex_v);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, best_frame->linesize[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, best_frame->width/2, best_frame->height/2, 
                     0, GL_RED, GL_UNSIGNED_BYTE, best_frame->data[2]);
        
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); 
        
        dec->texture_ready = true;
        av_frame_free(&best_frame);
    }
}

static void draw_clip_rect(Compositor* comp, ClipDecoder* dec, TimelineClip* tc) {
    if (!dec->texture_ready) return;
    
    glUseProgram(comp->shader_program);
    
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_y"), 0);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_u"), 1);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_v"), 2);
    
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, dec->tex_y);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, dec->tex_u);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, dec->tex_v);
    
    // [Fix] 安全检查：防止未初始化或错误的 transform 数据
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float opacity = tc->transform.opacity;

    // 防止 Scale 为 0 (不可见)
    if (fabsf(scale_x) < 0.001f) scale_x = 1.0f;
    if (fabsf(scale_y) < 0.001f) scale_y = 1.0f;
    // 允许透明度为0，但如果出现极小浮点误差则归一
    if (opacity < 0.001f && opacity > -0.001f) opacity = 1.0f;

    float w = (float)tc->media->width * scale_x;
    float h = (float)tc->media->height * scale_y;
    
    if (w < 1.0f || h < 1.0f) return;

    // 使用传入的真实坐标
    mat4 model = mat4_translate_scale(tc->transform.x, tc->transform.y, w, h);
    
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_model"), 1, GL_FALSE, model.m);
    glUniform1f(glGetUniformLocation(comp->shader_program, "u_opacity"), opacity);
    
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// --- Compositor Lifecycle ---

Compositor* compositor_create(VM* vm, Timeline* timeline) {
    Compositor* comp = ALLOCATE(vm, Compositor, 1);
    memset(comp, 0, sizeof(Compositor));
    comp->vm = vm;
    comp->timeline = timeline;
    comp->mix_mutex = SDL_CreateMutex();
    
    GLuint vs = compile_shader(VS_SOURCE, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(FS_SOURCE_YUV, GL_FRAGMENT_SHADER);
    comp->shader_program = glCreateProgram();
    glAttachShader(comp->shader_program, vs);
    glAttachShader(comp->shader_program, fs);
    glLinkProgram(comp->shader_program);
    // 可选：检查 Link 错误，但移除 verbose 输出
    glDeleteShader(vs); glDeleteShader(fs);
    
    float quad[] = { 0,0,0,0, 1,0,1,0, 0,1,0,1, 0,1,0,1, 1,0,1,0, 1,1,1,1 };
    glGenVertexArrays(1, &comp->vao);
    glGenBuffers(1, &comp->vbo);
    glBindVertexArray(comp->vao);
    glBindBuffer(GL_ARRAY_BUFFER, comp->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)8);
    
    glGenFramebuffers(1, &comp->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    
    glGenTextures(1, &comp->output_texture);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, timeline->width, timeline->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->output_texture, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer Error!\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (SDL_Init(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = {0}, have;
        want.freq = MIX_SAMPLE_RATE;
        want.format = AUDIO_F32;
        want.channels = 2;
        want.samples = 1024;
        want.callback = audio_callback;
        want.userdata = comp;
        comp->audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if(comp->audio_device) SDL_PauseAudioDevice(comp->audio_device, 0);
    }
    return comp;
}

void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    if (comp->audio_device) SDL_CloseAudioDevice(comp->audio_device);
    
    for(int i=0; i<comp->decoder_count; i++) free_decoder(vm, comp->decoders[i]);
    if(comp->decoders) FREE_ARRAY(vm, ClipDecoder*, comp->decoders, comp->decoder_capacity);
    
    glDeleteProgram(comp->shader_program);
    glDeleteFramebuffers(1, &comp->fbo);
    glDeleteTextures(1, &comp->output_texture);
    glDeleteBuffers(1, &comp->vbo);
    glDeleteVertexArrays(1, &comp->vao);
    SDL_DestroyMutex(comp->mix_mutex);
    FREE(vm, Compositor, comp);
}

static ClipDecoder* get_decoder_safe(Compositor* comp, ObjClip* clip) {
    for(int i=0; i<comp->decoder_count; i++) {
        if (comp->decoders[i]->clip_ref == clip) return comp->decoders[i];
    }
    ClipDecoder* dec = create_decoder(comp->vm, clip);
    if (comp->decoder_count >= comp->decoder_capacity) {
        int old = comp->decoder_capacity;
        comp->decoder_capacity = old < 8 ? 8 : old * 2;
        comp->decoders = GROW_ARRAY(comp->vm, ClipDecoder*, comp->decoders, old, comp->decoder_capacity);
    }
    comp->decoders[comp->decoder_count++] = dec;
    return dec;
}

void compositor_render(Compositor* comp, double time) {
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glViewport(0, 0, comp->timeline->width, comp->timeline->height);
    
    // [Fix] 恢复 Timeline 设定的背景色
    u8 r = comp->timeline->background_color.r;
    u8 g = comp->timeline->background_color.g;
    u8 b = comp->timeline->background_color.b;
    glClearColor(r/255.f, g/255.f, b/255.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    mat4 proj = mat4_ortho(0, comp->timeline->width, comp->timeline->height, 0, -1, 1);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_projection"), 1, GL_FALSE, proj.m);
    
    SDL_LockMutex(comp->mix_mutex); 
    
    for(int i=0; i<comp->decoder_count; i++) comp->decoders[i]->active_this_frame = false;
    
    for(int i=0; i<comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        
        ClipDecoder* dec = get_decoder_safe(comp, tc->media);
        dec->active_this_frame = true;
        dec->current_volume = (float)tc->media->volume;
        
        double clip_time = (time - tc->timeline_start) + tc->source_in;
        update_decoder_video(dec, clip_time);
        draw_clip_rect(comp, dec, tc);
    }
    
    SDL_UnlockMutex(comp->mix_mutex);
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
        glDeleteShader(vs); glDeleteShader(fs);
    }

    glViewport(0, 0, window_width, window_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // 屏幕外围显示黑色
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