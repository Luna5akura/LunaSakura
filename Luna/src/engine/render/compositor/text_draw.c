#include "internal.h"
static void draw_text_glyph(Compositor* comp, GLint loc_model, GLint loc_uv, GlyphInfo* glyph, mat4 group_model,
                            float rel_x, float rel_y, float base_scale_x, float base_scale_y,
                            const TextCharStyle* style) {
    float w = glyph->width * base_scale_x * style->scale_x;
    float h = glyph->height * base_scale_y * style->scale_y;
    float pivot_x = w * 0.5f + style->anchor_x;
    float pivot_y = h * 0.5f + style->anchor_y;
    mat4 local_model = mat4_identity();
    local_model = mat4_mult(mat4_translate(rel_x + style->offset_x, rel_y + style->offset_y), local_model);
    local_model = mat4_mult(mat4_translate(pivot_x, pivot_y), local_model);
    local_model = mat4_mult(mat4_skew(style->skew, style->skew_axis), local_model);
    local_model = mat4_mult(mat4_rotate(style->rotation), local_model);
    local_model = mat4_mult(mat4_translate(-pivot_x, -pivot_y), local_model);
    local_model = mat4_mult(mat4_scale(w, h), local_model);
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, mat4_mult(local_model, group_model).m);
    glUniform4f(loc_uv, glyph->u0, glyph->v0, glyph->u1, glyph->v1);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void draw_clip_text(Compositor* comp, TimelineClip* tc, double anim_time) {
    Clip* clip = tc->media;
    TextRenderer* tr = comp->text_renderer;
    int char_count = clip->text.content ? (int)strlen(clip->text.content) : 0;
    TextCharMeta* metas = NULL;
    int total_words = 0;
    int total_lines = 0;
    text_renderer_update(tr, clip);

    glUseProgram(comp->text_shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text_renderer_get_texture(tr));
    glUniform1i(comp->text_uniforms.sampler0, 0);

    GLint loc_model = comp->text_uniforms.u_model;
    GLint loc_uv    = comp->text_uniforms.u_uv_rect;
    GLint loc_color = comp->text_uniforms.u_color;

    float start_x = tc->transform.x;
    float start_y = tc->transform.y;
    float rotation = tc->transform.rotation;
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;

    float scaled_w = clip->text.cached_width * scale_x;
    float scaled_h = clip->text.cached_height * scale_y;
    float center_x = scaled_w / 2.0f;
    float center_y = scaled_h / 2.0f;

    mat4 group_model = mat4_identity();
    group_model = mat4_mult(mat4_translate(start_x + center_x, start_y + center_y), group_model);
    group_model = mat4_mult(mat4_rotate(rotation), group_model);
    group_model = mat4_mult(mat4_translate(-center_x, -center_y), group_model);

    {
        float x_cursor = 0.0f;
        const char* p = clip->text.content;
        int char_index = 0;
        if (char_count > 0) {
            metas = (TextCharMeta*)malloc(sizeof(TextCharMeta) * (size_t)char_count);
            if (metas) {
                build_text_char_meta(clip->text.content, char_count, metas, &total_words, &total_lines);
            }
        }
        while (p && *p) {
            TextCharStyle style;
            char rendered_char;
            GlyphInfo* glyph;
            float rel_x;
            float rel_y;
            compute_text_char_style(tc, anim_time, metas ? &metas[char_index] : NULL,
                                    total_words, total_lines, char_index, char_count, &style);
            rendered_char = *p;
            if (fabsf(style.character_value) > 0.5f) {
                int code = (int)lroundf(style.character_value);
                if (code >= 32 && code < TEXT_RENDERER_MAX_GLYPHS) rendered_char = (char)code;
            } else if (fabsf(style.character_offset) > 0.5f) {
                int code = (int)rendered_char + (int)lroundf(style.character_offset);
                if (code < 32) code = 32;
                if (code >= TEXT_RENDERER_MAX_GLYPHS) code = TEXT_RENDERER_MAX_GLYPHS - 1;
                rendered_char = (char)code;
            }
            glyph = text_renderer_get_glyph(comp->text_renderer, rendered_char);
            rel_x = x_cursor + glyph->bearing_x;
            rel_y = clip->text.font_size - glyph->bearing_y;

            if ((clip->text.stroke_enabled || style.stroke_width > 0.0f) && style.stroke_a > 0.0f) {
                float offset = style.stroke_width * 0.5f;
                glUniform4f(loc_color, style.stroke_r, style.stroke_g, style.stroke_b,
                            style.stroke_a * tc->transform.opacity * style.opacity_factor * style.stroke_opacity);
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        TextCharStyle stroke_style = style;
                        if (dx == 0 && dy == 0) continue;
                        stroke_style.offset_x += (float)dx * offset;
                        stroke_style.offset_y += (float)dy * offset;
                        draw_text_glyph(comp, loc_model, loc_uv, glyph, group_model, rel_x, rel_y,
                                        scale_x, scale_y, &stroke_style);
                    }
                }
            }

            glUniform4f(loc_color, style.fill_r, style.fill_g, style.fill_b,
                        style.fill_a * tc->transform.opacity * style.opacity_factor * style.fill_opacity);
            draw_text_glyph(comp, loc_model, loc_uv, glyph, group_model, rel_x, rel_y,
                            scale_x, scale_y, &style);
            x_cursor += glyph->advance + clip->text.letter_spacing + style.tracking;
            p++;
            char_index++;
        }
        if (metas) free(metas);
    }
}

