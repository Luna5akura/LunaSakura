// src/engine/compositor.c

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <glad/glad.h> 
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

#include "compositor.h"
#include "vm/memory.h"
#include "vm/vm.h"

// --- Shader Sources ---

const char* VS_SOURCE = 
"#version 330 core\n"
"layout (location = 0) in vec2 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"uniform mat4 u_projection;\n"
"uniform mat4 u_model;\n"
"void main() {\n"
"    gl_Position = u_projection * u_model * vec4(aPos, 0.0, 1.0);\n"
"    TexCoord = aTexCoord;\n"
"}\n";

const char* FS_SOURCE = 
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D tex_y;\n"
"uniform sampler2D tex_u;\n"
"uniform sampler2D tex_v;\n"
"uniform float u_opacity;\n"
"void main() {\n"
"    float y = texture(tex_y, TexCoord).r;\n"
"    float u = texture(tex_u, TexCoord).r - 0.5;\n"
"    float v = texture(tex_v, TexCoord).r - 0.5;\n"
"    float r = y + 1.5748 * v;\n"
"    float g = y - 0.1873 * u - 0.4681 * v;\n"
"    float b = y + 1.8556 * u;\n"
"    FragColor = vec4(r, g, b, u_opacity);\n"
"}\n";

// 用于 Blit 到屏幕的简单 Shader
const char* VS_SCREEN = 
"#version 330 core\n"
"layout (location = 0) in vec2 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"out vec2 TexCoord;\n"
"void main() {\n"
"    gl_Position = vec4(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);\n"
"    TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);\n" 
"}\n";
const char* FS_SCREEN = 
"#version 330 core\n"
"out vec4 FragColor;\n"
"in vec2 TexCoord;\n"
"uniform sampler2D screenTexture;\n"
"void main() {\n"
"    FragColor = texture(screenTexture, TexCoord);\n"
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
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader Compile Error: %s\n", infoLog);
    }
    return shader;
}

