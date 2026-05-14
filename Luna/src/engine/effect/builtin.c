#include "engine/effect/filter_base.h"
#include "engine/model/animation.h"

#include <glad/glad.h>
#include <math.h>
#include <string.h>

typedef struct {
    Animation amount;
    Animation color[4];
} ColorMixEffectData;

typedef struct {
    Animation brightness;
    Animation contrast;
} BrightnessContrastEffectData;

typedef struct {
    Animation radius;
} BlurEffectData;

typedef struct {
    Animation block_size;
    bool sharp_colors;
} MosaicEffectData;

typedef struct {
    Animation size_x;
    Animation size_y;
    Animation line_width;
    Animation opacity;
    Animation color[4];
} GridEffectData;

typedef struct {
    Animation start[2];
    Animation end[2];
    Animation start_color[4];
    Animation end_color[4];
    Animation blend;
} GradientRampEffectData;

typedef struct {
    Animation scale;
    Animation evolution;
    Animation contrast;
    Animation brightness;
    Animation octaves;
    Animation amount;
    Animation offset_x;
    Animation offset_y;
    bool invert;
} FractalNoiseEffectData;

typedef struct {
    Animation scale_x;
    Animation scale_y;
    Animation amount;
    Animation offset_x;
    Animation offset_y;
    bool use_luma;
    u32 source_clip_id;
} DisplacementMapEffectData;

typedef struct {
    Animation levels;
    Animation amount;
} PosterizeEffectData;

typedef struct {
    Animation value;
} SingleNumberControlEffectData;

typedef struct {
    Animation value;
} CheckboxControlEffectData;

typedef struct {
    Animation x;
    Animation y;
} PointControlEffectData;

typedef struct {
    Animation color[4];
} ColorControlEffectData;

static void draw_fullscreen_quad(GLuint vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void bind_effect_input_texture(GLuint texture) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
}

static void begin_effect_pass(EffectRenderContext* ctx, GLuint fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, ctx->width, ctx->height);
    glDisable(GL_BLEND);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void end_effect_pass(void) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void copy_input_to_output(EffectRenderContext* ctx) {
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->copy_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->copy_u_texture, 0);
    glUniform1f(ctx->copy_u_opacity, 1.0f);
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void init_color_mix_data(ColorMixEffectData* data, Allocator* a) {
    init_animation(&data->amount, a, 1.0);
    for (int i = 0; i < 4; i++) init_animation(&data->color[i], a, 1.0);
}

static void free_color_mix_data(ColorMixEffectData* data, Allocator* a) {
    free_animation(&data->amount, a);
    for (int i = 0; i < 4; i++) free_animation(&data->color[i], a);
}

static bool resolve_color_array(Animation* color, const char* key, const char* expected,
                                Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    if (strcmp(key, expected) != 0) return false;
    if (out_r) *out_r = &color[0];
    if (out_g) *out_g = &color[1];
    if (out_b) *out_b = &color[2];
    if (out_a) *out_a = &color[3];
    return true;
}

static bool get_color_mix_animation(ColorMixEffectData* data, const char* key,
                                    Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return resolve_color_array(data->color, key, "color", out_r, out_g, out_b, out_a);
}

static bool set_color_array(Animation* color, const char* key, const char* expected,
                            double r, double g, double b, double a) {
    if (strcmp(key, expected) != 0) return false;
    color[0].default_value = r / 255.0;
    color[1].default_value = g / 255.0;
    color[2].default_value = b / 255.0;
    color[3].default_value = a / 255.0;
    return true;
}

static void* color_mix_create(Allocator* a, Value* params, i32 param_count) {
    ColorMixEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, ColorMixEffectData, 1);
    if (!data) return NULL;
    init_color_mix_data(data, a);
    return data;
}

static void color_mix_destroy(Allocator* a, void* instance) {
    ColorMixEffectData* data = (ColorMixEffectData*)instance;
    free_color_mix_data(data, a);
    MEM_FREE(a, ColorMixEffectData, data);
}

static bool color_mix_set_number(void* instance, const char* key, double value) {
    ColorMixEffectData* data = (ColorMixEffectData*)instance;
    if (strcmp(key, "amount") != 0) return false;
    data->amount.default_value = fmax(0.0, fmin(1.0, value));
    return true;
}

static bool color_mix_get_number(void* instance, const char* key, double* out_value) {
    ColorMixEffectData* data = (ColorMixEffectData*)instance;
    if (!out_value || strcmp(key, "amount") != 0) return false;
    *out_value = data->amount.default_value;
    return true;
}

static bool color_mix_set_color(void* instance, const char* key, double r, double g, double b, double a) {
    return set_color_array(((ColorMixEffectData*)instance)->color, key, "color", r, g, b, a);
}

