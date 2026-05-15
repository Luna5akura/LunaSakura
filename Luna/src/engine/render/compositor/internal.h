#pragma once

#include "../compositor.h"
#include "engine/media/codec/decoder.h"
#include "engine/effect/filter_base.h"
#include "engine/media/utils/image_loader.h"
#include "engine/render/gl_utils.h"
#include "engine/bridge/object.h"
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/model/animation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float m[16]; } mat4;
#define TEXT_RENDERER_MAX_GLYPHS 128

mat4 mat4_identity(void);
mat4 mat4_mult(mat4 a, mat4 b);
mat4 mat4_translate(float x, float y);
mat4 mat4_scale(float sx, float sy);
mat4 mat4_rotate(float angle_deg);
mat4 mat4_shear_x(float factor);
mat4 mat4_skew(float skew_deg, float axis_deg);
mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far);

extern const char* VS_SOURCE;
extern const char* FS_SOURCE_YUV;
extern const char* FS_SOURCE_RGBA;
extern const char* FS_SOURCE_COLOR;
extern const char* FS_SOURCE_COPY;
extern const char* FS_SOURCE_TINT;
extern const char* FS_SOURCE_FILL;
extern const char* FS_SOURCE_GRADIENT_RAMP;
extern const char* FS_SOURCE_GRID;
extern const char* FS_SOURCE_MOSAIC;
extern const char* FS_SOURCE_BRIGHTNESS_CONTRAST;
extern const char* FS_SOURCE_BLUR;
extern const char* FS_SOURCE_GLOW;
extern const char* FS_SOURCE_FRACTAL_NOISE;
extern const char* FS_SOURCE_DISPLACEMENT_MAP;
extern const char* FS_SOURCE_POSTERIZE;
extern const char* FS_SOURCE_ADJUSTMENT_COMPOSITE;
extern const char* VS_SCREEN;
extern const char* FS_SCREEN;
extern const char* FS_SOURCE_TEXT;

typedef enum {
    RENDER_SOURCE_MEDIA,
    RENDER_SOURCE_IMAGE,
    RENDER_SOURCE_NESTED
} RenderSourceType;

typedef struct {
    RenderSourceType type;
    Clip* clip_ref;
    Timeline* nested_timeline;
    Compositor* nested_compositor;
    Decoder* decoder;
    GLuint tex_y;
    GLuint tex_u;
    GLuint tex_v;
    GLuint tex_rgba;
    int width, height;
    bool image_loaded;
} RenderSource;

typedef struct {
    float offset_x;
    float offset_y;
    float scale_x;
    float scale_y;
    float rotation;
    float anchor_x;
    float anchor_y;
    float skew;
    float skew_axis;
    float opacity_factor;
    float fill_opacity;
    float stroke_opacity;
    float tracking;
    float stroke_width;
    float fill_r;
    float fill_g;
    float fill_b;
    float fill_a;
    float stroke_r;
    float stroke_g;
    float stroke_b;
    float stroke_a;
    float character_offset;
    float character_value;
} TextCharStyle;

typedef struct {
    int word_index;
    int line_index;
    bool word_selectable;
    bool line_selectable;
} TextCharMeta;

typedef struct {
    double index;
    double count;
    double time;
    double position;
    double char_index;
    double char_count;
    double word_index;
    double word_count;
    double line_index;
    double line_count;
} TextExpressionContext;

typedef struct {
    const char* cursor;
    const TextExpressionContext* ctx;
    bool error;
} TextExpressionParser;

