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
const EffectProcessor SLIDER_CONTROL_PROCESSOR = {
    "SliderControl", slider_control_create, slider_control_apply, single_number_control_destroy, NULL,
    single_number_control_set_number, single_number_control_get_number, NULL, NULL, NULL, NULL, 0, NULL,
    slider_control_get_number_animation, NULL
};
const EffectProcessor ANGLE_CONTROL_PROCESSOR = {
    "AngleControl", slider_control_create, slider_control_apply, single_number_control_destroy, NULL,
    single_number_control_set_number, single_number_control_get_number, NULL, NULL, NULL, NULL, 0, NULL,
    slider_control_get_number_animation, NULL
};
const EffectProcessor CHECKBOX_CONTROL_PROCESSOR = {
    "CheckboxControl", checkbox_control_create, checkbox_control_apply, checkbox_control_destroy, NULL,
    checkbox_control_set_number, checkbox_control_get_number, checkbox_control_set_bool, checkbox_control_get_bool, NULL, NULL, 0, NULL,
    checkbox_control_get_number_animation, NULL
};
const EffectProcessor POINT_CONTROL_PROCESSOR = {
    "PointControl", point_control_create, point_control_apply, point_control_destroy, NULL,
    point_control_set_number, point_control_get_number, NULL, NULL, NULL, NULL, 0, NULL,
    point_control_get_number_animation, NULL
};
const EffectProcessor COLOR_CONTROL_PROCESSOR = {
    "ColorControl", color_control_create, color_control_apply, color_control_destroy, NULL,
    NULL, NULL, NULL, NULL, color_control_set_color, NULL, 0, NULL,
    NULL, color_control_get_color_animations_cb
};