static void tint_apply(void* instance, void* renderContext, double time) {
    ColorMixEffectData* data = (ColorMixEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->tint_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->tint_u_texture, 0);
    glUniform1f(ctx->tint_u_amount, (float)evaluate_animation(&data->amount, time));
    glUniform4f(ctx->tint_u_color,
                (float)evaluate_animation(&data->color[0], time),
                (float)evaluate_animation(&data->color[1], time),
                (float)evaluate_animation(&data->color[2], time),
                (float)evaluate_animation(&data->color[3], time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void fill_apply(void* instance, void* renderContext, double time) {
    ColorMixEffectData* data = (ColorMixEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->fill_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->fill_u_texture, 0);
    glUniform1f(ctx->fill_u_amount, (float)evaluate_animation(&data->amount, time));
    glUniform4f(ctx->fill_u_color,
                (float)evaluate_animation(&data->color[0], time),
                (float)evaluate_animation(&data->color[1], time),
                (float)evaluate_animation(&data->color[2], time),
                (float)evaluate_animation(&data->color[3], time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void* brightness_contrast_create(Allocator* a, Value* params, i32 param_count) {
    BrightnessContrastEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, BrightnessContrastEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->brightness, a, 0.0);
    init_animation(&data->contrast, a, 1.0);
    return data;
}

static void brightness_contrast_apply(void* instance, void* renderContext, double time) {
    BrightnessContrastEffectData* data = (BrightnessContrastEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->brightness_contrast_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->brightness_u_texture, 0);
    glUniform1f(ctx->brightness_u_brightness, (float)evaluate_animation(&data->brightness, time));
    glUniform1f(ctx->brightness_u_contrast, (float)evaluate_animation(&data->contrast, time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void brightness_contrast_destroy(Allocator* a, void* instance) {
    BrightnessContrastEffectData* data = (BrightnessContrastEffectData*)instance;
    free_animation(&data->brightness, a);
    free_animation(&data->contrast, a);
    MEM_FREE(a, BrightnessContrastEffectData, data);
}

static bool brightness_contrast_set_number(void* instance, const char* key, double value) {
    BrightnessContrastEffectData* data = (BrightnessContrastEffectData*)instance;
    if (strcmp(key, "brightness") == 0) { data->brightness.default_value = value; return true; }
    if (strcmp(key, "contrast") == 0) { data->contrast.default_value = value; return true; }
    return false;
}

static bool brightness_contrast_get_number(void* instance, const char* key, double* out_value) {
    BrightnessContrastEffectData* data = (BrightnessContrastEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "brightness") == 0) { *out_value = data->brightness.default_value; return true; }
    if (strcmp(key, "contrast") == 0) { *out_value = data->contrast.default_value; return true; }
    return false;
}

static void* blur_create(Allocator* a, Value* params, i32 param_count) {
    BlurEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, BlurEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->radius, a, 0.0);
    return data;
}

static void blur_apply(void* instance, void* renderContext, double time) {
    BlurEffectData* data = (BlurEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->blur_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->blur_u_texture, 0);
    glUniform2f(ctx->blur_u_texel_size,
                1.0f / (float)ctx->width, 1.0f / (float)ctx->height);
    glUniform1f(ctx->blur_u_radius, (float)evaluate_animation(&data->radius, time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void blur_destroy(Allocator* a, void* instance) {
    BlurEffectData* data = (BlurEffectData*)instance;
    free_animation(&data->radius, a);
    MEM_FREE(a, BlurEffectData, data);
}

static bool blur_set_number(void* instance, const char* key, double value) {
    BlurEffectData* data = (BlurEffectData*)instance;
    if (strcmp(key, "radius") != 0) return false;
    data->radius.default_value = fmax(0.0, value);
    return true;
}

static bool blur_get_number(void* instance, const char* key, double* out_value) {
    BlurEffectData* data = (BlurEffectData*)instance;
    if (!out_value || strcmp(key, "radius") != 0) return false;
    *out_value = data->radius.default_value;
    return true;
}

static void* mosaic_create(Allocator* a, Value* params, i32 param_count) {
    MosaicEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, MosaicEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->block_size, a, 8.0);
    data->sharp_colors = false;
    return data;
}

static void mosaic_apply(void* instance, void* renderContext, double time) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    ctx->prefer_nearest_output = data->sharp_colors;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->mosaic_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, data->sharp_colors ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, data->sharp_colors ? GL_NEAREST : GL_LINEAR);
    glUniform1i(ctx->mosaic_u_texture, 0);
    glUniform1f(ctx->mosaic_u_block_size, (float)evaluate_animation(&data->block_size, time));
    glUniform2f(ctx->mosaic_u_resolution, (float)ctx->width, (float)ctx->height);
    glUniform1i(ctx->mosaic_u_sharp_colors, data->sharp_colors ? 1 : 0);
    draw_fullscreen_quad(ctx->quad_vao);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    end_effect_pass();
}

static void mosaic_destroy(Allocator* a, void* instance) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    free_animation(&data->block_size, a);
    MEM_FREE(a, MosaicEffectData, data);
}

static bool mosaic_set_number(void* instance, const char* key, double value) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    if (strcmp(key, "blockSize") != 0) return false;
    data->block_size.default_value = fmax(1.0, value);
    return true;
}

static bool mosaic_get_number(void* instance, const char* key, double* out_value) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    if (!out_value || strcmp(key, "blockSize") != 0) return false;
    *out_value = data->block_size.default_value;
    return true;
}

static bool mosaic_set_bool(void* instance, const char* key, bool value) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    if (strcmp(key, "sharpColors") != 0) return false;
    data->sharp_colors = value;
    return true;
}

static bool mosaic_get_bool(void* instance, const char* key, bool* out_value) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    if (!out_value || strcmp(key, "sharpColors") != 0) return false;
    *out_value = data->sharp_colors;
    return true;
}

static void* grid_create(Allocator* a, Value* params, i32 param_count) {
    GridEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, GridEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->size_x, a, 10.0);
    init_animation(&data->size_y, a, 10.0);
    init_animation(&data->line_width, a, 0.04);
    init_animation(&data->opacity, a, 1.0);
    for (int i = 0; i < 4; i++) init_animation(&data->color[i], a, 1.0);
    return data;
}

