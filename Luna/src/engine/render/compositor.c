// src/engine/render/compositor.c

#include "compositor.h"
#include "engine/media/codec/decoder.h"
#include "engine/render/gl_utils.h" // 使用新工具库
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/model/animation.h"  // 新增
#include <math.h> // For cosf, sinf
typedef struct { float m[16]; } mat4;
static mat4 mat4_identity(void) {
    mat4 res = {0};
    res.m[0] = 1.0f; res.m[5] = 1.0f; res.m[10] = 1.0f; res.m[15] = 1.0f;
    return res;
}
static mat4 mat4_mult(mat4 a, mat4 b) {
    mat4 res = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                res.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
            }
        }
    }
    return res;
}
static mat4 mat4_translate(float x, float y) {
    mat4 res = mat4_identity();
    res.m[12] = x;
    res.m[13] = y;
    return res;
}
static mat4 mat4_scale(float sx, float sy) {
    mat4 res = mat4_identity();
    res.m[0] = sx;
    res.m[5] = sy;
    return res;
}
static mat4 mat4_rotate(float angle_deg) {
    float rad = angle_deg * 3.141592653589793f / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    mat4 res = mat4_identity();
    res.m[0] = c; res.m[1] = -s;
    res.m[4] = s; res.m[5] = c;
    return res;
}
static mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    mat4 res = {0};
    res.m[0]=2.0f/(right-left);
    res.m[5]=2.0f/(top-bottom);
    res.m[10]=-2.0f/(far-near);
    res.m[12]=-(right+left)/(right-left);
    res.m[13]=-(top+bottom)/(top-bottom);
    res.m[14]=-(far+near)/(far-near);
    res.m[15]=1.0f;
    return res;
}
const char* VS_SOURCE = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "uniform mat4 u_projection;\n"
    "uniform mat4 u_model;\n"
    "void main() {\n"
    " gl_Position = u_projection * u_model * vec4(aPos, 0.0, 1.0);\n"
    " TexCoord = aTexCoord;\n"
    "}\n";
const char* FS_SOURCE_YUV = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D tex_y;\n"
    "uniform sampler2D tex_u;\n"
    "uniform sampler2D tex_v;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    " float y = texture(tex_y, TexCoord).r;\n"
    " float u = texture(tex_u, TexCoord).r - 0.5;\n"
    " float v = texture(tex_v, TexCoord).r - 0.5;\n"
    " float r = y + 1.402 * v;\n"
    " float g = y - 0.344136 * u - 0.714136 * v;\n"
    " float b = y + 1.772 * u;\n"
    " FragColor = vec4(r, g, b, u_opacity);\n"
    "}\n";
const char* VS_SCREEN = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "void main() {\n"
    " gl_Position = vec4(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);\n"
    " TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);\n"
    "}\n";
const char* FS_SCREEN = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D screenTexture;\n"
    "void main() {\n"
    " FragColor = texture(screenTexture, TexCoord);\n"
    "}\n";
const char* FS_SOURCE_TEXT = "#version 330 core\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D textAtlas;\n"
    "uniform vec4 u_color;\n"
    "uniform vec4 u_uv_rect;\n" // xy=top-left, zw=bottom-right
    "void main() {\n"
    " vec2 uv = mix(u_uv_rect.xy, u_uv_rect.zw, TexCoord);\n"
    " float alpha = texture(textAtlas, uv).r;\n"
    " FragColor = vec4(u_color.rgb, alpha * u_color.a);\n"
    "}\n";
