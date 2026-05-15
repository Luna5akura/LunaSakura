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
    init_animation(&data->horizontal_channel, a, 0.0);
    init_animation(&data->vertical_channel, a, 1.0);
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
    glUniform1i(ctx->displacement_u_horizontal_channel, (int)lround(evaluate_animation(&data->horizontal_channel, time)));
    glUniform1i(ctx->displacement_u_vertical_channel, (int)lround(evaluate_animation(&data->vertical_channel, time)));
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
    free_animation(&data->horizontal_channel, a);
    free_animation(&data->vertical_channel, a);
    MEM_FREE(a, DisplacementMapEffectData, data);
}

static bool displacement_map_set_number(void* instance, const char* key, double value) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    double channel = fmax(0.0, fmin(4.0, round(value)));
    if (strcmp(key, "scaleX") == 0) { data->scale_x.default_value = value; return true; }
    if (strcmp(key, "scaleY") == 0) { data->scale_y.default_value = value; return true; }
    if (strcmp(key, "amount") == 0) { data->amount.default_value = fmax(0.0, value); return true; }
    if (strcmp(key, "offsetX") == 0) { data->offset_x.default_value = value; return true; }
    if (strcmp(key, "offsetY") == 0) { data->offset_y.default_value = value; return true; }
    if (strcmp(key, "horizontalChannel") == 0) { data->horizontal_channel.default_value = channel; return true; }
    if (strcmp(key, "verticalChannel") == 0) { data->vertical_channel.default_value = channel; return true; }
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
    if (strcmp(key, "horizontalChannel") == 0) { *out_value = data->horizontal_channel.default_value; return true; }
    if (strcmp(key, "verticalChannel") == 0) { *out_value = data->vertical_channel.default_value; return true; }
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

const EffectProcessor FRACTAL_NOISE_PROCESSOR = {
    "FractalNoise", fractal_noise_create, fractal_noise_apply, fractal_noise_destroy, NULL,
    fractal_noise_set_number, fractal_noise_get_number, fractal_noise_set_bool, fractal_noise_get_bool, NULL, NULL, 0,
    fractal_noise_get_number_animation, NULL
};
const EffectProcessor DISPLACEMENT_MAP_PROCESSOR = {
    "DisplacementMap", displacement_map_create, displacement_map_apply, displacement_map_destroy, NULL,
    displacement_map_set_number, displacement_map_get_number, displacement_map_set_bool, displacement_map_get_bool, NULL,
    displacement_map_set_source_clip, displacement_map_get_source_clip,
    displacement_map_get_number_animation, NULL
};
const EffectProcessor POSTERIZE_PROCESSOR = {
    "Posterize", posterize_create, posterize_apply, posterize_destroy, NULL,
    posterize_set_number, posterize_get_number, NULL, NULL, NULL, NULL, 0,
    posterize_get_number_animation, NULL
};
