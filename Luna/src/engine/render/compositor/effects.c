#include "internal.h"

static void restore_timeline_projections(Compositor* comp) {
    mat4 proj = mat4_ortho(0, comp->timeline->width, comp->timeline->height, 0, -1, 1);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(comp->yuv_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->image_shader_program);
    glUniformMatrix4fv(comp->image_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->color_shader_program);
    glUniformMatrix4fv(comp->color_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->text_shader_program);
    glUniformMatrix4fv(comp->text_uniforms.u_projection, 1, GL_FALSE, proj.m);
}

static float effect_chain_padding(EffectInstance* effect, double time) {
    float padding = 0.0f;
    while (effect) {
        if (effect->processor && effect->processor->get_padding) {
            float current = effect->processor->get_padding(effect->data, time);
            if (current > padding) padding = current;
        }
        effect = effect->next;
    }
    return padding;
}

void draw_texture_transformed(Compositor* comp, GLuint texture, TimelineClip* tc, float width, float height,
                              bool nearest_sampling, bool premultiplied) {
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float rotation = tc->transform.rotation;
    float opacity = tc->transform.opacity;
    float w = width * scale_x;
    float h = height * scale_y;
    float center_x = w / 2.0f;
    float center_y = h / 2.0f;
    mat4 model = mat4_identity();

    glUseProgram(comp->image_shader_program);
    glActiveTexture(GL_TEXTURE0);
    set_texture_sampling(texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(comp->image_uniforms.sampler0, 0);
    glUniform1i(comp->image_uniforms.u_flip_y, 1);
    glUniform1i(comp->image_uniforms.u_premultiplied, premultiplied ? 1 : 0);
    glBlendFunc(premultiplied ? GL_ONE : GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->image_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform1f(comp->image_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (nearest_sampling) {
        set_texture_sampling(texture, false);
    }
    glUniform1i(comp->image_uniforms.u_flip_y, 0);
    glUniform1i(comp->image_uniforms.u_premultiplied, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void draw_texture_fullframe(Compositor* comp, GLuint texture, bool nearest_sampling, float opacity) {
    glUseProgram(comp->copy_shader_program);
    glActiveTexture(GL_TEXTURE0);
    set_texture_sampling(texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(comp->copy_uniforms.sampler0, 0);
    glUniform1f(comp->copy_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (nearest_sampling) {
        set_texture_sampling(texture, false);
    }
}

bool is_nested_timeline_clip(const Clip* clip) {
    return clip && (clip->type == CLIP_TYPE_GROUP || clip->type == CLIP_TYPE_PRECOMP);
}

void render_nested_timeline_to_target(Compositor* comp, TimelineClip* tc, double clip_time, GLuint target_fbo) {
    ObjClip* owner;
    Timeline* nested;
    Compositor* sub;

    if (!tc || !tc->media || !is_nested_timeline_clip(tc->media)) return;
    owner = tc->media->user_data ? (ObjClip*)tc->media->user_data : NULL;
    nested = (owner && owner->timelineObj) ? owner->timelineObj->timeline : tc->media->nested_timeline.timeline;
    if (!nested) return;

    sub = get_nested_compositor_safe(comp, tc->media, nested);
    if (!sub) return;
    compositor_render(sub, clip_time);

    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, nested->width, nested->height);
    glDisable(GL_BLEND);
    draw_texture_fullframe(comp, sub->output_texture, false, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void compute_adjustment_region_params(TimelineClip* tc,
                                             float* out_center_x, float* out_center_y,
                                             float* out_half_w, float* out_half_h);

void composite_adjustment_layer(Compositor* comp, TimelineClip* tc, GLuint base_texture, GLuint adjusted_texture,
                                       GLuint mask_texture, bool has_mask, bool nearest_sampling) {
    float region_center_x = 0.0f;
    float region_center_y = 0.0f;
    float region_half_w = 0.0f;
    float region_half_h = 0.0f;
    glUseProgram(comp->adjustment_composite_shader_program);
    glActiveTexture(GL_TEXTURE0);
    set_texture_sampling(base_texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, base_texture);
    glUniform1i(comp->adjustment_u_base_texture, 0);
    glActiveTexture(GL_TEXTURE1);
    set_texture_sampling(adjusted_texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, adjusted_texture);
    glUniform1i(comp->adjustment_u_adjusted_texture, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, has_mask ? mask_texture : base_texture);
    glUniform1i(comp->adjustment_u_mask_texture, 2);
    if (!tc->media->adjustment.affects_whole_frame) {
        compute_adjustment_region_params(tc, &region_center_x, &region_center_y, &region_half_w, &region_half_h);
    }
    glUniform2f(comp->adjustment_u_region_center,
                region_center_x, region_center_y);
    glUniform2f(comp->adjustment_u_region_half_size,
                region_half_w, region_half_h);
    glUniform2f(comp->adjustment_u_canvas_size,
                (float)comp->timeline->width, (float)comp->timeline->height);
    glUniform1f(comp->adjustment_u_opacity, tc->transform.opacity);
    glUniform1f(comp->adjustment_u_feather,
                (float)tc->media->adjustment.feather);
    glUniform1f(comp->adjustment_u_region_rotation,
                tc->transform.rotation);
    glUniform1i(comp->adjustment_u_blend_mode,
                (int)tc->media->adjustment.blend_mode);
    glUniform1i(comp->adjustment_u_mask_mode,
                (int)tc->media->adjustment.mask_mode);
    glUniform1i(comp->adjustment_u_whole_frame,
                tc->media->adjustment.affects_whole_frame ? 1 : 0);
    glUniform1i(comp->adjustment_u_has_mask, has_mask ? 1 : 0);
    glUniform1i(comp->adjustment_u_mask_invert,
                tc->media->adjustment.mask_invert ? 1 : 0);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (nearest_sampling) {
        set_texture_sampling(base_texture, false);
        set_texture_sampling(adjusted_texture, false);
    }
}

static void compute_adjustment_region_params(TimelineClip* tc,
                                             float* out_center_x, float* out_center_y,
                                             float* out_half_w, float* out_half_h) {
    float x = tc->transform.x;
    float y = tc->transform.y;
    float w = (float)tc->media->width * tc->transform.scale_x;
    float h = (float)tc->media->height * tc->transform.scale_y;
    if (w < 0.0f) {
        x += w;
        w = -w;
    }
    if (h < 0.0f) {
        y += h;
        h = -h;
    }
    if (out_center_x) *out_center_x = x + w * 0.5f;
    if (out_center_y) *out_center_y = y + h * 0.5f;
    if (out_half_w) *out_half_w = w * 0.5f;
    if (out_half_h) *out_half_h = h * 0.5f;
}

static void capture_framebuffer_to_target(Compositor* comp, GLuint target_fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, comp->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fbo);
    glBlitFramebuffer(
        0, 0, comp->timeline->width, comp->timeline->height,
        0, 0, comp->effect_width, comp->effect_height,
        GL_COLOR_BUFFER_BIT, GL_LINEAR
    );
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
}

static void capture_composited_frame_for_adjustment(Compositor* comp) {
    capture_framebuffer_to_target(comp, comp->effect_source_fbo);
}

void render_clip_source_to_target(Compositor* comp, TimelineClip* tc, double clip_time, GLuint target_fbo,
                                  float offset_x, float offset_y) {
    TimelineClip local = *tc;
    mat4 proj;

    local.transform.x = offset_x;
    local.transform.y = offset_y;
    local.transform.scale_x = 1.0f;
    local.transform.scale_y = 1.0f;
    local.transform.rotation = 0.0f;
    local.transform.opacity = 1.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, comp->effect_width, comp->effect_height);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    proj = mat4_ortho(0, comp->effect_width, comp->effect_height, 0, -1, 1);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(comp->yuv_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->image_shader_program);
    glUniformMatrix4fv(comp->image_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->color_shader_program);
    glUniformMatrix4fv(comp->color_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->text_shader_program);
    glUniformMatrix4fv(comp->text_uniforms.u_projection, 1, GL_FALSE, proj.m);

    if (local.media->type == CLIP_TYPE_TEXT) {
        draw_clip_text(comp, &local, clip_time);
    } else if (local.media->type == CLIP_TYPE_IMAGE) {
        RenderSource* src = get_source_safe(comp, local.media);
        draw_clip_rgba(comp, src, &local);
    } else if (local.media->type == CLIP_TYPE_SOLID) {
        draw_clip_solid(comp, &local);
    } else if (is_nested_timeline_clip(local.media)) {
        render_nested_timeline_to_target(comp, &local, clip_time, target_fbo);
    } else if (local.media->type == CLIP_TYPE_ADJUSTMENT) {
        capture_composited_frame_for_adjustment(comp);
    } else {
        RenderSource* src = get_source_safe(comp, local.media);
        bool new_frame = decoder_update_video(src->decoder, clip_time);
        uint8_t* data[3];
        int linesize[3];
        int fw = 0, fh = 0;
        if (decoder_get_video_data(src->decoder, data, linesize, &fw, &fh)) {
            if (new_frame) {
                bool resize = (src->width != fw || src->height != fh);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, src->tex_y);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[0]);
                if (resize) {
                    src->width = fw;
                    src->height = fh;
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw, fh, 0, GL_RED, GL_UNSIGNED_BYTE, data[0]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RED, GL_UNSIGNED_BYTE, data[0]);
                }
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, src->tex_u);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[1]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2, 0, GL_RED, GL_UNSIGNED_BYTE, data[1]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2, GL_RED, GL_UNSIGNED_BYTE, data[1]);
                }
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, src->tex_v);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[2]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2, 0, GL_RED, GL_UNSIGNED_BYTE, data[2]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2, GL_RED, GL_UNSIGNED_BYTE, data[2]);
                }
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            draw_clip_rect(comp, src, &local);
        }
    }
}

