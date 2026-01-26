// src/engine/render/compositor.h

#pragma once
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include "engine/model/timeline.h"
#include "engine/media/audio/mixer.h"
#include "engine/render/text_renderer.h" // 新增
typedef struct VM VM;
typedef struct Compositor {
    VM* vm;
    Timeline* timeline;
   
    unsigned int shader_program; // Video YUV Shader
    unsigned int text_shader_program;// [新增] Text Shader
   
    unsigned int vao, vbo;
    unsigned int fbo;
    unsigned int output_texture;
   
    uint8_t* cpu_output_buffer;
    bool cpu_buffer_stale;
   
    void* render_sources;
    int32_t source_count;
    int32_t source_capacity;
   
    TextRenderer* text_renderer; // [新增]
    AudioMixer* mixer;
} Compositor;
Compositor* compositor_create(VM* vm, Timeline* timeline);
void compositor_free(VM* vm, Compositor* comp);
void compositor_render(Compositor* comp, double time);
void compositor_blit_to_screen(Compositor* comp, int32_t w, int32_t h);
void compositor_read_pixels(Compositor* comp, uint8_t* out_buffer);