static void grid_apply(void* instance, void* renderContext, double time) {
    GridEffectData* data = (GridEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->grid_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->grid_u_texture, 0);
    glUniform2f(ctx->grid_u_size,
                (float)evaluate_animation(&data->size_x, time),
                (float)evaluate_animation(&data->size_y, time));
    glUniform1f(ctx->grid_u_line_width, (float)evaluate_animation(&data->line_width, time));
    glUniform1f(ctx->grid_u_opacity, (float)evaluate_animation(&data->opacity, time));
    glUniform4f(ctx->grid_u_color,
                (float)evaluate_animation(&data->color[0], time),
                (float)evaluate_animation(&data->color[1], time),
                (float)evaluate_animation(&data->color[2], time),
                (float)evaluate_animation(&data->color[3], time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void grid_destroy(Allocator* a, void* instance) {
    GridEffectData* data = (GridEffectData*)instance;
    free_animation(&data->size_x, a);
    free_animation(&data->size_y, a);
    free_animation(&data->line_width, a);
    free_animation(&data->opacity, a);
    for (int i = 0; i < 4; i++) free_animation(&data->color[i], a);
    MEM_FREE(a, GridEffectData, data);
}

static bool grid_set_number(void* instance, const char* key, double value) {
    GridEffectData* data = (GridEffectData*)instance;
    if (strcmp(key, "sizeX") == 0) { data->size_x.default_value = fmax(1.0, value); return true; }
    if (strcmp(key, "sizeY") == 0) { data->size_y.default_value = fmax(1.0, value); return true; }
    if (strcmp(key, "lineWidth") == 0) { data->line_width.default_value = fmax(0.001, value); return true; }
    if (strcmp(key, "opacity") == 0) { data->opacity.default_value = fmax(0.0, fmin(1.0, value)); return true; }
    return false;
}

static bool grid_get_number(void* instance, const char* key, double* out_value) {
    GridEffectData* data = (GridEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "sizeX") == 0) { *out_value = data->size_x.default_value; return true; }
    if (strcmp(key, "sizeY") == 0) { *out_value = data->size_y.default_value; return true; }
    if (strcmp(key, "lineWidth") == 0) { *out_value = data->line_width.default_value; return true; }
    if (strcmp(key, "opacity") == 0) { *out_value = data->opacity.default_value; return true; }
    return false;
}

static bool grid_set_color(void* instance, const char* key, double r, double g, double b, double a) {
    return set_color_array(((GridEffectData*)instance)->color, key, "color", r, g, b, a);
}

static void* gradient_ramp_create(Allocator* a, Value* params, i32 param_count) {
    GradientRampEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, GradientRampEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->start[0], a, 0.0);
    init_animation(&data->start[1], a, 0.0);
    init_animation(&data->end[0], a, 1.0);
    init_animation(&data->end[1], a, 1.0);
    init_animation(&data->blend, a, 1.0);
    for (int i = 0; i < 4; i++) init_animation(&data->start_color[i], a, 1.0);
    init_animation(&data->end_color[0], a, 0.0);
    init_animation(&data->end_color[1], a, 0.0);
    init_animation(&data->end_color[2], a, 0.0);
    init_animation(&data->end_color[3], a, 1.0);
    return data;
}

static void gradient_ramp_apply(void* instance, void* renderContext, double time) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->gradient_ramp_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->gradient_u_texture, 0);
    glUniform2f(ctx->gradient_u_start,
                (float)evaluate_animation(&data->start[0], time),
                (float)evaluate_animation(&data->start[1], time));
    glUniform2f(ctx->gradient_u_end,
                (float)evaluate_animation(&data->end[0], time),
                (float)evaluate_animation(&data->end[1], time));
    glUniform4f(ctx->gradient_u_start_color,
                (float)evaluate_animation(&data->start_color[0], time),
                (float)evaluate_animation(&data->start_color[1], time),
                (float)evaluate_animation(&data->start_color[2], time),
                (float)evaluate_animation(&data->start_color[3], time));
    glUniform4f(ctx->gradient_u_end_color,
                (float)evaluate_animation(&data->end_color[0], time),
                (float)evaluate_animation(&data->end_color[1], time),
                (float)evaluate_animation(&data->end_color[2], time),
                (float)evaluate_animation(&data->end_color[3], time));
    glUniform1f(ctx->gradient_u_blend, (float)evaluate_animation(&data->blend, time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void gradient_ramp_destroy(Allocator* a, void* instance) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    for (int i = 0; i < 2; i++) {
        free_animation(&data->start[i], a);
        free_animation(&data->end[i], a);
    }
    for (int i = 0; i < 4; i++) {
        free_animation(&data->start_color[i], a);
        free_animation(&data->end_color[i], a);
    }
    free_animation(&data->blend, a);
    MEM_FREE(a, GradientRampEffectData, data);
}

static bool gradient_ramp_set_number(void* instance, const char* key, double value) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    if (strcmp(key, "startX") == 0) { data->start[0].default_value = value; return true; }
    if (strcmp(key, "startY") == 0) { data->start[1].default_value = value; return true; }
    if (strcmp(key, "endX") == 0) { data->end[0].default_value = value; return true; }
    if (strcmp(key, "endY") == 0) { data->end[1].default_value = value; return true; }
    if (strcmp(key, "blend") == 0) { data->blend.default_value = fmax(0.0, fmin(1.0, value)); return true; }
    return false;
}