void render_clip_source_to_effect_target(Compositor* comp, TimelineClip* tc, double clip_time,
                                         float offset_x, float offset_y) {
    render_clip_source_to_target(comp, tc, clip_time, comp->effect_source_fbo, offset_x, offset_y);
}

bool effect_chain_has_external_source_refs(EffectInstance* effect) {
    while (effect) {
        if (effect->processor && effect->processor->get_source_clip &&
            effect->processor->get_source_clip(effect->data) != 0) {
            return true;
        }
        effect = effect->next;
    }
    return false;
}

void populate_effect_render_context(Compositor* comp, EffectRenderContext* ctx,
                                           GLuint input_texture, GLuint output_fbo) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->input_texture = input_texture;
    ctx->output_fbo = output_fbo;
    ctx->auxiliary_texture = 0;
    ctx->quad_vao = comp->vao;
    ctx->copy_shader_program = comp->copy_shader_program;
    ctx->tint_shader_program = comp->tint_shader_program;
    ctx->fill_shader_program = comp->fill_shader_program;
    ctx->gradient_ramp_shader_program = comp->gradient_ramp_shader_program;
    ctx->grid_shader_program = comp->grid_shader_program;
    ctx->mosaic_shader_program = comp->mosaic_shader_program;
    ctx->brightness_contrast_shader_program = comp->brightness_contrast_shader_program;
    ctx->blur_shader_program = comp->blur_shader_program;
    ctx->fractal_noise_shader_program = comp->fractal_noise_shader_program;
    ctx->displacement_map_shader_program = comp->displacement_map_shader_program;
    ctx->posterize_shader_program = comp->posterize_shader_program;
    ctx->copy_u_texture = comp->copy_uniforms.sampler0;
    ctx->copy_u_opacity = comp->copy_uniforms.u_opacity;
    ctx->tint_u_texture = comp->tint_uniforms.sampler0;
    ctx->tint_u_amount = comp->tint_uniforms.u_opacity;
    ctx->tint_u_color = comp->tint_uniforms.u_color;
    ctx->fill_u_texture = comp->fill_uniforms.sampler0;
    ctx->fill_u_amount = comp->fill_uniforms.u_opacity;
    ctx->fill_u_color = comp->fill_uniforms.u_color;
    ctx->gradient_u_texture = comp->gradient_uniforms.sampler0;
    ctx->gradient_u_start = comp->gradient_uniforms.u_projection;
    ctx->gradient_u_end = comp->gradient_uniforms.u_model;
    ctx->gradient_u_start_color = comp->gradient_uniforms.u_color;
    ctx->gradient_u_end_color = comp->gradient_uniforms.sampler1;
    ctx->gradient_u_blend = comp->gradient_uniforms.u_opacity;
    ctx->grid_u_texture = comp->grid_uniforms.sampler0;
    ctx->grid_u_size = comp->grid_uniforms.u_projection;
    ctx->grid_u_line_width = comp->grid_uniforms.u_model;
    ctx->grid_u_color = comp->grid_uniforms.u_color;
    ctx->grid_u_opacity = comp->grid_uniforms.u_opacity;
    ctx->mosaic_u_texture = comp->mosaic_uniforms.sampler0;
    ctx->mosaic_u_block_size = comp->mosaic_uniforms.u_projection;
    ctx->mosaic_u_resolution = comp->mosaic_uniforms.u_model;
    ctx->mosaic_u_sharp_colors = comp->mosaic_uniforms.u_opacity;
    ctx->brightness_u_texture = comp->brightness_uniforms.sampler0;
    ctx->brightness_u_brightness = comp->brightness_uniforms.u_projection;
    ctx->brightness_u_contrast = comp->brightness_uniforms.u_model;
    ctx->blur_u_texture = comp->blur_uniforms.sampler0;
    ctx->blur_u_texel_size = comp->blur_uniforms.u_projection;
    ctx->blur_u_radius = comp->blur_uniforms.u_opacity;
    ctx->blur_u_direction = comp->blur_uniforms.u_color;
    ctx->glow_shader_program = comp->glow_shader_program;
    ctx->glow_u_texture = comp->glow_uniforms.sampler0;
    ctx->glow_u_texel_size = comp->glow_u_texel_size;
    ctx->glow_u_radius = comp->glow_u_radius;
    ctx->glow_u_intensity = comp->glow_u_intensity;
    ctx->glow_u_threshold = comp->glow_u_threshold;
    ctx->glow_u_softness = comp->glow_u_softness;
    ctx->glow_u_color = comp->glow_u_color;
    ctx->glow_u_mode = comp->glow_u_mode;
    ctx->fractal_u_texture = comp->fractal_uniforms.sampler0;
    ctx->fractal_u_resolution = comp->fractal_uniforms.u_projection;
    ctx->fractal_u_scale = comp->fractal_uniforms.u_model;
    ctx->fractal_u_evolution = comp->fractal_uniforms.u_opacity;
    ctx->fractal_u_contrast = comp->fractal_uniforms.u_color;
    ctx->fractal_u_brightness = comp->fractal_uniforms.sampler1;
    ctx->fractal_u_octaves = comp->fractal_uniforms.sampler2;
    ctx->fractal_u_amount = comp->fractal_u_amount;
    ctx->fractal_u_offset = comp->fractal_u_offset;
    ctx->fractal_u_invert = comp->fractal_u_invert;
    ctx->displacement_u_texture = comp->displacement_uniforms.sampler0;
    ctx->displacement_u_map_texture = comp->displacement_uniforms.sampler1;
    ctx->displacement_u_resolution = comp->displacement_uniforms.u_projection;
    ctx->displacement_u_scale_x = comp->displacement_uniforms.u_model;
    ctx->displacement_u_scale_y = comp->displacement_uniforms.u_opacity;
    ctx->displacement_u_amount = comp->displacement_uniforms.u_color;
    ctx->displacement_u_offset = comp->displacement_u_offset;
    ctx->displacement_u_horizontal_channel = comp->displacement_u_horizontal_channel;
    ctx->displacement_u_vertical_channel = comp->displacement_u_vertical_channel;
    ctx->displacement_u_use_luma = comp->displacement_u_use_luma;
    ctx->posterize_u_texture = comp->posterize_uniforms.sampler0;
    ctx->posterize_u_levels = comp->posterize_uniforms.u_projection;
    ctx->posterize_u_amount = comp->posterize_uniforms.u_opacity;
    ctx->width = comp->effect_width;
    ctx->height = comp->effect_height;
    ctx->source_offset_x = 0.0f;
    ctx->source_offset_y = 0.0f;
}

