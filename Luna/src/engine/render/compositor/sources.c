#include "internal.h"

static void bind_yuv_textures(RenderSource* src) {
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src->tex_y);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, src->tex_u);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, src->tex_v);
}
void draw_clip_rect(Compositor* comp, RenderSource* src, TimelineClip* tc) {
    if (src->tex_y == 0) return;
    glUseProgram(comp->shader_program);
    bind_yuv_textures(src);
    glUniform1i(comp->yuv_uniforms.sampler0, 0);
    glUniform1i(comp->yuv_uniforms.sampler1, 1);
    glUniform1i(comp->yuv_uniforms.sampler2, 2);
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float rotation = tc->transform.rotation;
    float opacity = tc->transform.opacity;
    float w = (float)tc->media->width * scale_x;
    float h = (float)tc->media->height * scale_y;
    float center_x = w / 2.0f;
    float center_y = h / 2.0f;
    mat4 model = mat4_identity();
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->yuv_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform1f(comp->yuv_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
void draw_clip_rgba(Compositor* comp, RenderSource* src, TimelineClip* tc) {
    float scale_x;
    float scale_y;
    float rotation;
    float opacity;
    float w;
    float h;
    float center_x;
    float center_y;
    mat4 model;

    if (!src || !src->image_loaded || src->tex_rgba == 0) return;

    glUseProgram(comp->image_shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src->tex_rgba);
    glUniform1i(comp->image_uniforms.sampler0, 0);
    glUniform1i(comp->image_uniforms.u_flip_y, 0);
    glUniform1i(comp->image_uniforms.u_premultiplied, 0);

    scale_x = tc->transform.scale_x;
    scale_y = tc->transform.scale_y;
    rotation = tc->transform.rotation;
    opacity = tc->transform.opacity;
    w = (float)tc->media->width * scale_x;
    h = (float)tc->media->height * scale_y;
    center_x = w / 2.0f;
    center_y = h / 2.0f;
    model = mat4_identity();
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->image_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform1f(comp->image_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
void draw_clip_solid(Compositor* comp, TimelineClip* tc) {
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float rotation = tc->transform.rotation;
    float opacity = tc->transform.opacity;
    float w = (float)tc->media->width * scale_x;
    float h = (float)tc->media->height * scale_y;
    float center_x = w / 2.0f;
    float center_y = h / 2.0f;
    mat4 model = mat4_identity();

    glUseProgram(comp->color_shader_program);
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->color_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform4f(
        comp->color_uniforms.u_color,
        tc->media->solid.color.r / 255.0f,
        tc->media->solid.color.g / 255.0f,
        tc->media->solid.color.b / 255.0f,
        (tc->media->solid.color.a / 255.0f) * opacity
    );
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void get_anchor_point_coords(float left, float top, float width, float height, u8 anchor, float* out_x, float* out_y) {
    float x = left;
    float y = top;
    switch (anchor) {
        case TIMELINE_ANCHOR_TOP_CENTER:
            x += width * 0.5f;
            break;
        case TIMELINE_ANCHOR_TOP_RIGHT:
            x += width;
            break;
        case TIMELINE_ANCHOR_CENTER_LEFT:
            y += height * 0.5f;
            break;
        case TIMELINE_ANCHOR_CENTER:
            x += width * 0.5f;
            y += height * 0.5f;
            break;
        case TIMELINE_ANCHOR_CENTER_RIGHT:
            x += width;
            y += height * 0.5f;
            break;
        case TIMELINE_ANCHOR_BOTTOM_LEFT:
            y += height;
            break;
        case TIMELINE_ANCHOR_BOTTOM_CENTER:
            x += width * 0.5f;
            y += height;
            break;
        case TIMELINE_ANCHOR_BOTTOM_RIGHT:
            x += width;
            y += height;
            break;
        case TIMELINE_ANCHOR_TOP_LEFT:
        default:
            break;
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

void get_clip_visual_size(Compositor* comp, TimelineClip* tc, float* out_width, float* out_height) {
    float width = 0.0f;
    float height = 0.0f;
    if (!tc || !tc->media) {
        if (out_width) *out_width = 0.0f;
        if (out_height) *out_height = 0.0f;
        return;
    }
    if (tc->media->type == CLIP_TYPE_TEXT) {
        text_renderer_update(comp->text_renderer, tc->media);
        width = tc->media->text.cached_width;
        height = tc->media->text.cached_height;
    } else {
        width = (float)tc->media->width;
        height = (float)tc->media->height;
    }
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
}

void evaluate_clip_transform_state(Compositor* comp, TimelineClip* tc, double time,
                                          float* out_x, float* out_y, float* out_scale_x, float* out_scale_y,
                                          float* out_rotation, float* out_opacity) {
    double anim_time = time - tc->timeline_start;
    if (out_x) *out_x = (float)evaluate_animation(&tc->anim.x, anim_time);
    if (out_y) *out_y = (float)evaluate_animation(&tc->anim.y, anim_time);
    if (out_scale_x) *out_scale_x = (float)evaluate_animation(&tc->anim.scale_x, anim_time);
    if (out_scale_y) *out_scale_y = (float)evaluate_animation(&tc->anim.scale_y, anim_time);
    if (out_rotation) *out_rotation = (float)evaluate_animation(&tc->anim.rotation, anim_time);
    if (out_opacity) *out_opacity = (float)evaluate_animation(&tc->anim.opacity, anim_time);
    if (tc->media && tc->media->type == CLIP_TYPE_TEXT) {
        tc->media->text.font_size = (uint32_t)round(evaluate_animation(&tc->anim.font_size, anim_time));
        text_renderer_update(comp->text_renderer, tc->media);
    }
}