static bool gradient_ramp_get_number(void* instance, const char* key, double* out_value) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "startX") == 0) { *out_value = data->start[0].default_value; return true; }
    if (strcmp(key, "startY") == 0) { *out_value = data->start[1].default_value; return true; }
    if (strcmp(key, "endX") == 0) { *out_value = data->end[0].default_value; return true; }
    if (strcmp(key, "endY") == 0) { *out_value = data->end[1].default_value; return true; }
    if (strcmp(key, "blend") == 0) { *out_value = data->blend.default_value; return true; }
    return false;
}

static bool gradient_ramp_set_color(void* instance, const char* key, double r, double g, double b, double a) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    if (strcmp(key, "startColor") == 0) return set_color_array(data->start_color, key, "startColor", r, g, b, a);
    if (strcmp(key, "endColor") == 0) return set_color_array(data->end_color, key, "endColor", r, g, b, a);
    return false;
}

static void* fractal_noise_create(Allocator* a, Value* params, i32 param_count) {
    FractalNoiseEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, FractalNoiseEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->scale, a, 120.0);
    init_animation(&data->evolution, a, 0.0);
    init_animation(&data->contrast, a, 1.0);
    init_animation(&data->brightness, a, 0.0);
    init_animation(&data->octaves, a, 4.0);
    init_animation(&data->amount, a, 1.0);
    init_animation(&data->offset_x, a, 0.0);
    init_animation(&data->offset_y, a, 0.0);
    data->invert = false;
    return data;
}

static void fractal_noise_apply(void* instance, void* renderContext, double time) {
    FractalNoiseEffectData* data = (FractalNoiseEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->fractal_noise_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->fractal_u_texture, 0);
    glUniform2f(ctx->fractal_u_resolution, (float)ctx->width, (float)ctx->height);
    glUniform1f(ctx->fractal_u_scale, (float)evaluate_animation(&data->scale, time));
    glUniform1f(ctx->fractal_u_evolution, (float)evaluate_animation(&data->evolution, time));
    glUniform1f(ctx->fractal_u_contrast, (float)evaluate_animation(&data->contrast, time));
    glUniform1f(ctx->fractal_u_brightness, (float)evaluate_animation(&data->brightness, time));
    glUniform1f(ctx->fractal_u_octaves, (float)evaluate_animation(&data->octaves, time));
    glUniform1f(ctx->fractal_u_amount, (float)evaluate_animation(&data->amount, time));
    glUniform2f(ctx->fractal_u_offset,
                (float)evaluate_animation(&data->offset_x, time),
                (float)evaluate_animation(&data->offset_y, time));
    glUniform1i(ctx->fractal_u_invert, data->invert ? 1 : 0);
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void fractal_noise_destroy(Allocator* a, void* instance) {
    FractalNoiseEffectData* data = (FractalNoiseEffectData*)instance;
    free_animation(&data->scale, a);
    free_animation(&data->evolution, a);
    free_animation(&data->contrast, a);
    free_animation(&data->brightness, a);
    free_animation(&data->octaves, a);
    free_animation(&data->amount, a);
    free_animation(&data->offset_x, a);
    free_animation(&data->offset_y, a);
    MEM_FREE(a, FractalNoiseEffectData, data);
}

static bool fractal_noise_set_number(void* instance, const char* key, double value) {
    FractalNoiseEffectData* data = (FractalNoiseEffectData*)instance;
    if (strcmp(key, "scale") == 0) { data->scale.default_value = fmax(1.0, value); return true; }
    if (strcmp(key, "evolution") == 0) { data->evolution.default_value = value; return true; }
    if (strcmp(key, "contrast") == 0) { data->contrast.default_value = fmax(0.0, value); return true; }
    if (strcmp(key, "brightness") == 0) { data->brightness.default_value = value; return true; }
    if (strcmp(key, "octaves") == 0) { data->octaves.default_value = fmax(1.0, fmin(6.0, value)); return true; }
    if (strcmp(key, "amount") == 0) { data->amount.default_value = fmax(0.0, fmin(1.0, value)); return true; }
    if (strcmp(key, "offsetX") == 0) { data->offset_x.default_value = value; return true; }
    if (strcmp(key, "offsetY") == 0) { data->offset_y.default_value = value; return true; }
    return false;
}

static bool fractal_noise_get_number(void* instance, const char* key, double* out_value) {
    FractalNoiseEffectData* data = (FractalNoiseEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "scale") == 0) { *out_value = data->scale.default_value; return true; }
    if (strcmp(key, "evolution") == 0) { *out_value = data->evolution.default_value; return true; }
    if (strcmp(key, "contrast") == 0) { *out_value = data->contrast.default_value; return true; }
    if (strcmp(key, "brightness") == 0) { *out_value = data->brightness.default_value; return true; }
    if (strcmp(key, "octaves") == 0) { *out_value = data->octaves.default_value; return true; }
    if (strcmp(key, "amount") == 0) { *out_value = data->amount.default_value; return true; }
    if (strcmp(key, "offsetX") == 0) { *out_value = data->offset_x.default_value; return true; }
    if (strcmp(key, "offsetY") == 0) { *out_value = data->offset_y.default_value; return true; }
    return false;
}

static bool fractal_noise_set_bool(void* instance, const char* key, bool value) {
    FractalNoiseEffectData* data = (FractalNoiseEffectData*)instance;
    if (strcmp(key, "invert") != 0) return false;
    data->invert = value;
    return true;
}

