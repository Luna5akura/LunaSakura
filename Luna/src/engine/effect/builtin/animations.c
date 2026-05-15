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

Animation* color_mix_get_number_animation(void* instance, const char* key) {
    ColorMixEffectData* data = (ColorMixEffectData*)instance;
    if (strcmp(key, "amount") == 0) return &data->amount;
    return NULL;
}

bool color_mix_get_color_animations_cb(void* instance, const char* key,
                                              Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return get_color_mix_animation((ColorMixEffectData*)instance, key, out_r, out_g, out_b, out_a);
}

Animation* brightness_contrast_get_number_animation(void* instance, const char* key) {
    BrightnessContrastEffectData* data = (BrightnessContrastEffectData*)instance;
    if (strcmp(key, "brightness") == 0) return &data->brightness;
    if (strcmp(key, "contrast") == 0) return &data->contrast;
    return NULL;
}

Animation* blur_get_number_animation(void* instance, const char* key) {
    BlurEffectData* data = (BlurEffectData*)instance;
    if (strcmp(key, "radius") == 0) return &data->radius;
    return NULL;
}

Animation* glow_get_number_animation(void* instance, const char* key) {
    GlowEffectData* data = (GlowEffectData*)instance;
    if (strcmp(key, "radius") == 0) return &data->radius;
    if (strcmp(key, "intensity") == 0) return &data->intensity;
    if (strcmp(key, "threshold") == 0) return &data->threshold;
    if (strcmp(key, "softness") == 0) return &data->softness;
    return NULL;
}

bool glow_get_color_animations_cb(void* instance, const char* key,
                                         Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return resolve_color_array(((GlowEffectData*)instance)->color, key, "color", out_r, out_g, out_b, out_a);
}

Animation* mosaic_get_number_animation(void* instance, const char* key) {
    MosaicEffectData* data = (MosaicEffectData*)instance;
    if (strcmp(key, "blockSize") == 0) return &data->block_size;
    return NULL;
}

Animation* grid_get_number_animation(void* instance, const char* key) {
    GridEffectData* data = (GridEffectData*)instance;
    if (strcmp(key, "sizeX") == 0) return &data->size_x;
    if (strcmp(key, "sizeY") == 0) return &data->size_y;
    if (strcmp(key, "lineWidth") == 0) return &data->line_width;
    if (strcmp(key, "opacity") == 0) return &data->opacity;
    return NULL;
}

bool grid_get_color_animations_cb(void* instance, const char* key,
                                         Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return resolve_color_array(((GridEffectData*)instance)->color, key, "color", out_r, out_g, out_b, out_a);
}

Animation* gradient_ramp_get_number_animation(void* instance, const char* key) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    if (strcmp(key, "startX") == 0) return &data->start[0];
    if (strcmp(key, "startY") == 0) return &data->start[1];
    if (strcmp(key, "endX") == 0) return &data->end[0];
    if (strcmp(key, "endY") == 0) return &data->end[1];
    if (strcmp(key, "blend") == 0) return &data->blend;
    return NULL;
}

bool gradient_ramp_get_color_animations_cb(void* instance, const char* key,
                                                  Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    GradientRampEffectData* data = (GradientRampEffectData*)instance;
    if (strcmp(key, "startColor") == 0) return resolve_color_array(data->start_color, key, "startColor", out_r, out_g, out_b, out_a);
    if (strcmp(key, "endColor") == 0) return resolve_color_array(data->end_color, key, "endColor", out_r, out_g, out_b, out_a);
    return false;
}

Animation* fractal_noise_get_number_animation(void* instance, const char* key) {
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

Animation* displacement_map_get_number_animation(void* instance, const char* key) {
    DisplacementMapEffectData* data = (DisplacementMapEffectData*)instance;
    if (strcmp(key, "scaleX") == 0) return &data->scale_x;
    if (strcmp(key, "scaleY") == 0) return &data->scale_y;
    if (strcmp(key, "amount") == 0) return &data->amount;
    if (strcmp(key, "offsetX") == 0) return &data->offset_x;
    if (strcmp(key, "offsetY") == 0) return &data->offset_y;
    if (strcmp(key, "horizontalChannel") == 0) return &data->horizontal_channel;
    if (strcmp(key, "verticalChannel") == 0) return &data->vertical_channel;
    return NULL;
}

Animation* posterize_get_number_animation(void* instance, const char* key) {
    PosterizeEffectData* data = (PosterizeEffectData*)instance;
    if (strcmp(key, "levels") == 0) return &data->levels;
    if (strcmp(key, "amount") == 0) return &data->amount;
    return NULL;
}

Animation* slider_control_get_number_animation(void* instance, const char* key) {
    SingleNumberControlEffectData* data = (SingleNumberControlEffectData*)instance;
    if (strcmp(key, "value") == 0 || strcmp(key, "angle") == 0) return &data->value;
    return NULL;
}

Animation* checkbox_control_get_number_animation(void* instance, const char* key) {
    CheckboxControlEffectData* data = (CheckboxControlEffectData*)instance;
    if (strcmp(key, "value") == 0) return &data->value;
    return NULL;
}

Animation* point_control_get_number_animation(void* instance, const char* key) {
    PointControlEffectData* data = (PointControlEffectData*)instance;
    if (strcmp(key, "x") == 0) return &data->x;
    if (strcmp(key, "y") == 0) return &data->y;
    return NULL;
}

bool color_control_get_color_animations_cb(void* instance, const char* key,
                                                  Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    return resolve_color_array(((ColorControlEffectData*)instance)->color, key, "color", out_r, out_g, out_b, out_a);
}