static inline float clamp01f(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static inline float wrap01f(float value) {
    float wrapped = fmodf(value, 1.0f);
    if (wrapped < 0.0f) wrapped += 1.0f;
    return wrapped;
}

static inline float mixf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float hash11f(float x) {
    float v = sinf(x * 127.1f) * 43758.5453123f;
    return v - floorf(v);
}

static inline float noise11f(float x) {
    float i = floorf(x);
    float f = x - i;
    float a = hash11f(i);
    float b = hash11f(i + 1.0f);
    float t = f * f * (3.0f - 2.0f * f);
    return mixf(a, b, t);
}

static inline void rgb_to_hsv(float r, float g, float b, float* out_h, float* out_s, float* out_v) {
    float maxv = fmaxf(r, fmaxf(g, b));
    float minv = fminf(r, fminf(g, b));
    float delta = maxv - minv;
    float h = 0.0f;
    float s = (maxv <= 1e-6f) ? 0.0f : delta / maxv;
    if (delta > 1e-6f) {
        if (maxv == r) h = fmodf(((g - b) / delta), 6.0f);
        else if (maxv == g) h = ((b - r) / delta) + 2.0f;
        else h = ((r - g) / delta) + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    if (out_h) *out_h = h;
    if (out_s) *out_s = s;
    if (out_v) *out_v = maxv;
}

static inline void hsv_to_rgb(float h, float s, float v, float* out_r, float* out_g, float* out_b) {
    float hh = wrap01f(h) * 6.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(hh, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hh < 1.0f) { r = c; g = x; }
    else if (hh < 2.0f) { r = x; g = c; }
    else if (hh < 3.0f) { g = c; b = x; }
    else if (hh < 4.0f) { g = x; b = c; }
    else if (hh < 5.0f) { r = x; b = c; }
    else { r = c; b = x; }
    if (out_r) *out_r = r + m;
    if (out_g) *out_g = g + m;
    if (out_b) *out_b = b + m;
}

static inline void apply_hsv_adjustment(float* r, float* g, float* b, float hue_deg, float sat_delta, float val_delta) {
    float h, s, v;
    rgb_to_hsv(*r, *g, *b, &h, &s, &v);
    h = wrap01f(h + hue_deg / 360.0f);
    s = clamp01f(s + sat_delta / 100.0f);
    v = clamp01f(v + val_delta / 100.0f);
    hsv_to_rgb(h, s, v, r, g, b);
}

static inline float smoothstepf(float edge0, float edge1, float x) {
    float t;
    if (fabsf(edge1 - edge0) < 1e-6f) return x >= edge1 ? 1.0f : 0.0f;
    t = clamp01f((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static inline float apply_selector_ease(float value, float ease_low, float ease_high) {
    float result = clamp01f(value);
    if (ease_low > 0.0f && result < 0.5f) {
        float exponent = 1.0f + (ease_low / 100.0f) * 2.0f;
        result = 0.5f * powf(result * 2.0f, exponent);
    }
    if (ease_high > 0.0f && result > 0.5f) {
        float exponent = 1.0f + (ease_high / 100.0f) * 2.0f;
        float t = (result - 0.5f) * 2.0f;
        t = 1.0f - powf(1.0f - t, exponent);
        result = 0.5f + t * 0.5f;
    }
    return clamp01f(result);
}

static inline float selector_shape_value(TextSelectorShape shape, float normalized) {
    float t = clamp01f(normalized);
    switch (shape) {
        case TEXT_SELECTOR_SHAPE_RAMP_UP:
            return t;
        case TEXT_SELECTOR_SHAPE_RAMP_DOWN:
            return 1.0f - t;
        case TEXT_SELECTOR_SHAPE_TRIANGLE:
            return 1.0f - fabsf(2.0f * t - 1.0f);
        case TEXT_SELECTOR_SHAPE_SMOOTH:
            return smoothstepf(0.0f, 1.0f, t);
        case TEXT_SELECTOR_SHAPE_SQUARE:
        default:
            return (t > 0.0f && t < 1.0f) ? 1.0f : 0.0f;
    }
}

void init_compositor_uniform_caches(Compositor* comp);
void cache_uniform_locations(Compositor* comp);
void setup_texture_params(GLuint tex);
RenderSource* get_source_safe(Compositor* comp, Clip* clip);
Compositor* get_nested_compositor_safe(Compositor* comp, Clip* clip, Timeline* nested);
void build_text_char_meta(const char* text, int char_count, TextCharMeta* metas, int* out_word_count, int* out_line_count);
bool resolve_selector_unit(TextSelectorBasedOn based_on, const TextCharMeta* meta, int char_index, int char_count, int total_words, int total_lines, int* out_unit_index, int* out_unit_count);
double evaluate_text_expression(const char* expression, const TextExpressionContext* ctx, bool* out_ok);
void compute_text_char_style(const TimelineClip* tc, double anim_time, const TextCharMeta* meta, int total_words, int total_lines, int char_index, int char_count, TextCharStyle* out_style);
void draw_clip_text(Compositor* comp, TimelineClip* tc, double anim_time);
void draw_clip_rect(Compositor* comp, RenderSource* src, TimelineClip* tc);
void draw_clip_rgba(Compositor* comp, RenderSource* src, TimelineClip* tc);
void draw_clip_solid(Compositor* comp, TimelineClip* tc);
void get_anchor_point_coords(float left, float top, float width, float height, u8 anchor, float* out_x, float* out_y);
void get_clip_visual_size(Compositor* comp, TimelineClip* tc, float* out_width, float* out_height);
void evaluate_clip_transform_state(Compositor* comp, TimelineClip* tc, double time, float* out_x, float* out_y, float* out_scale_x, float* out_scale_y, float* out_rotation, float* out_opacity);
bool resolve_clip_layout_recursive(Compositor* comp, TimelineClip* tc, double time, int depth, float* out_x, float* out_y, float* out_w, float* out_h);
void ensure_effect_targets(Compositor* comp, int width, int height);
void set_texture_sampling(GLuint texture, bool nearest);
void draw_texture_transformed(Compositor* comp, GLuint texture, TimelineClip* tc, float width, float height,
                              bool nearest_sampling, bool premultiplied);
void draw_texture_fullframe(Compositor* comp, GLuint texture, bool nearest_sampling, float opacity);
bool is_nested_timeline_clip(const Clip* clip);
void render_nested_timeline_to_target(Compositor* comp, TimelineClip* tc, double clip_time, GLuint target_fbo);
void render_clip_source_to_target(Compositor* comp, TimelineClip* tc, double clip_time, GLuint target_fbo,
                                  float offset_x, float offset_y);
void render_clip_source_to_effect_target(Compositor* comp, TimelineClip* tc, double clip_time,
                                         float offset_x, float offset_y);
bool effect_chain_has_external_source_refs(EffectInstance* effect);
void populate_effect_render_context(Compositor* comp, EffectRenderContext* ctx, GLuint input_texture, GLuint output_fbo);
bool render_clip_effect_result_to_auxiliary(Compositor* comp, TimelineClip* tc, double clip_time, GLuint scratch_texture, GLuint scratch_fbo, bool* out_prefer_nearest);
void render_clip_with_effects(Compositor* comp, TimelineClip* tc, double clip_time);
void composite_adjustment_layer(Compositor* comp, TimelineClip* tc, GLuint base_texture, GLuint adjusted_texture, GLuint mask_texture, bool has_mask, bool nearest_sampling);
