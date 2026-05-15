#include "internal.h"
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
    glUniformMatrix4fv(comp->yuv_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->image_shader_program);
    glUniformMatrix4fv(comp->image_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->color_shader_program);
    glUniformMatrix4fv(comp->color_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->text_shader_program);
    glUniformMatrix4fv(comp->text_uniforms.u_projection, 1, GL_FALSE, proj.m);
    if (comp->mixer) mixer_begin_frame(comp->mixer);
    comp->preview_prefers_nearest = false;
}

typedef struct {
    TimelineClip* clip;
    int track_index;
} ActiveClip;

static int compare_active_clips(const void* a, const void* b) {
    const ActiveClip* aa = (const ActiveClip*)a;
    const ActiveClip* bb = (const ActiveClip*)b;
    if (aa->clip->transform.z_index != bb->clip->transform.z_index) {
        return aa->clip->transform.z_index - bb->clip->transform.z_index;
    }
    return aa->track_index - bb->track_index;
}

static bool active_clip_precedes(const ActiveClip* a, const ActiveClip* b) {
    if (a->clip->transform.z_index != b->clip->transform.z_index) {
        return a->clip->transform.z_index < b->clip->transform.z_index;
    }
    return a->track_index <= b->track_index;
}

static void sort_active_clips(ActiveClip* clips, int count) {
    bool sorted = true;

    if (!clips || count < 2) return;
    for (int i = 1; i < count; i++) {
        if (!active_clip_precedes(&clips[i - 1], &clips[i])) {
            sorted = false;
            break;
        }
    }
    if (sorted) return;

    if (count <= 16) {
        for (int i = 1; i < count; i++) {
            ActiveClip current = clips[i];
            int j = i - 1;
            while (j >= 0 && !active_clip_precedes(&clips[j], &current)) {
                clips[j + 1] = clips[j];
                j--;
            }
            clips[j + 1] = current;
        }
        return;
    }

    qsort(clips, (size_t)count, sizeof(ActiveClip), compare_active_clips);
}