static bool fractal_noise_get_bool(void* instance, const char* key, bool* out_value) {
    FractalNoiseEffectData* data = (FractalNoiseEffectData*)instance;
    if (!out_value || strcmp(key, "invert") != 0) return false;
    *out_value = data->invert;
    return true;
}

static void* displacement_map_create(Allocator* a, Value* params, i32 param_count) {
    DisplacementMapEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, DisplacementMapEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->scale_x, a, 20.0);
    init_animation(&data->scale_y, a, 20.0);
    init_animation(&data->amount, a, 1.0);
    init_animation(&data->offset_x, a, 0.0);
    init_animation(&data->offset_y, a, 0.0);
    data->use_luma = true;
    data->source_clip_id = 0;
    return data;
}

static void displacement_map_apply(void* instance, void* renderContext, double time) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    GLuint map_texture = ctx->has_auxiliary_texture ? ctx->auxiliary_texture : ctx->input_texture;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->displacement_map_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->displacement_u_texture, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, map_texture);
    glUniform1i(ctx->displacement_u_map_texture, 1);
    glUniform2f(ctx->displacement_u_resolution, (float)ctx->width, (float)ctx->height);
    glUniform1f(ctx->displacement_u_scale_x, (float)evaluate_animation(&data->scale_x, time));
    glUniform1f(ctx->displacement_u_scale_y, (float)evaluate_animation(&data->scale_y, time));
    glUniform1f(ctx->displacement_u_amount, (float)evaluate_animation(&data->amount, time));
    glUniform2f(ctx->displacement_u_offset,
                (float)evaluate_animation(&data->offset_x, time),
                (float)evaluate_animation(&data->offset_y, time));
    glUniform1i(ctx->displacement_u_use_luma, data->use_luma ? 1 : 0);
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void displacement_map_destroy(Allocator* a, void* instance) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    free_animation(&data->scale_x, a);
    free_animation(&data->scale_y, a);
    free_animation(&data->amount, a);
    free_animation(&data->offset_x, a);
    free_animation(&data->offset_y, a);
    MEM_FREE(a, DisplacementMapEffectData, data);
}

static bool displacement_map_set_number(void* instance, const char* key, double value) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    if (strcmp(key, "scaleX") == 0) { data->scale_x.default_value = value; return true; }
    if (strcmp(key, "scaleY") == 0) { data->scale_y.default_value = value; return true; }
    if (strcmp(key, "amount") == 0) { data->amount.default_value = fmax(0.0, value); return true; }
    if (strcmp(key, "offsetX") == 0) { data->offset_x.default_value = value; return true; }
    if (strcmp(key, "offsetY") == 0) { data->offset_y.default_value = value; return true; }
    return false;
}

static bool displacement_map_get_number(void* instance, const char* key, double* out_value) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "scaleX") == 0) { *out_value = data->scale_x.default_value; return true; }
    if (strcmp(key, "scaleY") == 0) { *out_value = data->scale_y.default_value; return true; }
    if (strcmp(key, "amount") == 0) { *out_value = data->amount.default_value; return true; }
    if (strcmp(key, "offsetX") == 0) { *out_value = data->offset_x.default_value; return true; }
    if (strcmp(key, "offsetY") == 0) { *out_value = data->offset_y.default_value; return true; }
    return false;
}

static bool displacement_map_set_bool(void* instance, const char* key, bool value) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    if (strcmp(key, "useLuma") != 0) return false;
    data->use_luma = value;
    return true;
}

static bool displacement_map_get_bool(void* instance, const char* key, bool* out_value) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    if (!out_value || strcmp(key, "useLuma") != 0) return false;
    *out_value = data->use_luma;
    return true;
}

static bool displacement_map_set_source_clip(void* instance, u32 clip_id) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    data->source_clip_id = clip_id;
    return true;
}

static u32 displacement_map_get_source_clip(void* instance) {
    return ((DisplacementMapEffectData*)instance)->source_clip_id;
}

static void* posterize_create(Allocator* a, Value* params, i32 param_count) {
    PosterizeEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, PosterizeEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->levels, a, 6.0);
    init_animation(&data->amount, a, 1.0);
    return data;
}

static void posterize_apply(void* instance, void* renderContext, double time) {
    PosterizeEffectData* data = (PosterizeEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->posterize_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->posterize_u_texture, 0);
    glUniform1f(ctx->posterize_u_levels, (float)evaluate_animation(&data->levels, time));
    glUniform1f(ctx->posterize_u_amount, (float)evaluate_animation(&data->amount, time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void posterize_destroy(Allocator* a, void* instance) {
    PosterizeEffectData* data = (PosterizeEffectData*)instance;
    free_animation(&data->levels, a);
    free_animation(&data->amount, a);
    MEM_FREE(a, PosterizeEffectData, data);
}

static bool posterize_set_number(void* instance, const char* key, double value) {
    PosterizeEffectData* data = (PosterizeEffectData*)instance;
    if (strcmp(key, "levels") == 0) { data->levels.default_value = fmax(2.0, value); return true; }
    if (strcmp(key, "amount") == 0) { data->amount.default_value = fmax(0.0, fmin(1.0, value)); return true; }
    return false;
}

static bool posterize_get_number(void* instance, const char* key, double* out_value) {
    PosterizeEffectData* data = (PosterizeEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "levels") == 0) { *out_value = data->levels.default_value; return true; }
    if (strcmp(key, "amount") == 0) { *out_value = data->amount.default_value; return true; }
    return false;
}

static void* slider_control_create(Allocator* a, Value* params, i32 param_count) {
    SingleNumberControlEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, SingleNumberControlEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->value, a, 0.0);
    return data;
}

static void slider_control_apply(void* instance, void* renderContext, double time) {
    (void)instance;
    (void)time;
    copy_input_to_output((EffectRenderContext*)renderContext);
}

static void single_number_control_destroy(Allocator* a, void* instance) {
    SingleNumberControlEffectData* data = (SingleNumberControlEffectData*)instance;
    free_animation(&data->value, a);
    MEM_FREE(a, SingleNumberControlEffectData, data);
}

static bool single_number_control_set_number(void* instance, const char* key, double value) {
    SingleNumberControlEffectData* data = (SingleNumberControlEffectData*)instance;
    if (strcmp(key, "value") != 0 && strcmp(key, "angle") != 0) return false;
    data->value.default_value = value;
    return true;
}

static bool single_number_control_get_number(void* instance, const char* key, double* out_value) {
    SingleNumberControlEffectData* data = (SingleNumberControlEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "value") != 0 && strcmp(key, "angle") != 0) return false;
    *out_value = data->value.default_value;
    return true;
}