bool render_clip_effect_result_to_auxiliary(Compositor* comp, TimelineClip* tc, double clip_time,
                                                   GLuint scratch_texture, GLuint scratch_fbo,
                                                   bool* out_prefer_nearest) {
    EffectInstance* effect;
    GLuint input_texture;
    GLuint output_texture;
    GLuint output_fbo;
    bool prefer_nearest_output = false;

    if (!comp || !tc) return false;
    effect = tc->effectChain;
    if (!effect || effect_chain_has_external_source_refs(effect)) return false;

    render_clip_source_to_target(comp, tc, clip_time, scratch_fbo, 0.0f, 0.0f);
    input_texture = scratch_texture;
    output_texture = comp->effect_aux_texture;
    output_fbo = comp->effect_aux_fbo;

    while (effect) {
        EffectRenderContext ctx;
        populate_effect_render_context(comp, &ctx, input_texture, output_fbo);
        effect_apply_links(comp->timeline, effect, clip_time);
        effect->processor->apply(effect->data, &ctx, clip_time);
        prefer_nearest_output = ctx.prefer_nearest_output;
        input_texture = output_texture;
        if (output_texture == comp->effect_aux_texture) {
            output_texture = scratch_texture;
            output_fbo = scratch_fbo;
        } else {
            output_texture = comp->effect_aux_texture;
            output_fbo = comp->effect_aux_fbo;
        }
        effect = effect->next;
    }

    if (input_texture != comp->effect_aux_texture) {
        glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_aux_fbo);
        glViewport(0, 0, comp->effect_width, comp->effect_height);
        glDisable(GL_BLEND);
        draw_texture_fullframe(comp, input_texture, prefer_nearest_output, 1.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (out_prefer_nearest) *out_prefer_nearest = prefer_nearest_output;
    return true;
}

void render_clip_with_effects(Compositor* comp, TimelineClip* tc, double clip_time) {
    EffectInstance* effect = tc->effectChain;
    GLuint input_texture;
    GLuint output_texture = comp->effect_ping_texture;
    GLuint output_fbo = comp->effect_ping_fbo;
    EffectRenderContext ctx;
    bool prefer_nearest_output = false;
    bool has_adjustment_mask = false;
    float source_width = 0.0f;
    float source_height = 0.0f;
    float padding = 0.0f;

    if (tc->media->type == CLIP_TYPE_ADJUSTMENT) {
        ensure_effect_targets(comp, (int)comp->timeline->width, (int)comp->timeline->height);
        capture_framebuffer_to_target(comp, comp->effect_base_fbo);
        capture_framebuffer_to_target(comp, comp->effect_source_fbo);
    } else {
        padding = effect_chain_padding(tc->effectChain, clip_time);
        get_clip_visual_size(comp, tc, &source_width, &source_height);
        ensure_effect_targets(comp, (int)ceilf(source_width + padding * 2.0f), (int)ceilf(source_height + padding * 2.0f));
        render_clip_source_to_effect_target(comp, tc, clip_time, padding, padding);
    }
    input_texture = comp->effect_source_texture;

    while (effect) {
        effect_apply_links(comp->timeline, effect, clip_time);
        populate_effect_render_context(comp, &ctx, input_texture, output_fbo);
        if (effect->processor && effect->processor->get_source_clip) {
            u32 source_clip_id = effect->processor->get_source_clip(effect->data);
            if (source_clip_id != 0) {
                TimelineClip* source_clip = timeline_find_clip_by_id(comp->timeline, source_clip_id, NULL, NULL);
                if (source_clip) {
                    double timeline_time = tc->timeline_start + clip_time - tc->source_in;
                    double source_time = timeline_time - source_clip->timeline_start + source_clip->source_in;
                    if (!render_clip_effect_result_to_auxiliary(comp, source_clip, source_time,
                                                                output_texture, output_fbo, NULL)) {
                        render_clip_source_to_target(comp, source_clip, source_time, comp->effect_aux_fbo, 0.0f, 0.0f);
                    }
                    ctx.auxiliary_texture = comp->effect_aux_texture;
                    ctx.has_auxiliary_texture = true;
                }
            }
        }
        effect->processor->apply(effect->data, &ctx, clip_time);
        prefer_nearest_output = ctx.prefer_nearest_output;
        input_texture = output_texture;
        if (output_texture == comp->effect_ping_texture) {
            output_texture = comp->effect_source_texture;
            output_fbo = comp->effect_source_fbo;
        } else {
            output_texture = comp->effect_ping_texture;
            output_fbo = comp->effect_ping_fbo;
        }
        effect = effect->next;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glViewport(0, 0, comp->timeline->width, comp->timeline->height);
    restore_timeline_projections(comp);
    if (tc->media->type == CLIP_TYPE_ADJUSTMENT) {
        if (tc->media->adjustment.mask_source_clip_id != 0) {
            TimelineClip* mask_clip = timeline_find_clip_by_id(comp->timeline, tc->media->adjustment.mask_source_clip_id, NULL, NULL);
            if (mask_clip) {
                double timeline_time = tc->timeline_start + clip_time - tc->source_in;
                double mask_time = timeline_time - mask_clip->timeline_start + mask_clip->source_in;
                render_clip_source_to_target(comp, mask_clip, mask_time, comp->effect_aux_fbo, 0.0f, 0.0f);
                has_adjustment_mask = true;
            }
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        composite_adjustment_layer(comp, tc, comp->effect_base_texture, input_texture,
                                   comp->effect_aux_texture, has_adjustment_mask, prefer_nearest_output);
    } else {
        TimelineClip composite_clip = *tc;
        if (padding > 0.0f) {
            composite_clip.transform.x -= padding * tc->transform.scale_x;
            composite_clip.transform.y -= padding * tc->transform.scale_y;
        }
        draw_texture_transformed(comp, input_texture, &composite_clip,
                                 (float)comp->effect_width, (float)comp->effect_height,
                                 prefer_nearest_output, true);
    }
    if (prefer_nearest_output) {
        comp->preview_prefers_nearest = true;
    }
}
