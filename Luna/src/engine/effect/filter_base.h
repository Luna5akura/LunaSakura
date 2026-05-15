#pragma once
#include "common.h"
#include "allocator.h"
#include "core/vm/vm.h"

typedef struct Animation Animation;
typedef struct Timeline Timeline;

typedef struct {
    int copy_u_texture;
    int copy_u_opacity;
    int tint_u_texture;
    int tint_u_amount;
    int tint_u_color;
    int fill_u_texture;
    int fill_u_amount;
    int fill_u_color;
    int gradient_u_texture;
    int gradient_u_start;
    int gradient_u_end;
    int gradient_u_start_color;
    int gradient_u_end_color;
    int gradient_u_blend;
    int grid_u_texture;
    int grid_u_size;
    int grid_u_line_width;
    int grid_u_color;
    int grid_u_opacity;
    int mosaic_u_texture;
    int mosaic_u_block_size;
    int mosaic_u_resolution;
    int mosaic_u_sharp_colors;
    int brightness_u_texture;
    int brightness_u_brightness;
    int brightness_u_contrast;
    int blur_u_texture;
    int blur_u_texel_size;
    int blur_u_radius;
    int glow_u_texture;
    int glow_u_texel_size;
    int glow_u_radius;
    int glow_u_intensity;
    int glow_u_threshold;
    int glow_u_softness;
    int glow_u_color;
    int fractal_u_texture;
    int fractal_u_resolution;
    int fractal_u_scale;
    int fractal_u_evolution;
    int fractal_u_contrast;
    int fractal_u_brightness;
    int fractal_u_octaves;
    int fractal_u_amount;
    int fractal_u_offset;
    int fractal_u_invert;
    int displacement_u_texture;
    int displacement_u_map_texture;
    int displacement_u_resolution;
    int displacement_u_scale_x;
    int displacement_u_scale_y;
    int displacement_u_amount;
    int displacement_u_offset;
    int displacement_u_horizontal_channel;
    int displacement_u_vertical_channel;
    int displacement_u_use_luma;
    int posterize_u_texture;
    int posterize_u_levels;
    int posterize_u_amount;
    unsigned int input_texture;
    unsigned int output_fbo;
    unsigned int auxiliary_texture;
    unsigned int quad_vao;
    unsigned int copy_shader_program;
    unsigned int tint_shader_program;
    unsigned int fill_shader_program;
    unsigned int gradient_ramp_shader_program;
    unsigned int grid_shader_program;
    unsigned int mosaic_shader_program;
    unsigned int brightness_contrast_shader_program;
    unsigned int blur_shader_program;
    unsigned int glow_shader_program;
    unsigned int fractal_noise_shader_program;
    unsigned int displacement_map_shader_program;
    unsigned int posterize_shader_program;
    int width;
    int height;
    bool prefer_nearest_output;
    bool has_auxiliary_texture;
} EffectRenderContext;

typedef enum {
    EFFECT_LINK_NUMBER,
    EFFECT_LINK_COLOR
} EffectLinkKind;

typedef struct EffectLink {
    EffectLinkKind kind;
    char target_key[32];
    char source_key[32];
    u32 source_clip_id;
    i32 cached_track_index;
    i32 cached_clip_index;
    struct EffectInstance* source_effect;
    double scale;
    double offset;
    struct EffectLink* next;
} EffectLink;

// === 效果实例（每个 Clip 可挂载多个效果）===
typedef struct EffectInstance {
    const char* name;
    const struct EffectProcessor* processor;
    void*       data;                    // 具体效果私有数据
    EffectLink* links;
    struct EffectInstance* next;
} EffectInstance;

// === 效果处理器接口（所有新效果必须实现）===
typedef struct EffectProcessor {
    const char* name;                                      // "gaussian_blur"
    
    // 创建效果实例
    void*      (*create)(Allocator* a, Value* params, i32 paramCount);
    
    // 应用效果（renderContext 为将来 compositor 传递的上下文，目前可用 void* 兼容）
    void       (*apply)(void* instance, void* renderContext, double time);
    
    // 销毁效果实例
    void       (*destroy)(Allocator* a, void* instance);
    
    // GC 标记钩子（可选）
    void       (*mark)(VM* vm, void* instance);
    
    bool       (*set_number)(void* instance, const char* key, double value);
    bool       (*get_number)(void* instance, const char* key, double* out_value);
    bool       (*set_bool)(void* instance, const char* key, bool value);
    bool       (*get_bool)(void* instance, const char* key, bool* out_value);
    bool       (*set_color)(void* instance, const char* key, double r, double g, double b, double a);
    bool       (*set_source_clip)(void* instance, u32 clip_id);
    u32        (*get_source_clip)(void* instance);
    struct Animation* (*get_number_animation)(void* instance, const char* key);
    bool       (*get_color_animations)(void* instance, const char* key,
                                       struct Animation** out_r,
                                       struct Animation** out_g,
                                       struct Animation** out_b,
                                       struct Animation** out_a);
} EffectProcessor;

// === 注册表 API ===
void effect_registry_init(VM* vm);
void effect_registry_register(const EffectProcessor* proc);
const EffectProcessor* effect_registry_get(const char* name);

EffectInstance* effect_chain_append(Allocator* a, EffectInstance** chain, const EffectProcessor* proc, Value* params, i32 param_count);
EffectInstance* effect_instance_create(Allocator* a, const EffectProcessor* proc, Value* params, i32 param_count);
bool effect_chain_append_existing(EffectInstance** chain, EffectInstance* effect);
i32 effect_chain_count(EffectInstance* chain);
EffectInstance* effect_chain_get(EffectInstance* chain, i32 index);
bool effect_chain_remove_at(Allocator* a, EffectInstance** chain, i32 index);
void effect_chain_clear(Allocator* a, EffectInstance** chain);
void effect_instance_destroy(Allocator* a, EffectInstance* effect);
bool effect_link_number(Allocator* a, EffectInstance* target, const char* target_key,
                        u32 source_clip_id, EffectInstance* source_effect, const char* source_key,
                        double scale, double offset);
bool effect_link_color(Allocator* a, EffectInstance* target, const char* target_key,
                       u32 source_clip_id, EffectInstance* source_effect, const char* source_key);
bool effect_unlink(Allocator* a, EffectInstance* target, const char* target_key, EffectLinkKind kind);
void effect_apply_links(Timeline* timeline, EffectInstance* target, double time);
