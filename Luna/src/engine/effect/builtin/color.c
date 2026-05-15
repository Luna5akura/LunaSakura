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

static void* glow_create(Allocator* a, Value* params, i32 param_count) {
    GlowEffectData* data;
    (void)params;
    (void)param_count;
    data = MEM_ALLOC(a, GlowEffectData, 1);
    if (!data) return NULL;
    init_animation(&data->radius, a, 18.0);
    init_animation(&data->intensity, a, 1.0);
    init_animation(&data->threshold, a, 0.65);
    init_animation(&data->softness, a, 0.2);
    for (int i = 0; i < 4; i++) init_animation(&data->color[i], a, 1.0);
    return data;
}

static void glow_apply(void* instance, void* renderContext, double time) {
    GlowEffectData* data = (GlowEffectData*)instance;
    EffectRenderContext* ctx = (EffectRenderContext*)renderContext;
    begin_effect_pass(ctx, ctx->output_fbo);
    glUseProgram(ctx->glow_shader_program);
    bind_effect_input_texture(ctx->input_texture);
    glUniform1i(ctx->glow_u_texture, 0);
    glUniform2f(ctx->glow_u_texel_size,
                1.0f / (float)ctx->width, 1.0f / (float)ctx->height);
    glUniform1f(ctx->glow_u_radius, (float)evaluate_animation(&data->radius, time));
    glUniform1f(ctx->glow_u_intensity, (float)evaluate_animation(&data->intensity, time));
    glUniform1f(ctx->glow_u_threshold, (float)evaluate_animation(&data->threshold, time));
    glUniform1f(ctx->glow_u_softness, (float)evaluate_animation(&data->softness, time));
    glUniform4f(ctx->glow_u_color,
                (float)evaluate_animation(&data->color[0], time),
                (float)evaluate_animation(&data->color[1], time),
                (float)evaluate_animation(&data->color[2], time),
                (float)evaluate_animation(&data->color[3], time));
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void glow_destroy(Allocator* a, void* instance) {
    GlowEffectData* data = (GlowEffectData*)instance;
    free_animation(&data->radius, a);
    free_animation(&data->intensity, a);
    free_animation(&data->threshold, a);
    free_animation(&data->softness, a);
    for (int i = 0; i < 4; i++) free_animation(&data->color[i], a);
    MEM_FREE(a, GlowEffectData, data);
}

static bool glow_set_number(void* instance, const char* key, double value) {
    GlowEffectData* data = (GlowEffectData*)instance;
    if (strcmp(key, "radius") == 0) { data->radius.default_value = fmax(0.0, value); return true; }
    if (strcmp(key, "intensity") == 0) { data->intensity.default_value = fmax(0.0, value); return true; }
    if (strcmp(key, "threshold") == 0) { data->threshold.default_value = fmax(0.0, fmin(1.0, value)); return true; }
    if (strcmp(key, "softness") == 0) { data->softness.default_value = fmax(0.001, fmin(1.0, value)); return true; }
    return false;
}

static bool glow_get_number(void* instance, const char* key, double* out_value) {
    GlowEffectData* data = (GlowEffectData*)instance;
    if (!out_value) return false;
    if (strcmp(key, "radius") == 0) { *out_value = data->radius.default_value; return true; }
    if (strcmp(key, "intensity") == 0) { *out_value = data->intensity.default_value; return true; }
    if (strcmp(key, "threshold") == 0) { *out_value = data->threshold.default_value; return true; }
    if (strcmp(key, "softness") == 0) { *out_value = data->softness.default_value; return true; }
    return false;
}

static bool glow_set_color(void* instance, const char* key, double r, double g, double b, double a) {
    return set_color_array(((GlowEffectData*)instance)->color, key, "color", r, g, b, a);
}
const EffectProcessor TINT_PROCESSOR = {
    "Tint", color_mix_create, tint_apply, color_mix_destroy, NULL,
    color_mix_set_number, color_mix_get_number, NULL, NULL, color_mix_set_color, NULL, 0,
    color_mix_get_number_animation, color_mix_get_color_animations_cb
};
const EffectProcessor FILL_PROCESSOR = {
    "Fill", color_mix_create, fill_apply, color_mix_destroy, NULL,
    color_mix_set_number, color_mix_get_number, NULL, NULL, color_mix_set_color, NULL, 0,
    color_mix_get_number_animation, color_mix_get_color_animations_cb
};
const EffectProcessor BRIGHTNESS_CONTRAST_PROCESSOR = {
    "BrightnessContrast", brightness_contrast_create, brightness_contrast_apply, brightness_contrast_destroy, NULL,
    brightness_contrast_set_number, brightness_contrast_get_number, NULL, NULL, NULL, NULL, 0,
    brightness_contrast_get_number_animation, NULL
};
const EffectProcessor BLUR_PROCESSOR = {
    "Blur", blur_create, blur_apply, blur_destroy, NULL,
    blur_set_number, blur_get_number, NULL, NULL, NULL, NULL, 0,
    blur_get_number_animation, NULL
};
const EffectProcessor GLOW_PROCESSOR = {
    "Glow", glow_create, glow_apply, glow_destroy, NULL,
    glow_set_number, glow_get_number, NULL, NULL, glow_set_color, NULL, 0,
    glow_get_number_animation, glow_get_color_animations_cb
};