typedef struct {
    Decoder* decoder;
    GLuint tex_y;
    GLuint tex_u;
    GLuint tex_v;
    int width, height;
} RenderSource;
static void setup_texture_params(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
static RenderSource* get_source_safe(Compositor* comp, Clip* clip) {
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for(int i=0; i<comp->source_count; i++) {
        if (decoder_get_clip_ref(sources[i].decoder) == clip) {
            return &sources[i];
        }
    }
    if (comp->source_count >= comp->source_capacity) {
        int old_capacity = comp->source_capacity;
        comp->source_capacity = MEM_GROW_CAPACITY(old_capacity);
        comp->render_sources = GROW_ARRAY(comp->vm, RenderSource,
            comp->render_sources, old_capacity, comp->source_capacity);
        sources = (RenderSource*)comp->render_sources;
    }
    RenderSource* src = &sources[comp->source_count++];
    memset(src, 0, sizeof(RenderSource));
    src->decoder = decoder_create(clip);
    glGenTextures(1, &src->tex_y);
    glGenTextures(1, &src->tex_u);
    glGenTextures(1, &src->tex_v);
    setup_texture_params(src->tex_y);
    setup_texture_params(src->tex_u);
    setup_texture_params(src->tex_v);
    return src;
}
static void bind_yuv_textures(RenderSource* src) {
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src->tex_y);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, src->tex_u);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, src->tex_v);
}
static void layout_and_draw_text(Compositor* comp, TimelineClip* tc, GLint loc_model, GLint loc_uv, mat4 group_model, float scale_x, float scale_y) {
    Clip* clip = tc->media;
    float x_cursor = 0;
    const char* p = clip->text.content;
    while (*p) {
        GlyphInfo* glyph = text_renderer_get_glyph(comp->text_renderer, *p);
        float rel_x = x_cursor + glyph->bearing_x;
        float rel_y = clip->text.font_size - glyph->bearing_y;
        float w = glyph->width;
        float h = glyph->height;
        mat4 local_model = mat4_identity();
        local_model = mat4_mult(mat4_translate(rel_x, rel_y), local_model);
        local_model = mat4_mult(mat4_scale(w * scale_x, h * scale_y), local_model);
        mat4 model = mat4_mult(local_model, group_model);
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, model.m);
        glUniform4f(loc_uv, glyph->u0, glyph->v0, glyph->u1, glyph->v1);
        glBindVertexArray(comp->vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        x_cursor += glyph->advance;
        p++;
    }
}
static void draw_clip_text(Compositor* comp, TimelineClip* tc) {
    Clip* clip = tc->media;
    TextRenderer* tr = comp->text_renderer;
    text_renderer_update(tr, clip);

    glUseProgram(comp->text_shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text_renderer_get_texture(tr));
    glUniform1i(glGetUniformLocation(comp->text_shader_program, "textAtlas"), 0);

    GLint loc_model = glGetUniformLocation(comp->text_shader_program, "u_model");
    GLint loc_uv    = glGetUniformLocation(comp->text_shader_program, "u_uv_rect");
    GLint loc_color = glGetUniformLocation(comp->text_shader_program, "u_color");

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

    // === 先绘制描边（如果启用）===
    if (clip->text.stroke_enabled && clip->text.stroke_width > 0.0f) {
        float stroke_r = clip->text.stroke_color.r / 255.0f;
        float stroke_g = clip->text.stroke_color.g / 255.0f;
        float stroke_b = clip->text.stroke_color.b / 255.0f;
        float stroke_a = (clip->text.stroke_color.a / 255.0f) * tc->transform.opacity;

        glUniform4f(loc_color, stroke_r, stroke_g, stroke_b, stroke_a);

        float offset = clip->text.stroke_width * 0.5f;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                mat4 offset_model = mat4_mult(mat4_translate((float)dx * offset, (float)dy * offset), group_model);
                layout_and_draw_text(comp, tc, loc_model, loc_uv, offset_model, scale_x, scale_y);
            }
        }
    }

    // === 再绘制填充文字（覆盖在描边上方）===
    float fill_r = clip->text.color.r / 255.0f;
    float fill_g = clip->text.color.g / 255.0f;
    float fill_b = clip->text.color.b / 255.0f;
    float fill_a = (clip->text.color.a / 255.0f) * tc->transform.opacity;

    glUniform4f(loc_color, fill_r, fill_g, fill_b, fill_a);
    layout_and_draw_text(comp, tc, loc_model, loc_uv, group_model, scale_x, scale_y);
}

