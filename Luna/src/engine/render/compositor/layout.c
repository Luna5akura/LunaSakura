#include "internal.h"
bool resolve_clip_layout_recursive(Compositor* comp, TimelineClip* tc, double time, int depth,
                                          float* out_x, float* out_y, float* out_w, float* out_h) {
    float base_x;
    float base_y;
    float scale_x;
    float scale_y;
    float width;
    float height;
    float x;
    float y;
    if (!tc || !tc->media || depth > 8) return false;
    evaluate_clip_transform_state(comp, tc, time, &base_x, &base_y, &scale_x, &scale_y, NULL, NULL);
    get_clip_visual_size(comp, tc, &width, &height);
    width *= scale_x;
    height *= scale_y;
    x = base_x;
    y = base_y;
    if (tc->position_mode == TIMELINE_POSITION_MODE_ANCHOR) {
        float target_left = 0.0f;
        float target_top = 0.0f;
        float target_width = (float)comp->timeline->width;
        float target_height = (float)comp->timeline->height;
        float target_anchor_x;
        float target_anchor_y;
        float self_anchor_x;
        float self_anchor_y;
        if (tc->alignment_target_type == TIMELINE_ALIGNMENT_TARGET_CLIP && tc->alignment_target_clip_id != 0) {
            TimelineClip* target = timeline_find_clip_by_id(comp->timeline, tc->alignment_target_clip_id, NULL, NULL);
            if (target && target != tc) {
                resolve_clip_layout_recursive(comp, target, time, depth + 1,
                                              &target_left, &target_top, &target_width, &target_height);
            }
        }
        get_anchor_point_coords(target_left, target_top, target_width, target_height,
                                tc->target_anchor, &target_anchor_x, &target_anchor_y);
        get_anchor_point_coords(0.0f, 0.0f, width, height,
                                tc->self_anchor, &self_anchor_x, &self_anchor_y);
        x = target_anchor_x - self_anchor_x + base_x;
        y = target_anchor_y - self_anchor_y + base_y;
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = width;
    if (out_h) *out_h = height;
    return true;
}

void ensure_effect_targets(Compositor* comp, int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (comp->effect_width == width && comp->effect_height == height &&
        comp->effect_source_texture != 0 && comp->effect_ping_texture != 0 &&
        comp->effect_aux_texture != 0) {
        return;
    }

    comp->effect_width = width;
    comp->effect_height = height;

    if (comp->effect_source_texture == 0) glGenTextures(1, &comp->effect_source_texture);
    if (comp->effect_source_fbo == 0) glGenFramebuffers(1, &comp->effect_source_fbo);
    if (comp->effect_ping_texture == 0) glGenTextures(1, &comp->effect_ping_texture);
    if (comp->effect_ping_fbo == 0) glGenFramebuffers(1, &comp->effect_ping_fbo);
    if (comp->effect_base_texture == 0) glGenTextures(1, &comp->effect_base_texture);
    if (comp->effect_base_fbo == 0) glGenFramebuffers(1, &comp->effect_base_fbo);
    if (comp->effect_aux_texture == 0) glGenTextures(1, &comp->effect_aux_texture);
    if (comp->effect_aux_fbo == 0) glGenFramebuffers(1, &comp->effect_aux_fbo);

    glBindTexture(GL_TEXTURE_2D, comp->effect_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_source_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_source_texture, 0);

    glBindTexture(GL_TEXTURE_2D, comp->effect_ping_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_ping_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_ping_texture, 0);

    glBindTexture(GL_TEXTURE_2D, comp->effect_base_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_base_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_base_texture, 0);

    glBindTexture(GL_TEXTURE_2D, comp->effect_aux_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_aux_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_aux_texture, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void set_texture_sampling(GLuint texture, bool nearest) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
}