static void render_clip(Compositor* comp, TimelineClip* tc, double time) {
    double anim_time = time - tc->timeline_start;  // 动画相对时间
    // 新增：更新属性值基于关键帧
    evaluate_clip_transform_state(comp, tc, time,
                                  &tc->transform.x, &tc->transform.y,
                                  &tc->transform.scale_x, &tc->transform.scale_y,
                                  &tc->transform.rotation, &tc->transform.opacity);
    tc->media->volume = evaluate_animation(&tc->anim.volume, anim_time);
    resolve_clip_layout_recursive(comp, tc, time, 0, &tc->transform.x, &tc->transform.y, NULL, NULL);
    double clip_time = anim_time + tc->source_in;
    if (tc->effectChain) {
        render_clip_with_effects(comp, tc, clip_time);
        if (comp->mixer && tc->media->type == CLIP_TYPE_MEDIA) {
            RenderSource* src = get_source_safe(comp, tc->media);
            mixer_add_source(comp->mixer, src->decoder, (float)tc->media->volume);
        }
        return;
    }
    if (tc->media->type == CLIP_TYPE_ADJUSTMENT) {
        return;
    }
    if (tc->media->type == CLIP_TYPE_TEXT) {
        draw_clip_text(comp, tc, clip_time);
    } else if (tc->media->type == CLIP_TYPE_IMAGE) {
        RenderSource* src = get_source_safe(comp, tc->media);
        draw_clip_rgba(comp, src, tc);
    } else if (tc->media->type == CLIP_TYPE_SOLID) {
        draw_clip_solid(comp, tc);
    } else if (is_nested_timeline_clip(tc->media)) {
        ensure_effect_targets(comp, (int)tc->media->width, (int)tc->media->height);
        render_nested_timeline_to_target(comp, tc, clip_time, comp->effect_source_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
        glViewport(0, 0, comp->timeline->width, comp->timeline->height);
        draw_texture_transformed(comp, comp->effect_source_texture, tc,
                                 (float)tc->media->width, (float)tc->media->height, false, false);
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
    comp->image_shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_RGBA);
    comp->color_shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_COLOR);
    comp->text_shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_TEXT);
    comp->copy_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_COPY);
    comp->tint_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_TINT);
    comp->fill_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_FILL);
    comp->gradient_ramp_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_GRADIENT_RAMP);
    comp->grid_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_GRID);
    comp->mosaic_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_MOSAIC);
    comp->brightness_contrast_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_BRIGHTNESS_CONTRAST);
    comp->blur_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_BLUR);
    comp->glow_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_GLOW);
    comp->fractal_noise_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_FRACTAL_NOISE);
    comp->displacement_map_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_DISPLACEMENT_MAP);
    comp->posterize_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_POSTERIZE);
    comp->adjustment_composite_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_ADJUSTMENT_COMPOSITE);
    comp->text_renderer = text_renderer_create();
    init_compositor_uniform_caches(comp);
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
    cache_uniform_locations(comp);
    return comp;
}
void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    if (comp->text_renderer) text_renderer_free(comp->text_renderer);
    glDeleteProgram(comp->text_shader_program);
    glDeleteProgram(comp->image_shader_program);
    glDeleteProgram(comp->color_shader_program);
    glDeleteProgram(comp->copy_shader_program);
    glDeleteProgram(comp->tint_shader_program);
    glDeleteProgram(comp->fill_shader_program);
    glDeleteProgram(comp->gradient_ramp_shader_program);
    glDeleteProgram(comp->grid_shader_program);
    glDeleteProgram(comp->mosaic_shader_program);
    glDeleteProgram(comp->brightness_contrast_shader_program);
    glDeleteProgram(comp->blur_shader_program);
    glDeleteProgram(comp->glow_shader_program);
    glDeleteProgram(comp->fractal_noise_shader_program);
    glDeleteProgram(comp->displacement_map_shader_program);
    glDeleteProgram(comp->posterize_shader_program);
    glDeleteProgram(comp->adjustment_composite_shader_program);
    if (comp->mixer) mixer_free(comp->mixer);
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for(int i=0; i<comp->source_count; i++) {
        if (sources[i].type == RENDER_SOURCE_MEDIA) {
            decoder_destroy(sources[i].decoder);
            glDeleteTextures(1, &sources[i].tex_y);
            glDeleteTextures(1, &sources[i].tex_u);
            glDeleteTextures(1, &sources[i].tex_v);
        } else if (sources[i].type == RENDER_SOURCE_IMAGE) {
            glDeleteTextures(1, &sources[i].tex_rgba);
        } else if (sources[i].type == RENDER_SOURCE_NESTED && sources[i].nested_compositor) {
            compositor_free(vm, sources[i].nested_compositor);
        }
    }
    if (comp->render_sources) {
        free(comp->render_sources);
    }
    if(comp->cpu_output_buffer) free(comp->cpu_output_buffer);
    if(comp->cpu_flip_row) free(comp->cpu_flip_row);
    if(comp->active_clips_buffer) free(comp->active_clips_buffer);
    glDeleteProgram(comp->shader_program);
    if (comp->effect_source_fbo) glDeleteFramebuffers(1, &comp->effect_source_fbo);
    if (comp->effect_ping_fbo) glDeleteFramebuffers(1, &comp->effect_ping_fbo);
    if (comp->effect_base_fbo) glDeleteFramebuffers(1, &comp->effect_base_fbo);
    if (comp->effect_aux_fbo) glDeleteFramebuffers(1, &comp->effect_aux_fbo);
    if (comp->effect_source_texture) glDeleteTextures(1, &comp->effect_source_texture);
    if (comp->effect_ping_texture) glDeleteTextures(1, &comp->effect_ping_texture);
    if (comp->effect_base_texture) glDeleteTextures(1, &comp->effect_base_texture);
    if (comp->effect_aux_texture) glDeleteTextures(1, &comp->effect_aux_texture);
    glDeleteFramebuffers(1, &comp->fbo);
    glDeleteTextures(1, &comp->output_texture);
    glDeleteBuffers(1, &comp->vbo);
    glDeleteVertexArrays(1, &comp->vao);
    FREE(vm, Compositor, comp);
}
void compositor_render(Compositor* comp, double time) {
    ActiveClip* active_clips;
    int active_count = 0;

    render_setup(comp);

    if (comp->active_clips_capacity < (int)comp->timeline->track_count) {
        comp->active_clips_capacity = (int)comp->timeline->track_count;
        comp->active_clips_buffer = realloc(comp->active_clips_buffer, sizeof(ActiveClip) * (size_t)comp->active_clips_capacity);
    }
    active_clips = (ActiveClip*)comp->active_clips_buffer;
    for(int i=0; i<comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if (!(track->flags & 1)) continue;
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        if (!(tc->flags & 1)) continue;
        active_clips[active_count].clip = tc;
        active_clips[active_count].track_index = i;
        active_count++;
    }

    sort_active_clips(active_clips, active_count);
    for (int i = 0; i < active_count; i++) {
        render_clip(comp, active_clips[i].clip, time);
    }
    render_cleanup(comp);
}
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height) {
    static GLuint blit_program = 0;
    static GLint blit_sampler_loc = -1;
    if (blit_program == 0) {
        blit_program = build_shader_program(VS_SCREEN, FS_SCREEN);
        blit_sampler_loc = glGetUniformLocation(blit_program, "screenTexture");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_width, window_height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(blit_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    set_texture_sampling(comp->output_texture, comp->preview_prefers_nearest);
    glUniform1i(blit_sampler_loc, 0);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (comp->preview_prefers_nearest) {
        set_texture_sampling(comp->output_texture, false);
    }
}
void compositor_read_pixels(Compositor* comp, u8* out_buffer) {
    if (!out_buffer) return;
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    i32 width = comp->timeline->width;
    i32 height = comp->timeline->height;
    i32 stride = width * 4;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out_buffer);
    if (comp->cpu_flip_row_capacity < (size_t)stride) {
        comp->cpu_flip_row = realloc(comp->cpu_flip_row, (size_t)stride);
        comp->cpu_flip_row_capacity = (size_t)stride;
    }
    for (int y = 0; y < height / 2; y++) {
        u8* top_row = out_buffer + y * stride;
        u8* bot_row = out_buffer + (height - 1 - y) * stride;
        memcpy(comp->cpu_flip_row, top_row, stride);
        memcpy(top_row, bot_row, stride);
        memcpy(bot_row, comp->cpu_flip_row, stride);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

u8* compositor_get_cpu_buffer_raw(Compositor* comp) {
    if (comp->cpu_buffer_stale) {
        size_t size = comp->timeline->width * comp->timeline->height * 4;
        if (!comp->cpu_output_buffer) {
            comp->cpu_output_buffer = reallocate(comp->vm, NULL, 0, size);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, comp->timeline->width, comp->timeline->height, GL_RGBA, GL_UNSIGNED_BYTE, comp->cpu_output_buffer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        comp->cpu_buffer_stale = false;
        comp->cpu_buffer_flipped = false;
    }
    return comp->cpu_output_buffer;
}

u8* compositor_get_cpu_buffer(Compositor* comp) {
    u8* buffer = compositor_get_cpu_buffer_raw(comp);
    i32 width;
    i32 height;
    i32 stride;

    if (!buffer) return NULL;
    if (comp->cpu_buffer_flipped) return buffer;

    width = comp->timeline->width;
    height = comp->timeline->height;
    stride = width * 4;
    if (comp->cpu_flip_row_capacity < (size_t)stride) {
        comp->cpu_flip_row = realloc(comp->cpu_flip_row, (size_t)stride);
        comp->cpu_flip_row_capacity = (size_t)stride;
    }
    for (int y = 0; y < height / 2; y++) {
        u8* top_row = buffer + y * stride;
        u8* bot_row = buffer + (height - 1 - y) * stride;
        memcpy(comp->cpu_flip_row, top_row, stride);
        memcpy(top_row, bot_row, stride);
        memcpy(bot_row, comp->cpu_flip_row, stride);
    }
    comp->cpu_buffer_flipped = true;
    return buffer;
}
