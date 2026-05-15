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

static void begin_effect_pass_sized(GLuint fbo, int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static bool create_temp_target(int width, int height, GLuint* out_tex, GLuint* out_fbo) {
    if (!out_tex || !out_fbo) return false;
    *out_tex = 0;
    *out_fbo = 0;
    glGenTextures(1, out_tex);
    glBindTexture(GL_TEXTURE_2D, *out_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, out_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *out_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *out_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        if (*out_fbo) glDeleteFramebuffers(1, out_fbo);
        if (*out_tex) glDeleteTextures(1, out_tex);
        *out_fbo = 0;
        *out_tex = 0;
        return false;
    }
    return true;
}

static void destroy_temp_target(GLuint* tex, GLuint* fbo) {
    if (fbo && *fbo) {
        glDeleteFramebuffers(1, fbo);
        *fbo = 0;
    }
    if (tex && *tex) {
        glDeleteTextures(1, tex);
        *tex = 0;
    }
}

static void copy_texture_to_target(EffectRenderContext* ctx, GLuint input_texture, GLuint output_fbo,
                                   int width, int height, float opacity) {
    begin_effect_pass_sized(output_fbo, width, height);
    glUseProgram(ctx->copy_shader_program);
    bind_effect_input_texture(input_texture);
    glUniform1i(ctx->copy_u_texture, 0);
    glUniform1f(ctx->copy_u_opacity, opacity);
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void additive_blend_texture_to_target(EffectRenderContext* ctx, GLuint input_texture, GLuint output_fbo,
                                             int width, int height, float opacity) {
    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
    glViewport(0, 0, width, height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(ctx->copy_shader_program);
    bind_effect_input_texture(input_texture);
    glUniform1i(ctx->copy_u_texture, 0);
    glUniform1f(ctx->copy_u_opacity, opacity);
    draw_fullscreen_quad(ctx->quad_vao);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void blur_texture_to_target(EffectRenderContext* ctx, GLuint input_texture, GLuint output_fbo,
                                   int width, int height, float radius, float dir_x, float dir_y) {
    begin_effect_pass_sized(output_fbo, width, height);
    glUseProgram(ctx->blur_shader_program);
    bind_effect_input_texture(input_texture);
    glUniform1i(ctx->blur_u_texture, 0);
    glUniform2f(ctx->blur_u_texel_size, 1.0f / (float)width, 1.0f / (float)height);
    glUniform1f(ctx->blur_u_radius, radius);
    glUniform2f(ctx->blur_u_direction, dir_x, dir_y);
    draw_fullscreen_quad(ctx->quad_vao);
    end_effect_pass();
}

static void glow_filter_to_target(EffectRenderContext* ctx, GLuint input_texture,
                                  int source_width, int source_height,
                                  GLuint output_fbo, int target_width, int target_height,
                                  int mode, float radius, float intensity,
                                  float threshold, float softness,
                                  float tint_r, float tint_g, float tint_b, float tint_a,
                                  bool additive) {
    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
    glViewport(0, 0, target_width, target_height);
    if (additive) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glDisable(GL_BLEND);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glUseProgram(ctx->glow_shader_program);
    bind_effect_input_texture(input_texture);
    glUniform1i(ctx->glow_u_texture, 0);
    glUniform2f(ctx->glow_u_texel_size, 1.0f / (float)source_width, 1.0f / (float)source_height);
    glUniform1f(ctx->glow_u_radius, radius);
    glUniform1f(ctx->glow_u_intensity, intensity);
    glUniform1f(ctx->glow_u_threshold, threshold);
    glUniform1f(ctx->glow_u_softness, softness);
    glUniform4f(ctx->glow_u_color, tint_r, tint_g, tint_b, tint_a);
    glUniform1i(ctx->glow_u_mode, mode);
    draw_fullscreen_quad(ctx->quad_vao);

    if (additive) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        end_effect_pass();
    }
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
    GLuint temp_tex = 0;
    GLuint temp_fbo = 0;
    float radius = (float)evaluate_animation(&data->radius, time);
    if (radius <= 0.001f || !create_temp_target(ctx->width, ctx->height, &temp_tex, &temp_fbo)) {
        copy_input_to_output(ctx);
        return;
    }
    blur_texture_to_target(ctx, ctx->input_texture, temp_fbo, ctx->width, ctx->height, radius, 1.0f, 0.0f);
    blur_texture_to_target(ctx, temp_tex, ctx->output_fbo, ctx->width, ctx->height, radius, 0.0f, 1.0f);
    destroy_temp_target(&temp_tex, &temp_fbo);
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
    float radius = (float)evaluate_animation(&data->radius, time);
    float intensity = (float)evaluate_animation(&data->intensity, time);
    float threshold = (float)evaluate_animation(&data->threshold, time);
    float softness = (float)evaluate_animation(&data->softness, time);
    float tint_r = (float)evaluate_animation(&data->color[0], time);
    float tint_g = (float)evaluate_animation(&data->color[1], time);
    float tint_b = (float)evaluate_animation(&data->color[2], time);
    float tint_a = (float)evaluate_animation(&data->color[3], time);
    GLuint levels_tex[6] = {0};
    GLuint levels_fbo[6] = {0};
    int widths[6] = {0};
    int heights[6] = {0};
    int level_count = 0;
    int desired_levels = 0;
    float upsample_radius = 1.0f;

    if (radius <= 0.001f || intensity <= 0.001f) {
        copy_input_to_output(ctx);
        return;
    }

    desired_levels = (int)floorf(log2f(fmaxf(radius, 1.0f))) + 1;
    if (desired_levels < 1) desired_levels = 1;
    if (desired_levels > 6) desired_levels = 6;
    upsample_radius = 0.85f + fminf(radius / 36.0f, 1.15f);

    widths[0] = ctx->width > 1 ? ctx->width / 2 : 1;
    heights[0] = ctx->height > 1 ? ctx->height / 2 : 1;
    while (level_count < desired_levels) {
        if (!create_temp_target(widths[level_count], heights[level_count],
                                &levels_tex[level_count], &levels_fbo[level_count])) {
            level_count = 0;
            break;
        }
        level_count++;
        if (level_count >= desired_levels) break;
        widths[level_count] = widths[level_count - 1] > 1 ? widths[level_count - 1] / 2 : 1;
        heights[level_count] = heights[level_count - 1] > 1 ? heights[level_count - 1] / 2 : 1;
        if (widths[level_count - 1] <= 2 || heights[level_count - 1] <= 2) break;
    }
    if (level_count == 0) {
        copy_input_to_output(ctx);
        return;
    }

    glow_filter_to_target(ctx, ctx->input_texture, ctx->width, ctx->height,
                          levels_fbo[0], widths[0], heights[0],
                          0, 1.0f, 1.0f, threshold, softness,
                          tint_r, tint_g, tint_b, tint_a, false);

    for (int i = 1; i < level_count; i++) {
        glow_filter_to_target(ctx, levels_tex[i - 1], widths[i - 1], heights[i - 1],
                              levels_fbo[i], widths[i], heights[i],
                              1, 1.0f, 1.0f, threshold, softness,
                              tint_r, tint_g, tint_b, tint_a, false);
    }

    for (int i = level_count - 2; i >= 0; i--) {
        glow_filter_to_target(ctx, levels_tex[i + 1], widths[i + 1], heights[i + 1],
                              levels_fbo[i], widths[i], heights[i],
                              2, upsample_radius, 1.0f, threshold, softness,
                              tint_r, tint_g, tint_b, tint_a, true);
    }

    copy_texture_to_target(ctx, ctx->input_texture, ctx->output_fbo, ctx->width, ctx->height, 1.0f);
    glow_filter_to_target(ctx, levels_tex[0], widths[0], heights[0],
                          ctx->output_fbo, ctx->width, ctx->height,
                          2, upsample_radius, intensity, threshold, softness,
                          tint_r, tint_g, tint_b, tint_a, true);

    for (int i = 0; i < level_count; i++) {
        destroy_temp_target(&levels_tex[i], &levels_fbo[i]);
    }
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

static float glow_get_padding(void* instance, double time) {
    GlowEffectData* data = (GlowEffectData*)instance;
    float radius = (float)evaluate_animation(&data->radius, time);
    if (radius < 0.0f) radius = 0.0f;
    return radius * 6.0f + 8.0f;
}
const EffectProcessor TINT_PROCESSOR = {
    "Tint", color_mix_create, tint_apply, color_mix_destroy, NULL,
    color_mix_set_number, color_mix_get_number, NULL, NULL, color_mix_set_color, NULL, 0, NULL,
    color_mix_get_number_animation, color_mix_get_color_animations_cb
};
const EffectProcessor FILL_PROCESSOR = {
    "Fill", color_mix_create, fill_apply, color_mix_destroy, NULL,
    color_mix_set_number, color_mix_get_number, NULL, NULL, color_mix_set_color, NULL, 0, NULL,
    color_mix_get_number_animation, color_mix_get_color_animations_cb
};
const EffectProcessor BRIGHTNESS_CONTRAST_PROCESSOR = {
    "BrightnessContrast", brightness_contrast_create, brightness_contrast_apply, brightness_contrast_destroy, NULL,
    brightness_contrast_set_number, brightness_contrast_get_number, NULL, NULL, NULL, NULL, 0, NULL,
    brightness_contrast_get_number_animation, NULL
};
const EffectProcessor BLUR_PROCESSOR = {
    "Blur", blur_create, blur_apply, blur_destroy, NULL,
    blur_set_number, blur_get_number, NULL, NULL, NULL, NULL, 0, NULL,
    blur_get_number_animation, NULL
};
const EffectProcessor GLOW_PROCESSOR = {
    "Glow", glow_create, glow_apply, glow_destroy, NULL,
    glow_set_number, glow_get_number, NULL, NULL, glow_set_color, NULL, 0, glow_get_padding,
    glow_get_number_animation, glow_get_color_animations_cb
};