static void* checkbox_control_create(Allocator* a, Value* params, i32 param_count) {
    CheckboxControlEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, CheckboxControlEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->value, a, 0.0);
    return data;
}

static void checkbox_control_apply(void* instance, void* renderContext, double time) {
    (void)instance;
    (void)time;
    copy_input_to_output((EffectRenderContext*)renderContext);
}

static void checkbox_control_destroy(Allocator* a, void* instance) {
    CheckboxControlEffectData* data = (CheckboxControlEffectData*)instance;
    free_animation(&data->value, a);
    MEM_FREE(a, CheckboxControlEffectData, data);
}

static bool checkbox_control_set_number(void* instance, const char* key, double value) {
    CheckboxControlEffectData* data = (CheckboxControlEffectData*)instance;
    if (strcmp(key, "value") != 0) return false;
    data->value.default_value = value >= 0.5 ? 1.0 : 0.0;
    return true;
}

static bool checkbox_control_get_number(void* instance, const char* key, double* out_value) {
    CheckboxControlEffectData* data = (CheckboxControlEffectData*)instance;
    if (!out_value || strcmp(key, "value") != 0) return false;
    *out_value = data->value.default_value;
    return true;
}

static bool checkbox_control_set_bool(void* instance, const char* key, bool value) {
    CheckboxControlEffectData* data = (CheckboxControlEffectData*)instance;
    if (strcmp(key, "value") != 0) return false;
    data->value.default_value = value ? 1.0 : 0.0;
    return true;
}

static bool checkbox_control_get_bool(void* instance, const char* key, bool* out_value) {
    CheckboxControlEffectData* data = (CheckboxControlEffectData*)instance;
    if (!out_value || strcmp(key, "value") != 0) return false;
    *out_value = data->value.default_value >= 0.5;
    return true;
}

static void* point_control_create(Allocator* a, Value* params, i32 param_count) {
    PointControlEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, PointControlEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->x, a, 0.0);
    init_animation(&data->y, a, 0.0);
    return data;
}

static void point_control_apply(void* instance, void* renderContext, double time) {
    (void)instance;
    (void)time;
    copy_input_to_output((EffectRenderContext*)renderContext);
}

static void point_control_destroy(Allocator* a, void* instance) {
    PointControlEffectData* data = (PointControlEffectData*)instance;
    free_animation(&data->x, a);
    free_animation(&data->y, a);
    MEM_FREE(a, PointControlEffectData, data);
}

static bool point_control_set_number(void* instance, const char* key, double value) {
    PointControlEffectData* data = (PointControlEffectData*)instance;
    if (strcmp(key, "x") == 0) { data->x.default_value = value; return true; }
    if (strcmp(key, "y") == 0) { data->y.default_value = value; return true; }
    return false;
}

static bool point_control_get_number(void* instance, const char* key, double* out_value) {
    PointControlEffectData* data = (PointControlEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "x") == 0) { *out_value = data->x.default_value; return true; }
    if (strcmp(key, "y") == 0) { *out_value = data->y.default_value; return true; }
    return false;
}

static void* color_control_create(Allocator* a, Value* params, i32 param_count) {
    ColorControlEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, ColorControlEffectData, 1);
    if (!data) return NULL;
    for (int i = 0; i < 4; i++) init_animation(&data->color[i], a, 1.0);
    return data;
}

static void color_control_apply(void* instance, void* renderContext, double time) {
    (void)instance;
    (void)time;
    copy_input_to_output((EffectRenderContext*)renderContext);
}

static void color_control_destroy(Allocator* a, void* instance) {
    ColorControlEffectData* data = (ColorControlEffectData*)instance;
    for (int i = 0; i < 4; i++) free_animation(&data->color[i], a);
    MEM_FREE(a, ColorControlEffectData, data);
}

static bool color_control_set_color(void* instance, const char* key, double r, double g, double b, double a) {
    return set_color_array(((ColorControlEffectData*)instance)->color, key, "color", r, g, b, a);
}

static Animation* color_mix_get_number_animation(void* instance, const char* key) {
    ColorMixEffectData* data = (ColorMixEffectData*)instance;
    if (strcmp(key, "amount") == 0) return &data->amount;
    return NULL;
}

static bool color_mix_get_color_animations_cb(void* instance, const char* key,
                                              Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return get_color_mix_animation((ColorMixEffectData*)instance, key, out_r, out_g, out_b, out_a);
}