static void init_gl_resources(Compositor* comp) {
    // 1. Program YUV -> RGB
    GLuint vs = compile_shader(VS_SOURCE, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(FS_SOURCE, GL_FRAGMENT_SHADER);
    comp->shader_program = glCreateProgram();
    glAttachShader(comp->shader_program, vs);
    glAttachShader(comp->shader_program, fs);
    glLinkProgram(comp->shader_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // 2. Quad (0.0 to 1.0)
    float quad[] = {
        // x, y       u, v
        0.0f, 0.0f,   0.0f, 0.0f, // TL
        1.0f, 0.0f,   1.0f, 0.0f, // TR
        0.0f, 1.0f,   0.0f, 1.0f, // BL
        0.0f, 1.0f,   0.0f, 1.0f, // BL
        1.0f, 0.0f,   1.0f, 0.0f, // TR
        1.0f, 1.0f,   1.0f, 1.0f  // BR
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

    // 3. FBO
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
    vm->bytesAllocated += sizeof(AVCodecContext);  // Track FFmpeg alloc
    avcodec_parameters_to_context(dec->dec_ctx, stream->codecpar);
    
    if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        dec->dec_ctx->thread_count = 0;
        dec->dec_ctx->thread_type = FF_THREAD_FRAME;
    }
    
    if (avcodec_open2(dec->dec_ctx, codec, NULL) < 0) { FREE(vm, ClipDecoder, dec); return NULL; }
    dec->raw_frame = av_frame_alloc();
    vm->bytesAllocated += sizeof(AVFrame);  // Track
    dec->current_pts_sec = -1.0;
    
    // Gen Textures
    glGenTextures(3, dec->yuv_textures);
    for (int i=0; i<3; i++) {
        glBindTexture(GL_TEXTURE_2D, dec->yuv_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    return dec;
}

static void free_decoder(VM* vm, ClipDecoder* dec) {
    if (!dec) return;
    glDeleteTextures(3, dec->yuv_textures);
    if (dec->raw_frame) {
        av_frame_free(&dec->raw_frame);
        vm->bytesAllocated -= sizeof(AVFrame);
    }
    if (dec->dec_ctx) {
        avcodec_free_context(&dec->dec_ctx);
        vm->bytesAllocated -= sizeof(AVCodecContext);
    }
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
    FREE(vm, ClipDecoder, dec);
}

static ClipDecoder* get_decoder(Compositor* comp, ObjClip* clip) {
    VM* vm = comp->vm;  // Use stored vm
    for (int i = 0; i < comp->decoder_count; i++) {
        if (comp->decoders[i]->clip_ref == clip) {
            comp->decoders[i]->active_this_frame = true;
            return comp->decoders[i];
        }
    }
    ClipDecoder* dec = create_decoder(vm, clip);
    if (!dec) return NULL;
    if (comp->decoder_count >= comp->decoder_capacity) {
        int new_cap = comp->decoder_capacity == 0 ? 4 : comp->decoder_capacity * 2;
        comp->decoders = GROW_ARRAY(vm, ClipDecoder*, comp->decoders, comp->decoder_capacity, new_cap);
        comp->decoder_capacity = new_cap;
    }
    dec->active_this_frame = true;
    comp->decoders[comp->decoder_count++] = dec;
    return dec;
}

static int decode_frame_at_time(ClipDecoder* dec, double target_time) {
    AVStream* stream = dec->fmt_ctx->streams[dec->video_stream_idx];
    double time_base = av_q2d(stream->time_base);
    double diff = target_time - dec->current_pts_sec;

    // Seek strategy
    if (diff < 0 || diff > 0.5) {
        int64_t target_ts = (int64_t)(target_time / time_base);
        av_seek_frame(dec->fmt_ctx, dec->video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec->dec_ctx);
        dec->current_pts_sec = -1.0;
    }

    AVPacket* pkt = av_packet_alloc();
    int ret = -1;
    
    // Simple caching: if current frame is close enough, reuse it
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
    return ret;
}

// --- GPU Upload & Draw ---
static void upload_frame_to_gpu(ClipDecoder* dec) {
    AVFrame* f = dec->raw_frame;
    if (!f->data[0]) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    // Y
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, dec->yuv_textures[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->linesize[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, f->width, f->height, 0, GL_RED, GL_UNSIGNED_BYTE, f->data[0]);
    
    // U
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, dec->yuv_textures[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->linesize[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, f->width/2, f->height/2, 0, GL_RED, GL_UNSIGNED_BYTE, f->data[1]);
    
    // V
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, dec->yuv_textures[2]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->linesize[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, f->width/2, f->height/2, 0, GL_RED, GL_UNSIGNED_BYTE, f->data[2]);
    
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
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
    
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, dec->yuv_textures[0]);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_y"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, dec->yuv_textures[1]);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_u"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, dec->yuv_textures[2]);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_v"), 2);

    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// --- Lifecycle ---

Compositor* compositor_create(VM* vm, Timeline* timeline) {
    Compositor* comp = ALLOCATE(vm, Compositor, 1);
    memset(comp, 0, sizeof(Compositor));
    comp->vm = vm;
    comp->timeline = timeline;
    init_gl_resources(comp);
    return comp;
}

void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    for (int i=0; i<comp->decoder_count; i++) free_decoder(vm, comp->decoders[i]);
    if (comp->decoders) FREE_ARRAY(vm, ClipDecoder*, comp->decoders, comp->decoder_capacity);
    glDeleteVertexArrays(1, &comp->vao);
    glDeleteBuffers(1, &comp->vbo);
    glDeleteFramebuffers(1, &comp->fbo);
    glDeleteTextures(1, &comp->output_texture);
    glDeleteProgram(comp->shader_program);
    if (comp->cpu_output_buffer) reallocate(vm, comp->cpu_output_buffer, comp->timeline->width * comp->timeline->height * 4, 0);
    FREE(vm, Compositor, comp);
}

void compositor_render(Compositor* comp, double time) {
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glViewport(0, 0, comp->timeline->width, comp->timeline->height);
    
    // 修正：直接读取结构体成员，修复 unknown type name 'Color' 错误
    uint8_t r = comp->timeline->background_color.r;
    uint8_t g = comp->timeline->background_color.g;
    uint8_t b = comp->timeline->background_color.b;
    glClearColor(r/255.0f, g/255.0f, b/255.0f, 1.0f);

    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    mat4 proj = mat4_ortho(0.0f, (float)comp->timeline->width, 
                           (float)comp->timeline->height, 0.0f, 
                           -1.0f, 1.0f);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_projection"), 1, GL_FALSE, proj.m);

    for (int i = 0; i < comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if ((track->flags & 1) == 0) continue;
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        ClipDecoder* dec = get_decoder(comp, tc->media);
        if (dec) {
            double source_time = tc->source_in + (time - tc->timeline_start);
            if (decode_frame_at_time(dec, source_time) == 0) {
                upload_frame_to_gpu(dec);
                draw_clip_rect(comp, dec, tc);
            }
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    comp->cpu_buffer_stale = true;
}

// 辅助：把 FBO 里的东西画到屏幕上
void compositor_blit_to_screen(Compositor* comp, int window_width, int window_height) {
    // 使用简单的 Pass-through shader
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

// 垂直翻转 buffer 的辅助函数
static void flip_buffer_vertical(uint8_t* buffer, int width, int height) {
    int stride = width * 4;
    uint8_t* row_buf = malloc(stride);
    for (int y = 0; y < height / 2; y++) {
        uint8_t* top = buffer + y * stride;
        uint8_t* bot = buffer + (height - 1 - y) * stride;
        memcpy(row_buf, top, stride);
        memcpy(top, bot, stride);
        memcpy(bot, row_buf, stride);
    }
    free(row_buf);
}

uint8_t* compositor_get_cpu_buffer(Compositor* comp) {
    if (comp->cpu_buffer_stale) {
        size_t size = comp->timeline->width * comp->timeline->height * 4;
        if (!comp->cpu_output_buffer) {
            comp->cpu_output_buffer = reallocate(comp->vm, NULL, 0, size);
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
        // glReadPixels 读取的是从左下角开始的，而内存图片通常是左上角开始
        glReadPixels(0, 0, comp->timeline->width, comp->timeline->height, GL_RGBA, GL_UNSIGNED_BYTE, comp->cpu_output_buffer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // 修正垂直翻转
        flip_buffer_vertical(comp->cpu_output_buffer, comp->timeline->width, comp->timeline->height);
        
        comp->cpu_buffer_stale = false;
    }
    return comp->cpu_output_buffer;
}