static void draw_clip_rect(Compositor* comp, RenderSource* src, TimelineClip* tc) {
    if (src->tex_y == 0) return;
    glUseProgram(comp->shader_program);
    bind_yuv_textures(src);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_y"), 0);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_u"), 1);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_v"), 2);
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
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_model"), 1, GL_FALSE, model.m);
    glUniform1f(glGetUniformLocation(comp->shader_program, "u_opacity"), opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
static void render_setup(Compositor* comp) {
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glViewport(0, 0, comp->timeline->width, comp->timeline->height);
    u8 r = comp->timeline->background_color.r;
    u8 g = comp->timeline->background_color.g;
    u8 b = comp->timeline->background_color.b;
    glClearColor(r/255.f, g/255.f, b/255.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mat4 proj = mat4_ortho(0, comp->timeline->width, comp->timeline->height, 0, -1, 1);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_projection"), 1, GL_FALSE, proj.m);
    glUseProgram(comp->text_shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->text_shader_program, "u_projection"), 1, GL_FALSE, proj.m);
    if (comp->mixer) mixer_begin_frame(comp->mixer);
}
static void render_clip(Compositor* comp, TimelineClip* tc, double time) {
    double anim_time = time - tc->timeline_start;  // 动画相对时间
    // 新增：更新属性值基于关键帧
    tc->transform.x = (float)evaluate_animation(&tc->anim.x, anim_time);
    tc->transform.y = (float)evaluate_animation(&tc->anim.y, anim_time);
    tc->transform.scale_x = (float)evaluate_animation(&tc->anim.scale_x, anim_time);
    tc->transform.scale_y = (float)evaluate_animation(&tc->anim.scale_y, anim_time);
    tc->transform.rotation = (float)evaluate_animation(&tc->anim.rotation, anim_time);
    tc->transform.opacity = (float)evaluate_animation(&tc->anim.opacity, anim_time);
    tc->media->volume = evaluate_animation(&tc->anim.volume, anim_time);
    if (tc->media->type == CLIP_TYPE_TEXT) {
        tc->media->text.font_size = (uint32_t)round(evaluate_animation(&tc->anim.font_size, anim_time));
    }
    double clip_time = anim_time + tc->source_in;
    if (tc->media->type == CLIP_TYPE_TEXT) {
        draw_clip_text(comp, tc);
    } else {
        RenderSource* src = get_source_safe(comp, tc->media);
        bool new_frame = decoder_update_video(src->decoder, clip_time);
        uint8_t* data[3];
        int linesize[3];
        int fw = 0, fh = 0;
        if (decoder_get_video_data(src->decoder, data, linesize, &fw, &fh)) {
            if (new_frame) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                bool resize = (src->width != fw || src->height != fh);
                if (resize) {
                    src->width = fw;
                    src->height = fh;
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, src->tex_y);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[0]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw, fh,
                                 0, GL_RED, GL_UNSIGNED_BYTE, data[0]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh,
                                    GL_RED, GL_UNSIGNED_BYTE, data[0]);
                }
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, src->tex_u);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[1]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2,
                                 0, GL_RED, GL_UNSIGNED_BYTE, data[1]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2,
                                    GL_RED, GL_UNSIGNED_BYTE, data[1]);
                }
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, src->tex_v);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[2]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2,
                                 0, GL_RED, GL_UNSIGNED_BYTE, data[2]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2,
                                    GL_RED, GL_UNSIGNED_BYTE, data[2]);
                }
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            draw_clip_rect(comp, src, tc);
        }
        if (comp->mixer) {
            mixer_add_source(comp->mixer, src->decoder, (float)tc->media->volume);
        }
    }
}
static void render_cleanup(Compositor* comp) {
    if (comp->mixer) mixer_end_frame(comp->mixer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    comp->cpu_buffer_stale = true;
}
Compositor* compositor_create(VM* vm, Timeline* timeline) {
    Compositor* comp = ALLOCATE(vm, Compositor, 1);
    memset(comp, 0, sizeof(Compositor));
    comp->vm = vm;
    comp->timeline = timeline;
    comp->mixer = mixer_create(44100);
    comp->shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_YUV);
    comp->text_shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_TEXT);
    comp->text_renderer = text_renderer_create();
    float quad[] = {
        0,0, 0,0,
        1,0, 1,0,
        0,1, 0,1,
        0,1, 0,1,
        1,0, 1,0,
        1,1, 1,1
    };
    glGenVertexArrays(1, &comp->vao);
    glGenBuffers(1, &comp->vbo);
    glBindVertexArray(comp->vao);
    glBindBuffer(GL_ARRAY_BUFFER, comp->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glGenFramebuffers(1, &comp->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glGenTextures(1, &comp->output_texture);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, timeline->width, timeline->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->output_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Error: Framebuffer is not complete!\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return comp;
}
void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    if (comp->text_renderer) text_renderer_free(comp->text_renderer);
    glDeleteProgram(comp->text_shader_program);
    if (comp->mixer) mixer_free(comp->mixer);
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for(int i=0; i<comp->source_count; i++) {
        decoder_destroy(sources[i].decoder);
        glDeleteTextures(1, &sources[i].tex_y);
        glDeleteTextures(1, &sources[i].tex_u);
        glDeleteTextures(1, &sources[i].tex_v);
    }
    if (comp->render_sources) {
        free(comp->render_sources);
    }
    if(comp->cpu_output_buffer) free(comp->cpu_output_buffer);
    glDeleteProgram(comp->shader_program);
    glDeleteFramebuffers(1, &comp->fbo);
    glDeleteTextures(1, &comp->output_texture);
    glDeleteBuffers(1, &comp->vbo);
    glDeleteVertexArrays(1, &comp->vao);
    FREE(vm, Compositor, comp);
}
void compositor_render(Compositor* comp, double time) {
    render_setup(comp);
    for(int i=0; i<comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if (!(track->flags & 1)) continue;
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        render_clip(comp, tc, time);
    }
    render_cleanup(comp);
}
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height) {
    static GLuint blit_program = 0;
    if (blit_program == 0) {
        blit_program = build_shader_program(VS_SCREEN, FS_SCREEN);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_width, window_height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(blit_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glUniform1i(glGetUniformLocation(blit_program, "screenTexture"), 0);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
void compositor_read_pixels(Compositor* comp, u8* out_buffer) {
    if (!out_buffer) return;
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    i32 width = comp->timeline->width;
    i32 height = comp->timeline->height;
    i32 stride = width * 4;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out_buffer);
    u8* temp_row = malloc(stride);
    for (int y = 0; y < height / 2; y++) {
        u8* top_row = out_buffer + y * stride;
        u8* bot_row = out_buffer + (height - 1 - y) * stride;
        memcpy(temp_row, top_row, stride);
        memcpy(top_row, bot_row, stride);
        memcpy(bot_row, temp_row, stride);
    }
    free(temp_row);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
u8* compositor_get_cpu_buffer(Compositor* comp) {
    if (comp->cpu_buffer_stale) {
        size_t size = comp->timeline->width * comp->timeline->height * 4;
        if (!comp->cpu_output_buffer) {
            comp->cpu_output_buffer = reallocate(comp->vm, NULL, 0, size);
        }
        compositor_read_pixels(comp, comp->cpu_output_buffer);
        comp->cpu_buffer_stale = false;
    }
    return comp->cpu_output_buffer;
}