static Animation* brightness_contrast_get_number_animation(void* instance, const char* key) {
    BrightnessContrastEffectData* data = (BrightnessContrastEffectData*)instance;
    if (strcmp(key, "brightness") == 0) return &data->brightness;
    if (strcmp(key, "contrast") == 0) return &data->contrast;
    return NULL;
}

static Animation* blur_get_number_animation(void* instance, const char* key) {
    BlurEffectData* data = (BlurEffectData*)instance;
    if (strcmp(key, "radius") == 0) return &data->radius;
    return NULL;
}

static Animation* mosaic_get_number_animation(void* instance, const char* key) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    if (strcmp(key, "blockSize") == 0) return &data->block_size;
    return NULL;
}

static Animation* grid_get_number_animation(void* instance, const char* key) {
    GridEffectData* data = (GridEffectData*)instance;
    if (strcmp(key, "sizeX") == 0) return &data->size_x;
    if (strcmp(key, "sizeY") == 0) return &data->size_y;
    if (strcmp(key, "lineWidth") == 0) return &data->line_width;
    if (strcmp(key, "opacity") == 0) return &data->opacity;
    return NULL;
}

static bool grid_get_color_animations_cb(void* instance, const char* key,
                                         Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return resolve_color_array(((GridEffectData*)instance)->color, key, "color", out_r, out_g, out_b, out_a);
}

static Animation* gradient_ramp_get_number_animation(void* instance, const char* key) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    if (strcmp(key, "startX") == 0) return &data->start[0];
    if (strcmp(key, "startY") == 0) return &data->start[1];
    if (strcmp(key, "endX") == 0) return &data->end[0];
    if (strcmp(key, "endY") == 0) return &data->end[1];
    if (strcmp(key, "blend") == 0) return &data->blend;
    return NULL;
}

static bool gradient_ramp_get_color_animations_cb(void* instance, const char* key,
                                                  Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    if (strcmp(key, "startColor") == 0) return resolve_color_array(data->start_color, key, "startColor", out_r, out_g, out_b, out_a);
    if (strcmp(key, "endColor") == 0) return resolve_color_array(data->end_color, key, "endColor", out_r, out_g, out_b, out_a);
    return false;
}

static Animation* fractal_noise_get_number_animation(void* instance, const char* key) {
    FractalNoiseEffectData* data = (FractalNoiseEffectData*)instance;
    if (strcmp(key, "scale") == 0) return &data->scale;
    if (strcmp(key, "evolution") == 0) return &data->evolution;
    if (strcmp(key, "contrast") == 0) return &data->contrast;
    if (strcmp(key, "brightness") == 0) return &data->brightness;
    if (strcmp(key, "octaves") == 0) return &data->octaves;
    if (strcmp(key, "amount") == 0) return &data->amount;
    if (strcmp(key, "offsetX") == 0) return &data->offset_x;
    if (strcmp(key, "offsetY") == 0) return &data->offset_y;
    return NULL;
}

static Animation* displacement_map_get_number_animation(void* instance, const char* key) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    if (strcmp(key, "scaleX") == 0) return &data->scale_x;
    if (strcmp(key, "scaleY") == 0) return &data->scale_y;
    if (strcmp(key, "amount") == 0) return &data->amount;
    if (strcmp(key, "offsetX") == 0) return &data->offset_x;
    if (strcmp(key, "offsetY") == 0) return &data->offset_y;
    return NULL;
}

static Animation* posterize_get_number_animation(void* instance, const char* key) {
    PosterizeEffectData* data = (PosterizeEffectData*)instance;
    if (strcmp(key, "levels") == 0) return &data->levels;
    if (strcmp(key, "amount") == 0) return &data->amount;
    return NULL;
}

static Animation* slider_control_get_number_animation(void* instance, const char* key) {
    SingleNumberControlEffectData* data = (SingleNumberControlEffectData*)instance;
    if (strcmp(key, "value") == 0 || strcmp(key, "angle") == 0) return &data->value;
    return NULL;
}

static Animation* checkbox_control_get_number_animation(void* instance, const char* key) {
    CheckboxControlEffectData* data = (CheckboxControlEffectData*)instance;
    if (strcmp(key, "value") == 0) return &data->value;
    return NULL;
}

static Animation* point_control_get_number_animation(void* instance, const char* key) {
    PointControlEffectData* data = (PointControlEffectData*)instance;
    if (strcmp(key, "x") == 0) return &data->x;
    if (strcmp(key, "y") == 0) return &data->y;
    return NULL;
}

static bool color_control_get_color_animations_cb(void* instance, const char* key,
                                                  Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return resolve_color_array(((ColorControlEffectData*)instance)->color, key, "color", out_r, out_g, out_b, out_a);
}

static const EffectProcessor TINT_PROCESSOR = {
    "Tint", color_mix_create, tint_apply, color_mix_destroy, NULL,
    color_mix_set_number, color_mix_get_number, NULL, NULL, color_mix_set_color, NULL, 0,
    color_mix_get_number_animation, color_mix_get_color_animations_cb
};

static const EffectProcessor FILL_PROCESSOR = {
    "Fill", color_mix_create, fill_apply, color_mix_destroy, NULL,
    color_mix_set_number, color_mix_get_number, NULL, NULL, color_mix_set_color, NULL, 0,
    color_mix_get_number_animation, color_mix_get_color_animations_cb
};

