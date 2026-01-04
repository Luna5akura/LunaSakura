// src/engine/compositor.h

#ifndef LUNA_ENGINE_COMPOSITOR_H
#define LUNA_ENGINE_COMPOSITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "engine/timeline.h"

// 包含 GLAD (确保你的项目中包含 glad.c 并在 include 路径下)
#include <glad/glad.h>

// 前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;

// --- ClipDecoder ---
typedef struct {
    ObjClip* clip_ref;
    
    // FFmpeg
    struct AVFormatContext* fmt_ctx;
    struct AVCodecContext* dec_ctx;
    struct AVFrame* raw_frame; 
    int video_stream_idx;
    double current_pts_sec;

    // OpenGL (YUV Textures)
    GLuint yuv_textures[3]; 
    int tex_w, tex_h;

    bool active_this_frame;
} ClipDecoder;

// --- Compositor ---
typedef struct {
    Timeline* timeline;

    // GL Resources
    GLuint shader_program;
    GLuint vao, vbo;
    GLuint fbo;
    GLuint output_texture;
    
    // CPU Readback
    uint8_t* cpu_output_buffer;
    bool cpu_buffer_stale;

    // Decoders
    ClipDecoder** decoders;
    int decoder_count;
    int decoder_capacity;
    
    long frame_counter;
} Compositor;

// --- API ---
// 注意：创建 Compositor 前必须先创建 OpenGL Context (在 main.c 中完成)
Compositor* compositor_create(Timeline* timeline);
void compositor_free(Compositor* comp);

// 核心渲染：绘制到内部 FBO
void compositor_render(Compositor* comp, double time);

// [新] 辅助函数：将内部 FBO 的内容绘制到默认帧缓冲区（即屏幕窗口）
// 这让 main.c 不需要自己写 Shader 就能显示结果
void compositor_blit_to_screen(Compositor* comp, int window_width, int window_height);

// 获取 CPU 缓冲区 (用于导出，包含 glReadPixels 和垂直翻转处理)
uint8_t* compositor_get_cpu_buffer(Compositor* comp);

#endif