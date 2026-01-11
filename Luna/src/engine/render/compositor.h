// src/engine/render/compositor.h

#pragma once

#include <SDL2/SDL.h>
#include "common.h"
#include "engine/model/timeline.h"
#include "engine/media/codec/decoder.h"
#include "engine/media/audio/mixer.h"

typedef struct VM VM;

// --- Compositor ---
typedef struct {
    VM* vm;
    Timeline* timeline;
    
    // GL Resources
    unsigned int shader_program;
    unsigned int vao, vbo;
    unsigned int fbo;
    unsigned int output_texture;
    
    // CPU Readback Buffer (Lazy allocation)
    u8* cpu_output_buffer;
    bool cpu_buffer_stale;
    
    // Decoders Management
    Decoder** decoders;
    i32 decoder_count;
    i32 decoder_capacity;
    
    // Audio Module
    AudioMixer* mixer;
} Compositor;

// --- API ---

// 初始化合成器 (GL Context 必须在当前线程激活)
Compositor* compositor_create(VM* vm, Timeline* timeline);

// 销毁合成器
void compositor_free(VM* vm, Compositor* comp);

// 渲染指定时间点的一帧到 FBO
void compositor_render(Compositor* comp, double time);

// 将 FBO 内容绘制到屏幕窗口 (用于实时预览)
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height);

// 将 FBO 内容读回 CPU 内存 (支持垂直翻转，用于导出)
// out_buffer: 必须预分配 width * height * 4 字节
void compositor_read_pixels(Compositor* comp, u8* out_buffer);

// 获取内部缓存的 CPU Buffer (可选，用于调试)
u8* compositor_get_cpu_buffer(Compositor* comp);