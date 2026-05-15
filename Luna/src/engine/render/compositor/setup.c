#include "internal.h"
static void init_shader_uniform_cache(ShaderUniformCache* cache) {
    memset(cache, -1, sizeof(*cache));
}

void init_compositor_uniform_caches(Compositor* comp) {
    init_shader_uniform_cache(&comp->yuv_uniforms);
    init_shader_uniform_cache(&comp->image_uniforms);
    init_shader_uniform_cache(&comp->color_uniforms);
    init_shader_uniform_cache(&comp->text_uniforms);
    init_shader_uniform_cache(&comp->copy_uniforms);
    init_shader_uniform_cache(&comp->tint_uniforms);
    init_shader_uniform_cache(&comp->fill_uniforms);
    init_shader_uniform_cache(&comp->gradient_uniforms);
    init_shader_uniform_cache(&comp->grid_uniforms);
    init_shader_uniform_cache(&comp->mosaic_uniforms);
    init_shader_uniform_cache(&comp->brightness_uniforms);
    init_shader_uniform_cache(&comp->blur_uniforms);
    init_shader_uniform_cache(&comp->glow_uniforms);
    init_shader_uniform_cache(&comp->fractal_uniforms);
    init_shader_uniform_cache(&comp->displacement_uniforms);
    init_shader_uniform_cache(&comp->posterize_uniforms);
    comp->adjustment_u_base_texture = -1;
    comp->fractal_u_amount = -1;
    comp->fractal_u_offset = -1;
    comp->fractal_u_invert = -1;
    comp->glow_u_texel_size = -1;
    comp->glow_u_radius = -1;
    comp->glow_u_intensity = -1;
    comp->glow_u_threshold = -1;
    comp->glow_u_softness = -1;
    comp->glow_u_color = -1;
    comp->glow_u_mode = -1;
    comp->displacement_u_offset = -1;
    comp->displacement_u_horizontal_channel = -1;
    comp->displacement_u_vertical_channel = -1;
    comp->displacement_u_use_luma = -1;
    comp->adjustment_u_adjusted_texture = -1;
    comp->adjustment_u_mask_texture = -1;
    comp->adjustment_u_region_center = -1;
    comp->adjustment_u_region_half_size = -1;
    comp->adjustment_u_canvas_size = -1;
    comp->adjustment_u_opacity = -1;
    comp->adjustment_u_feather = -1;
    comp->adjustment_u_region_rotation = -1;
    comp->adjustment_u_blend_mode = -1;
    comp->adjustment_u_mask_mode = -1;
    comp->adjustment_u_whole_frame = -1;
    comp->adjustment_u_has_mask = -1;
    comp->adjustment_u_mask_invert = -1;
}

