// src/engine/compositor.c

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
// 1. 包含 GLAD
#include <glad/glad.h>
// 2. 移除重复宏定义（glad 已处理）
// #define __gl_h_ 1 ...
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_vaapi.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <va/va.h>
#include <va/va_glx.h>
#include <X11/Xlib.h>
#include "compositor.h"
#include "core/memory.h"
#include "core/vm/vm.h"
// --- [修复] 手动定义扩展函数指针 ---
// 防止链接器找不到符号
typedef void (*PFN_glEGLImageTargetTexture2D)(GLenum target, void* image);
static PFN_glEGLImageTargetTexture2D my_glEGLImageTargetTexture2D = NULL;
// --- Forward Declarations ---
static ClipDecoder* create_decoder(VM* vm, ObjClip* clip);
static void draw_clip_rect(Compositor* comp, ClipDecoder* dec, TimelineClip* tc);
static void free_decoder(VM* vm, ClipDecoder* dec);
// 全局 VA 显示
extern VADisplay g_va_display;
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
const char* FS_SOURCE_NV12 =
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D tex_nv12; // NV12 纹理\n"
"uniform float u_opacity;\n"
"void main() {\n"
" float y = texture(tex_nv12, TexCoord).r;\n"
" vec2 uv = texture(tex_nv12, TexCoord / vec2(2.0, 2.0) + vec2(0.5, 0.5)).rg - 0.5;\n"
" float r = y + 1.5748 * uv.y;\n"
" float g = y - 0.1873 * uv.x - 0.4681 * uv.y;\n"
" float b = y + 1.8556 * uv.x;\n"
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
        GLuint fs = compile_shader(FS_SOURCE_NV12, GL_FRAGMENT_SHADER);
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
// --- Decoder Management ---
static AVFrame* alloc_frame_from_pool(Compositor* comp) {
    if (comp->pool_used < comp->pool_size) {
        return comp->frame_pool[comp->pool_used++];
    }
    if (comp->pool_used >= comp->pool_size) {
        i32 new_size = comp->pool_size * 2;
        comp->frame_pool = GROW_ARRAY(comp->vm, AVFrame*, comp->frame_pool, comp->pool_size, new_size);
        comp->pool_size = new_size;
    }
    AVFrame* frame = av_frame_alloc();
    if (frame) comp->vm->bytesAllocated += sizeof(AVFrame);
    return frame;
}
static void release_frame_to_pool(Compositor* comp, AVFrame** frame) {
    av_frame_unref(*frame);
    if (comp->pool_used >= comp->pool_size) {
        av_frame_free(frame);
    } else {
        comp->frame_pool[--comp->pool_used] = *frame;
    }
}
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
    dec->hw_accel = false;
    dec->hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
    if (dec->hw_device_ctx) {
        AVHWDeviceContext* hw_ctx = (AVHWDeviceContext*)dec->hw_device_ctx->data;
        AVVAAPIDeviceContext* va_ctx = (AVVAAPIDeviceContext*)hw_ctx->hwctx;
        va_ctx->display = g_va_display;
        if (av_hwdevice_ctx_init(dec->hw_device_ctx) >= 0) {
            dec->dec_ctx->hw_device_ctx = av_buffer_ref(dec->hw_device_ctx);
            dec->hw_accel = true;
            dec->hw_frames_ctx = av_hwframe_ctx_alloc(dec->hw_device_ctx);
            AVHWFramesContext* frames_ctx = (AVHWFramesContext*)(dec->hw_frames_ctx->data);
            frames_ctx->format = AV_PIX_FMT_VAAPI;
            frames_ctx->sw_format = AV_PIX_FMT_NV12;
            frames_ctx->width = dec->dec_ctx->width;
            frames_ctx->height = dec->dec_ctx->height;
            frames_ctx->initial_pool_size = 20;
            av_hwframe_ctx_init(dec->hw_frames_ctx);
            dec->dec_ctx->hw_frames_ctx = av_buffer_ref(dec->hw_frames_ctx);
        }
    }
    if (avcodec_open2(dec->dec_ctx, codec, NULL) < 0) { FREE(vm, ClipDecoder, dec); return NULL; }
    dec->raw_frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);
    dec->current_pts_sec = -1.0;
    // interop
    if (dec->hw_accel) {
        int major, minor;
        if (vaInitialize(g_va_display, &major, &minor) != VA_STATUS_SUCCESS) {
            dec->hw_accel = false;
        } else {
            VAConfigAttrib attr[1] = { {VAConfigAttribRTFormat, VA_RT_FORMAT_YUV420} };
            VAConfigID va_config;
            if (vaCreateConfig(g_va_display, VAProfileH264High, VAEntrypointVLD, attr, 1, &va_config) != VA_STATUS_SUCCESS) {
                dec->hw_accel = false;
            } else {
                if (vaCreateSurfaces(g_va_display, VA_RT_FORMAT_YUV420, dec->dec_ctx->width, dec->dec_ctx->height, &dec->va_surface, 1, NULL, 0) != VA_STATUS_SUCCESS) {
                    dec->hw_accel = false;
                } else {
                    EGLAttrib egl_attribs[] = {
                        EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_NV12,
                        EGL_WIDTH, dec->dec_ctx->width,
                        EGL_HEIGHT, dec->dec_ctx->height,
                        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
                        EGL_DMA_BUF_PLANE0_PITCH_EXT, dec->dec_ctx->width,
                        EGL_DMA_BUF_PLANE1_OFFSET_EXT, dec->dec_ctx->width * dec->dec_ctx->height,
                        EGL_DMA_BUF_PLANE1_PITCH_EXT, dec->dec_ctx->width / 2,
                        EGL_NONE
                    };
                    dec->egl_image = eglCreateImage(eglGetCurrentDisplay(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, egl_attribs);
                    glGenTextures(1, &dec->texture);
                    glBindTexture(GL_TEXTURE_2D, dec->texture);
                    // --- [修复] 动态加载并调用函数 ---
                    if (!my_glEGLImageTargetTexture2D) {
                        my_glEGLImageTargetTexture2D = (PFN_glEGLImageTargetTexture2D)eglGetProcAddress("glEGLImageTargetTexture2D");
                        if (!my_glEGLImageTargetTexture2D) {
                            // 尝试 OES 后缀
                            my_glEGLImageTargetTexture2D = (PFN_glEGLImageTargetTexture2D)eglGetProcAddress("glEGLImageTargetTexture2DOES");
                        }
                    }
                    if (my_glEGLImageTargetTexture2D) {
                        my_glEGLImageTargetTexture2D(GL_TEXTURE_2D, dec->egl_image);
                    } else {
                        fprintf(stderr, "Warning: glEGLImageTargetTexture2D not found!\n");
                    }
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                }
            }
        }
    } else {
        glGenTextures(1, &dec->texture);
        glBindTexture(GL_TEXTURE_2D, dec->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    return dec;
}
static void free_decoder(VM* vm, ClipDecoder* dec) {
    if (!dec) return;
    if (dec->hw_accel) {
        eglDestroyImage(eglGetCurrentDisplay(), dec->egl_image);
        vaDestroySurfaces(g_va_display, &dec->va_surface, 1);
    }
    glDeleteTextures(1, &dec->texture);
    if (dec->raw_frame) av_frame_free(&dec->raw_frame);
    if (dec->dec_ctx) avcodec_free_context(&dec->dec_ctx);
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
    if (dec->hw_frames_ctx) av_buffer_unref(&dec->hw_frames_ctx);
    if (dec->hw_device_ctx) av_buffer_unref(&dec->hw_device_ctx);
    FREE(vm, ClipDecoder, dec);
}
static i32 decode_frame_at_time(ClipDecoder* dec, double target_time, Compositor* comp) {
    UNUSED(comp);
    AVStream* stream = dec->fmt_ctx->streams[dec->video_stream_idx];
    double time_base = av_q2d(stream->time_base);
    double diff = target_time - dec->current_pts_sec;
    if (diff < 0 || diff > 0.5) {
        i64 target_ts = (i64)(target_time / time_base);
        av_seek_frame(dec->fmt_ctx, dec->video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec->dec_ctx);
        dec->current_pts_sec = -1.0;
    }
    AVPacket* pkt = av_packet_alloc();
    i32 ret = -1;
    if (dec->current_pts_sec >= 0 && (target_time - dec->current_pts_sec) >= 0 && (target_time - dec->current_pts_sec) < 0.05) {
        av_packet_free(&pkt);
        return 0;
    }
    while (av_read_frame(dec->fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == dec->video_stream_idx) {
            if (avcodec_send_packet(dec->dec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec->dec_ctx, dec->raw_frame) == 0) {
                    double pts = dec->raw_frame->pts * time_base;
                    dec->current_pts_sec = pts;
                    if (pts >= target_time) {
                        ret = 0;
                        goto done;
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }
done:
    av_packet_free(&pkt);
  
    if (dec->hw_accel && ret == 0) {
        // Hardware accel logic...
    } else if (ret == 0) {
        glBindTexture(GL_TEXTURE_2D, dec->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, dec->dec_ctx->width, dec->dec_ctx->height, 0, GL_RED, GL_UNSIGNED_BYTE, dec->raw_frame->data[0]);
    }
  
    return ret;
}
// 预取线程函数
static int prefetch_thread_func(void* data) {
    Compositor* comp = (Compositor*)data;
    while (comp->running) {
        SDL_LockMutex(comp->decoder_mutex);
        for (i32 i = 0; i < comp->decoder_count; i++) {
            ClipDecoder* dec = comp->decoders[i];
            if (dec->active_this_frame) {
                decode_frame_at_time(dec, dec->current_pts_sec + 1.0, comp);
            }
        }
        SDL_UnlockMutex(comp->decoder_mutex);
        SDL_Delay(16);
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
 
    comp->pool_size = 50;
    comp->frame_pool = ALLOCATE(vm, AVFrame*, comp->pool_size);
    for (int i = 0; i < comp->pool_size; i++) {
        comp->frame_pool[i] = av_frame_alloc();
    }
    comp->pool_used = 0;
 
    comp->decoder_mutex = SDL_CreateMutex();
    comp->running = true;
    comp->prefetch_thread = SDL_CreateThread(prefetch_thread_func, "Prefetch", comp);
 
    return comp;
}
void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    comp->running = false;
    int ret;
    SDL_WaitThread(comp->prefetch_thread, &ret);
    SDL_DestroyMutex(comp->decoder_mutex);
    for (int i = 0; i < comp->pool_size; i++) {
        av_frame_free(&comp->frame_pool[i]);
    }
    FREE_ARRAY(vm, AVFrame*, comp->frame_pool, comp->pool_size);
    for (i32 i=0; i<comp->decoder_count; i++) free_decoder(vm, comp->decoders[i]);
    if (comp->decoders) FREE_ARRAY(vm, ClipDecoder*, comp->decoders, comp->decoder_capacity);
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
    TimelineClip* clips[100]; // Assume max 100 clips
    i32 clip_count = 0;
    for (i32 i = 0; i < (i32)comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if ((track->flags & 1) == 0) continue;
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (tc) clips[clip_count++] = tc;
    }
    // Sort by z_index
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
            SDL_LockMutex(comp->decoder_mutex);
            if (decode_frame_at_time(dec, source_time, comp) == 0) {
                SDL_UnlockMutex(comp->decoder_mutex);
                int x = (int)tc->transform.x, y = (int)tc->transform.y;
                int w = (int)(dec->dec_ctx->width * tc->transform.scale_x);
                int h = (int)(dec->dec_ctx->height * tc->transform.scale_y);
                glScissor(x, y, w, h);
                glEnable(GL_SCISSOR_TEST);
                draw_clip_rect(comp, dec, tc);
                glDisable(GL_SCISSOR_TEST);
            } else {
                SDL_UnlockMutex(comp->decoder_mutex);
            }
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
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_nv12"), 0);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}