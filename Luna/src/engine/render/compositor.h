// src/engine/render/compositor.h

#pragma once
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include "engine/model/timeline.h"
#include "engine/media/audio/mixer.h"
#include "engine/render/text_renderer.h" // 新增
typedef struct VM VM;
typedef struct ObjTimeline ObjTimeline;
typedef struct {
    int u_projection;
    int u_model;
    int u_opacity;
    int sampler0;
    int sampler1;
    int sampler2;
    int u_color;
    int u_uv_rect;
} ShaderUniformCache;

typedef struct Compositor {
    VM* vm;
    Timeline* timeline;
   
    unsigned int shader_program; // Video YUV Shader
    unsigned int image_shader_program;
    unsigned int color_shader_program;
    unsigned int text_shader_program;// [新增] Text Shader
    unsigned int copy_shader_program;
    unsigned int tint_shader_program;
    unsigned int fill_shader_program;
    unsigned int gradient_ramp_shader_program;
    unsigned int grid_shader_program;
    unsigned int mosaic_shader_program;
    unsigned int brightness_contrast_shader_program;
    unsigned int blur_shader_program;
    unsigned int fractal_noise_shader_program;
    unsigned int displacement_map_shader_program;
    unsigned int posterize_shader_program;
    unsigned int adjustment_composite_shader_program;
    ShaderUniformCache yuv_uniforms;
    ShaderUniformCache image_uniforms;
    ShaderUniformCache color_uniforms;
    ShaderUniformCache text_uniforms;
    ShaderUniformCache copy_uniforms;
    ShaderUniformCache tint_uniforms;
    ShaderUniformCache fill_uniforms;
    ShaderUniformCache gradient_uniforms;
    ShaderUniformCache grid_uniforms;
    ShaderUniformCache mosaic_uniforms;
    ShaderUniformCache brightness_uniforms;
    ShaderUniformCache blur_uniforms;
    ShaderUniformCache fractal_uniforms;
    ShaderUniformCache displacement_uniforms;
    ShaderUniformCache posterize_uniforms;
    int fractal_u_amount;
    int fractal_u_offset;
    int fractal_u_invert;
    int displacement_u_offset;
    int displacement_u_use_luma;
    int adjustment_u_base_texture;
    int adjustment_u_adjusted_texture;
    int adjustment_u_mask_texture;
    int adjustment_u_region_center;
    int adjustment_u_region_half_size;
    int adjustment_u_canvas_size;
    int adjustment_u_opacity;
    int adjustment_u_feather;
    int adjustment_u_region_rotation;
    int adjustment_u_blend_mode;
    int adjustment_u_mask_mode;
    int adjustment_u_whole_frame;
    int adjustment_u_has_mask;
    int adjustment_u_mask_invert;
   
    unsigned int vao, vbo;
    unsigned int fbo;
    unsigned int output_texture;
    unsigned int effect_source_fbo;
    unsigned int effect_source_texture;
    unsigned int effect_ping_fbo;
    unsigned int effect_ping_texture;
    unsigned int effect_base_fbo;
    unsigned int effect_base_texture;
    unsigned int effect_aux_fbo;
    unsigned int effect_aux_texture;
    int effect_width;
    int effect_height;
    bool preview_prefers_nearest;
   
    uint8_t* cpu_output_buffer;
    uint8_t* cpu_flip_row;
    size_t cpu_flip_row_capacity;
    bool cpu_buffer_stale;
    bool cpu_buffer_flipped;

    void* active_clips_buffer;
    int active_clips_capacity;
   
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
uint8_t* compositor_get_cpu_buffer(Compositor* comp);
uint8_t* compositor_get_cpu_buffer_raw(Compositor* comp);