void cache_uniform_locations(Compositor* comp) {
    comp->yuv_uniforms.u_projection = glGetUniformLocation(comp->shader_program, "u_projection");
    comp->yuv_uniforms.u_model = glGetUniformLocation(comp->shader_program, "u_model");
    comp->yuv_uniforms.u_opacity = glGetUniformLocation(comp->shader_program, "u_opacity");
    comp->yuv_uniforms.sampler0 = glGetUniformLocation(comp->shader_program, "tex_y");
    comp->yuv_uniforms.sampler1 = glGetUniformLocation(comp->shader_program, "tex_u");
    comp->yuv_uniforms.sampler2 = glGetUniformLocation(comp->shader_program, "tex_v");

    comp->image_uniforms.u_projection = glGetUniformLocation(comp->image_shader_program, "u_projection");
    comp->image_uniforms.u_model = glGetUniformLocation(comp->image_shader_program, "u_model");
    comp->image_uniforms.u_opacity = glGetUniformLocation(comp->image_shader_program, "u_opacity");
    comp->image_uniforms.sampler0 = glGetUniformLocation(comp->image_shader_program, "tex_rgba");
    comp->image_uniforms.u_flip_y = glGetUniformLocation(comp->image_shader_program, "u_flip_y");
    comp->image_uniforms.u_premultiplied = glGetUniformLocation(comp->image_shader_program, "u_premultiplied");

    comp->color_uniforms.u_projection = glGetUniformLocation(comp->color_shader_program, "u_projection");
    comp->color_uniforms.u_model = glGetUniformLocation(comp->color_shader_program, "u_model");
    comp->color_uniforms.u_color = glGetUniformLocation(comp->color_shader_program, "u_color");

    comp->text_uniforms.u_projection = glGetUniformLocation(comp->text_shader_program, "u_projection");
    comp->text_uniforms.u_model = glGetUniformLocation(comp->text_shader_program, "u_model");
    comp->text_uniforms.u_uv_rect = glGetUniformLocation(comp->text_shader_program, "u_uv_rect");
    comp->text_uniforms.u_color = glGetUniformLocation(comp->text_shader_program, "u_color");
    comp->text_uniforms.sampler0 = glGetUniformLocation(comp->text_shader_program, "textAtlas");

    comp->copy_uniforms.sampler0 = glGetUniformLocation(comp->copy_shader_program, "u_texture");
    comp->copy_uniforms.u_opacity = glGetUniformLocation(comp->copy_shader_program, "u_opacity");
    comp->tint_uniforms.sampler0 = glGetUniformLocation(comp->tint_shader_program, "u_texture");
    comp->tint_uniforms.u_opacity = glGetUniformLocation(comp->tint_shader_program, "u_amount");
    comp->tint_uniforms.u_color = glGetUniformLocation(comp->tint_shader_program, "u_color");
    comp->fill_uniforms.sampler0 = glGetUniformLocation(comp->fill_shader_program, "u_texture");
    comp->fill_uniforms.u_opacity = glGetUniformLocation(comp->fill_shader_program, "u_amount");
    comp->fill_uniforms.u_color = glGetUniformLocation(comp->fill_shader_program, "u_color");
    comp->brightness_uniforms.sampler0 = glGetUniformLocation(comp->brightness_contrast_shader_program, "u_texture");
    comp->brightness_uniforms.u_projection = glGetUniformLocation(comp->brightness_contrast_shader_program, "u_brightness");
    comp->brightness_uniforms.u_model = glGetUniformLocation(comp->brightness_contrast_shader_program, "u_contrast");
    comp->blur_uniforms.sampler0 = glGetUniformLocation(comp->blur_shader_program, "u_texture");
    comp->blur_uniforms.u_projection = glGetUniformLocation(comp->blur_shader_program, "u_texel_size");
    comp->blur_uniforms.u_opacity = glGetUniformLocation(comp->blur_shader_program, "u_radius");
    comp->blur_uniforms.u_color = glGetUniformLocation(comp->blur_shader_program, "u_direction");
    comp->glow_uniforms.sampler0 = glGetUniformLocation(comp->glow_shader_program, "u_texture");
    comp->glow_u_texel_size = glGetUniformLocation(comp->glow_shader_program, "u_texel_size");
    comp->glow_u_radius = glGetUniformLocation(comp->glow_shader_program, "u_radius");
    comp->glow_u_intensity = glGetUniformLocation(comp->glow_shader_program, "u_intensity");
    comp->glow_u_threshold = glGetUniformLocation(comp->glow_shader_program, "u_threshold");
    comp->glow_u_softness = glGetUniformLocation(comp->glow_shader_program, "u_softness");
    comp->glow_u_color = glGetUniformLocation(comp->glow_shader_program, "u_color");
    comp->glow_u_mode = glGetUniformLocation(comp->glow_shader_program, "u_mode");
    comp->mosaic_uniforms.sampler0 = glGetUniformLocation(comp->mosaic_shader_program, "u_texture");
    comp->mosaic_uniforms.u_projection = glGetUniformLocation(comp->mosaic_shader_program, "u_block_size");
    comp->mosaic_uniforms.u_model = glGetUniformLocation(comp->mosaic_shader_program, "u_resolution");
    comp->mosaic_uniforms.u_opacity = glGetUniformLocation(comp->mosaic_shader_program, "u_sharp_colors");
    comp->grid_uniforms.sampler0 = glGetUniformLocation(comp->grid_shader_program, "u_texture");
    comp->grid_uniforms.u_projection = glGetUniformLocation(comp->grid_shader_program, "u_size");
    comp->grid_uniforms.u_model = glGetUniformLocation(comp->grid_shader_program, "u_line_width");
    comp->grid_uniforms.u_opacity = glGetUniformLocation(comp->grid_shader_program, "u_opacity");
    comp->grid_uniforms.u_color = glGetUniformLocation(comp->grid_shader_program, "u_color");
    comp->gradient_uniforms.sampler0 = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_texture");
    comp->gradient_uniforms.u_projection = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_start");
    comp->gradient_uniforms.u_model = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_end");
    comp->gradient_uniforms.u_color = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_start_color");
    comp->gradient_uniforms.sampler1 = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_end_color");
    comp->gradient_uniforms.u_opacity = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_blend");
    comp->fractal_uniforms.sampler0 = glGetUniformLocation(comp->fractal_noise_shader_program, "u_texture");
    comp->fractal_uniforms.u_projection = glGetUniformLocation(comp->fractal_noise_shader_program, "u_resolution");
    comp->fractal_uniforms.u_model = glGetUniformLocation(comp->fractal_noise_shader_program, "u_scale");
    comp->fractal_uniforms.u_opacity = glGetUniformLocation(comp->fractal_noise_shader_program, "u_evolution");
    comp->fractal_uniforms.u_color = glGetUniformLocation(comp->fractal_noise_shader_program, "u_contrast");
    comp->fractal_uniforms.sampler1 = glGetUniformLocation(comp->fractal_noise_shader_program, "u_brightness");
    comp->fractal_uniforms.sampler2 = glGetUniformLocation(comp->fractal_noise_shader_program, "u_octaves");
    comp->fractal_u_amount = glGetUniformLocation(comp->fractal_noise_shader_program, "u_amount");
    comp->fractal_u_offset = glGetUniformLocation(comp->fractal_noise_shader_program, "u_offset");
    comp->fractal_u_invert = glGetUniformLocation(comp->fractal_noise_shader_program, "u_invert");
    comp->displacement_uniforms.sampler0 = glGetUniformLocation(comp->displacement_map_shader_program, "u_texture");
    comp->displacement_uniforms.sampler1 = glGetUniformLocation(comp->displacement_map_shader_program, "u_map_texture");
    comp->displacement_uniforms.u_projection = glGetUniformLocation(comp->displacement_map_shader_program, "u_resolution");
    comp->displacement_uniforms.u_model = glGetUniformLocation(comp->displacement_map_shader_program, "u_scale_x");
    comp->displacement_uniforms.u_opacity = glGetUniformLocation(comp->displacement_map_shader_program, "u_scale_y");
    comp->displacement_uniforms.u_color = glGetUniformLocation(comp->displacement_map_shader_program, "u_amount");
    comp->displacement_u_offset = glGetUniformLocation(comp->displacement_map_shader_program, "u_offset");
    comp->displacement_u_horizontal_channel = glGetUniformLocation(comp->displacement_map_shader_program, "u_horizontal_channel");
    comp->displacement_u_vertical_channel = glGetUniformLocation(comp->displacement_map_shader_program, "u_vertical_channel");
    comp->displacement_u_use_luma = glGetUniformLocation(comp->displacement_map_shader_program, "u_use_luma");
    comp->posterize_uniforms.sampler0 = glGetUniformLocation(comp->posterize_shader_program, "u_texture");
    comp->posterize_uniforms.u_projection = glGetUniformLocation(comp->posterize_shader_program, "u_levels");
    comp->posterize_uniforms.u_opacity = glGetUniformLocation(comp->posterize_shader_program, "u_amount");

    comp->adjustment_u_base_texture = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_base_texture");
    comp->adjustment_u_adjusted_texture = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_adjusted_texture");
    comp->adjustment_u_mask_texture = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_mask_texture");
    comp->adjustment_u_region_center = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_region_center");
    comp->adjustment_u_region_half_size = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_region_half_size");
    comp->adjustment_u_canvas_size = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_canvas_size");
    comp->adjustment_u_opacity = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_opacity");
    comp->adjustment_u_feather = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_feather");
    comp->adjustment_u_region_rotation = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_region_rotation");
    comp->adjustment_u_blend_mode = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_blend_mode");
    comp->adjustment_u_mask_mode = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_mask_mode");
    comp->adjustment_u_whole_frame = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_whole_frame");
    comp->adjustment_u_has_mask = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_has_mask");
    comp->adjustment_u_mask_invert = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_mask_invert");
}
void setup_texture_params(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
RenderSource* get_source_safe(Compositor* comp, Clip* clip) {
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for(int i=0; i<comp->source_count; i++) {
        if (sources[i].type == RENDER_SOURCE_MEDIA &&
            sources[i].decoder &&
            decoder_get_clip_ref(sources[i].decoder) == clip) {
            return &sources[i];
        }
        if (sources[i].type == RENDER_SOURCE_IMAGE && sources[i].clip_ref == clip) {
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
    src->clip_ref = clip;
    if (clip->type == CLIP_TYPE_IMAGE) {
        uint8_t* pixels = NULL;
        int width = 0;
        int height = 0;

        src->type = RENDER_SOURCE_IMAGE;
        glGenTextures(1, &src->tex_rgba);
        setup_texture_params(src->tex_rgba);
        if (image_load_rgba(clip->path, &pixels, &width, &height)) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src->tex_rgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            src->width = width;
            src->height = height;
            src->image_loaded = true;
            free(pixels);
        }
        return src;
    }
    src->type = RENDER_SOURCE_MEDIA;
    src->decoder = decoder_create(clip);
    glGenTextures(1, &src->tex_y);
    glGenTextures(1, &src->tex_u);
    glGenTextures(1, &src->tex_v);
    setup_texture_params(src->tex_y);
    setup_texture_params(src->tex_u);
    setup_texture_params(src->tex_v);
    return src;
}

Compositor* get_nested_compositor_safe(Compositor* comp, Clip* clip, Timeline* nested) {
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for (int i = 0; i < comp->source_count; i++) {
        if (sources[i].type == RENDER_SOURCE_NESTED && sources[i].clip_ref == clip) {
            if (sources[i].nested_timeline != nested) {
                if (sources[i].nested_compositor) {
                    compositor_free(comp->vm, sources[i].nested_compositor);
                }
                sources[i].nested_timeline = nested;
                sources[i].nested_compositor = nested ? compositor_create(comp->vm, nested) : NULL;
                if (sources[i].nested_compositor && sources[i].nested_compositor->mixer) {
                    mixer_free(sources[i].nested_compositor->mixer);
                    sources[i].nested_compositor->mixer = NULL;
                }
            }
            return sources[i].nested_compositor;
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
    src->type = RENDER_SOURCE_NESTED;
    src->clip_ref = clip;
    src->nested_timeline = nested;
    src->nested_compositor = nested ? compositor_create(comp->vm, nested) : NULL;
    if (src->nested_compositor && src->nested_compositor->mixer) {
        mixer_free(src->nested_compositor->mixer);
        src->nested_compositor->mixer = NULL;
    }
    return src->nested_compositor;
}