static const EffectProcessor BRIGHTNESS_CONTRAST_PROCESSOR = {
    "BrightnessContrast", brightness_contrast_create, brightness_contrast_apply, brightness_contrast_destroy, NULL,
    brightness_contrast_set_number, brightness_contrast_get_number, NULL, NULL, NULL, NULL, 0,
    brightness_contrast_get_number_animation, NULL
};

static const EffectProcessor BLUR_PROCESSOR = {
    "Blur", blur_create, blur_apply, blur_destroy, NULL,
    blur_set_number, blur_get_number, NULL, NULL, NULL, NULL, 0,
    blur_get_number_animation, NULL
};

static const EffectProcessor MOSAIC_PROCESSOR = {
    "Mosaic", mosaic_create, mosaic_apply, mosaic_destroy, NULL,
    mosaic_set_number, mosaic_get_number, mosaic_set_bool, mosaic_get_bool, NULL, NULL, 0,
    mosaic_get_number_animation, NULL
};

static const EffectProcessor GRID_PROCESSOR = {
    "Grid", grid_create, grid_apply, grid_destroy, NULL,
    grid_set_number, grid_get_number, NULL, NULL, grid_set_color, NULL, 0,
    grid_get_number_animation, grid_get_color_animations_cb
};

static const EffectProcessor GRADIENT_RAMP_PROCESSOR = {
    "GradientRamp", gradient_ramp_create, gradient_ramp_apply, gradient_ramp_destroy, NULL,
    gradient_ramp_set_number, gradient_ramp_get_number, NULL, NULL, gradient_ramp_set_color, NULL, 0,
    gradient_ramp_get_number_animation, gradient_ramp_get_color_animations_cb
};

static const EffectProcessor FRACTAL_NOISE_PROCESSOR = {
    "FractalNoise", fractal_noise_create, fractal_noise_apply, fractal_noise_destroy, NULL,
    fractal_noise_set_number, fractal_noise_get_number, fractal_noise_set_bool, fractal_noise_get_bool, NULL, NULL, 0,
    fractal_noise_get_number_animation, NULL
};

static const EffectProcessor DISPLACEMENT_MAP_PROCESSOR = {
    "DisplacementMap", displacement_map_create, displacement_map_apply, displacement_map_destroy, NULL,
    displacement_map_set_number, displacement_map_get_number, displacement_map_set_bool, displacement_map_get_bool, NULL,
    displacement_map_set_source_clip, displacement_map_get_source_clip,
    displacement_map_get_number_animation, NULL
};

static const EffectProcessor POSTERIZE_PROCESSOR = {
    "Posterize", posterize_create, posterize_apply, posterize_destroy, NULL,
    posterize_set_number, posterize_get_number, NULL, NULL, NULL, NULL, 0,
    posterize_get_number_animation, NULL
};

static const EffectProcessor SLIDER_CONTROL_PROCESSOR = {
    "SliderControl", slider_control_create, slider_control_apply, single_number_control_destroy, NULL,
    single_number_control_set_number, single_number_control_get_number, NULL, NULL, NULL, NULL, 0,
    slider_control_get_number_animation, NULL
};

static const EffectProcessor ANGLE_CONTROL_PROCESSOR = {
    "AngleControl", slider_control_create, slider_control_apply, single_number_control_destroy, NULL,
    single_number_control_set_number, single_number_control_get_number, NULL, NULL, NULL, NULL, 0,
    slider_control_get_number_animation, NULL
};

static const EffectProcessor CHECKBOX_CONTROL_PROCESSOR = {
    "CheckboxControl", checkbox_control_create, checkbox_control_apply, checkbox_control_destroy, NULL,
    checkbox_control_set_number, checkbox_control_get_number, checkbox_control_set_bool, checkbox_control_get_bool, NULL, NULL, 0,
    checkbox_control_get_number_animation, NULL
};

static const EffectProcessor POINT_CONTROL_PROCESSOR = {
    "PointControl", point_control_create, point_control_apply, point_control_destroy, NULL,
    point_control_set_number, point_control_get_number, NULL, NULL, NULL, NULL, 0,
    point_control_get_number_animation, NULL
};

static const EffectProcessor COLOR_CONTROL_PROCESSOR = {
    "ColorControl", color_control_create, color_control_apply, color_control_destroy, NULL,
    NULL, NULL, NULL, NULL, color_control_set_color, NULL, 0,
    NULL, color_control_get_color_animations_cb
};

void effect_register_builtin_processors(void) {
    effect_registry_register(&TINT_PROCESSOR);
    effect_registry_register(&FILL_PROCESSOR);
    effect_registry_register(&BRIGHTNESS_CONTRAST_PROCESSOR);
    effect_registry_register(&BLUR_PROCESSOR);
    effect_registry_register(&MOSAIC_PROCESSOR);
    effect_registry_register(&GRID_PROCESSOR);
    effect_registry_register(&GRADIENT_RAMP_PROCESSOR);
    effect_registry_register(&FRACTAL_NOISE_PROCESSOR);
    effect_registry_register(&DISPLACEMENT_MAP_PROCESSOR);
    effect_registry_register(&POSTERIZE_PROCESSOR);
    effect_registry_register(&SLIDER_CONTROL_PROCESSOR);
    effect_registry_register(&ANGLE_CONTROL_PROCESSOR);
    effect_registry_register(&CHECKBOX_CONTROL_PROCESSOR);
    effect_registry_register(&POINT_CONTROL_PROCESSOR);
    effect_registry_register(&COLOR_CONTROL_PROCESSOR);
}
