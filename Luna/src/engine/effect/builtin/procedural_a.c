#include "internal.h"
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

const EffectProcessor MOSAIC_PROCESSOR = {
    "Mosaic", mosaic_create, mosaic_apply, mosaic_destroy, NULL,
    mosaic_set_number, mosaic_get_number, mosaic_set_bool, mosaic_get_bool, NULL, NULL, 0,
    mosaic_get_number_animation, NULL
};
const EffectProcessor GRID_PROCESSOR = {
    "Grid", grid_create, grid_apply, grid_destroy, NULL,
    grid_set_number, grid_get_number, NULL, NULL, grid_set_color, NULL, 0,
    grid_get_number_animation, grid_get_color_animations_cb
};
const EffectProcessor GRADIENT_RAMP_PROCESSOR = {
    "GradientRamp", gradient_ramp_create, gradient_ramp_apply, gradient_ramp_destroy, NULL,
    gradient_ramp_set_number, gradient_ramp_get_number, NULL, NULL, gradient_ramp_set_color, NULL, 0,
    gradient_ramp_get_number_animation, gradient_ramp_get_color_animations_cb
};
