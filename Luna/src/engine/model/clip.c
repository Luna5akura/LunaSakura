// src/engine/model/clip.c

#include "engine/model/clip.h"
#include "engine/bridge/object.h"
#include "engine/model/animation.h"
#include <stdlib.h>
#include <string.h>

static void init_defaults(Clip* c) {
    c->default_scale_x = 1.0;
    c->default_scale_y = 1.0;
    c->default_opacity = 1.0;
    c->default_rotation = 0.0;
    c->volume = 1.0;
    c->fps = 30.0;
}

static Allocator* resolve_clip_allocator(Clip* clip) {
    ObjClip* owner = clip && clip->user_data ? (ObjClip*)clip->user_data : NULL;
    return owner ? &owner->allocator : NULL;
}

static void init_text_range_selector(TextRangeSelector* selector, Allocator* allocator) {
    memset(selector, 0, sizeof(*selector));
    init_animation(&selector->start, allocator, 0.0);
    init_animation(&selector->end, allocator, 100.0);
    init_animation(&selector->offset, allocator, 0.0);
    init_animation(&selector->amount, allocator, 100.0);
    init_animation(&selector->ease_high, allocator, 0.0);
    init_animation(&selector->ease_low, allocator, 0.0);
    selector->based_on = TEXT_SELECTOR_BASED_ON_CHARACTERS;
    selector->shape = TEXT_SELECTOR_SHAPE_SQUARE;
    selector->mode = TEXT_SELECTOR_MODE_ADD;
}

static void free_text_range_selector(TextRangeSelector* selector, Allocator* allocator) {
    free_animation(&selector->start, allocator);
    free_animation(&selector->end, allocator);
    free_animation(&selector->offset, allocator);
    free_animation(&selector->amount, allocator);
    free_animation(&selector->ease_high, allocator);
    free_animation(&selector->ease_low, allocator);
}

static void init_text_expression_selector(TextExpressionSelector* selector, Allocator* allocator) {
    memset(selector, 0, sizeof(*selector));
    init_animation(&selector->amount, allocator, 100.0);
    selector->based_on = TEXT_SELECTOR_BASED_ON_CHARACTERS;
    selector->expression = strdup("100");
    selector->callback = NIL_VAL;
    selector->has_callback = false;
    selector->mode = TEXT_SELECTOR_MODE_ADD;
}

static void free_text_expression_selector(TextExpressionSelector* selector, Allocator* allocator) {
    free_animation(&selector->amount, allocator);
    if (selector->expression) {
        free(selector->expression);
        selector->expression = NULL;
    }
}

static void init_text_wiggly_selector(TextWigglySelector* selector, Allocator* allocator) {
    memset(selector, 0, sizeof(*selector));
    init_animation(&selector->amount, allocator, 100.0);
    init_animation(&selector->wiggles_per_second, allocator, 2.0);
    init_animation(&selector->correlation, allocator, 50.0);
    init_animation(&selector->temporal_phase, allocator, 0.0);
    init_animation(&selector->spatial_phase, allocator, 0.0);
    init_animation(&selector->min_amount, allocator, 0.0);
    init_animation(&selector->max_amount, allocator, 100.0);
    selector->based_on = TEXT_SELECTOR_BASED_ON_CHARACTERS;
    selector->mode = TEXT_SELECTOR_MODE_ADD;
}

static void free_text_wiggly_selector(TextWigglySelector* selector, Allocator* allocator) {
    free_animation(&selector->amount, allocator);
    free_animation(&selector->wiggles_per_second, allocator);
    free_animation(&selector->correlation, allocator);
    free_animation(&selector->temporal_phase, allocator);
    free_animation(&selector->spatial_phase, allocator);
    free_animation(&selector->min_amount, allocator);
    free_animation(&selector->max_amount, allocator);
}

static void init_text_animator(TextAnimator* animator, Allocator* allocator) {
    memset(animator, 0, sizeof(*animator));
    animator->allocator = allocator;
    init_animation(&animator->x, allocator, 0.0);
    init_animation(&animator->y, allocator, 0.0);
    init_animation(&animator->scale_x, allocator, 0.0);
    init_animation(&animator->scale_y, allocator, 0.0);
    init_animation(&animator->rotation, allocator, 0.0);
    init_animation(&animator->opacity, allocator, 0.0);
    init_animation(&animator->tracking, allocator, 0.0);
    init_animation(&animator->stroke_width, allocator, 0.0);
    init_animation(&animator->anchor_x, allocator, 0.0);
    init_animation(&animator->anchor_y, allocator, 0.0);
    init_animation(&animator->skew, allocator, 0.0);
    init_animation(&animator->skew_axis, allocator, 0.0);
    init_animation(&animator->fill_opacity, allocator, 0.0);
    init_animation(&animator->stroke_opacity, allocator, 0.0);
    init_animation(&animator->fill_hue, allocator, 0.0);
    init_animation(&animator->fill_saturation, allocator, 0.0);
    init_animation(&animator->fill_brightness, allocator, 0.0);
    init_animation(&animator->stroke_hue, allocator, 0.0);
    init_animation(&animator->stroke_saturation, allocator, 0.0);
    init_animation(&animator->stroke_brightness, allocator, 0.0);
    init_animation(&animator->character_offset, allocator, 0.0);
    init_animation(&animator->character_value, allocator, 0.0);
    for (int i = 0; i < 4; i++) {
        init_animation(&animator->fill_color[i], allocator, 255.0);
        init_animation(&animator->stroke_color[i], allocator, 0.0);
    }
    animator->fill_color[3].default_value = 0.0;
    animator->stroke_color[3].default_value = 0.0;
}

static void free_text_animator(TextAnimator* animator, Allocator* allocator) {
    free_animation(&animator->x, allocator);
    free_animation(&animator->y, allocator);
    free_animation(&animator->scale_x, allocator);
    free_animation(&animator->scale_y, allocator);
    free_animation(&animator->rotation, allocator);
    free_animation(&animator->opacity, allocator);
    free_animation(&animator->tracking, allocator);
    free_animation(&animator->stroke_width, allocator);
    free_animation(&animator->anchor_x, allocator);
    free_animation(&animator->anchor_y, allocator);
    free_animation(&animator->skew, allocator);
    free_animation(&animator->skew_axis, allocator);
    free_animation(&animator->fill_opacity, allocator);
    free_animation(&animator->stroke_opacity, allocator);
    free_animation(&animator->fill_hue, allocator);
    free_animation(&animator->fill_saturation, allocator);
    free_animation(&animator->fill_brightness, allocator);
    free_animation(&animator->stroke_hue, allocator);
    free_animation(&animator->stroke_saturation, allocator);
    free_animation(&animator->stroke_brightness, allocator);
    free_animation(&animator->character_offset, allocator);
    free_animation(&animator->character_value, allocator);
    for (int i = 0; i < 4; i++) {
        free_animation(&animator->fill_color[i], allocator);
        free_animation(&animator->stroke_color[i], allocator);
    }
    if (animator->range_selectors) {
        for (u32 i = 0; i < animator->range_selector_count; i++) {
            free_text_range_selector(&animator->range_selectors[i], allocator);
        }
        MEM_FREE_ARRAY(allocator, TextRangeSelector, animator->range_selectors, animator->range_selector_capacity);
    }
    if (animator->expression_selectors) {
        for (u32 i = 0; i < animator->expression_selector_count; i++) {
            free_text_expression_selector(&animator->expression_selectors[i], allocator);
        }
        MEM_FREE_ARRAY(allocator, TextExpressionSelector, animator->expression_selectors,
                       animator->expression_selector_capacity);
    }
    if (animator->wiggly_selectors) {
        for (u32 i = 0; i < animator->wiggly_selector_count; i++) {
            free_text_wiggly_selector(&animator->wiggly_selectors[i], allocator);
        }
        MEM_FREE_ARRAY(allocator, TextWigglySelector, animator->wiggly_selectors,
                       animator->wiggly_selector_capacity);
    }
}

Clip* clip_create_media(const char* path) {
    Clip* c = malloc(sizeof(Clip));
    memset(c, 0, sizeof(Clip));
    c->type = CLIP_TYPE_MEDIA;
    if (path) c->path = strdup(path);
    init_defaults(c);
    return c;
}

Clip* clip_create_image(const char* path, u32 width, u32 height) {
    Clip* c = malloc(sizeof(Clip));
    memset(c, 0, sizeof(Clip));
    c->type = CLIP_TYPE_IMAGE;
    if (path) c->path = strdup(path);
    c->duration = 5.0;
    c->has_video = true;
    c->has_audio = false;
    c->width = width;
    c->height = height;
    c->image.source_width = width;
    c->image.source_height = height;
    init_defaults(c);
    return c;
}

Clip* clip_create_text(const char* content, const char* font_path, u32 size, u8 r, u8 g, u8 b) {
    Clip* c = malloc(sizeof(Clip));
    memset(c, 0, sizeof(Clip));
    c->type = CLIP_TYPE_TEXT;
  
    c->text.content = content ? strdup(content) : NULL;
    c->text.font_path = font_path ? strdup(font_path) : NULL;
    c->text.font_size = size;
    c->text.color.r = r; c->text.color.g = g; c->text.color.b = b; c->text.color.a = 255;
  
    c->duration = 5.0; // 默认 5 秒
    init_defaults(c);
    return c;
}

Clip* clip_create_solid(u32 width, u32 height, u8 r, u8 g, u8 b, u8 a) {
    Clip* c = malloc(sizeof(Clip));
    memset(c, 0, sizeof(Clip));
    c->type = CLIP_TYPE_SOLID;
    c->duration = 5.0;
    c->has_video = true;
    c->has_audio = false;
    c->width = width;
    c->height = height;
    c->solid.color.r = r;
    c->solid.color.g = g;
    c->solid.color.b = b;
    c->solid.color.a = a;
    init_defaults(c);
    return c;
}

Clip* clip_create_adjustment(u32 width, u32 height) {
    Clip* c = malloc(sizeof(Clip));
    memset(c, 0, sizeof(Clip));
    c->type = CLIP_TYPE_ADJUSTMENT;
    c->duration = 5.0;
    c->has_video = false;
    c->has_audio = false;
    c->width = width;
    c->height = height;
    c->adjustment.blend_mode = 0;
    c->adjustment.mask_mode = 0;
    c->adjustment.affects_whole_frame = true;
    c->adjustment.mask_invert = false;
    c->adjustment.feather = 0.0;
    c->adjustment.mask_source_clip_id = 0;
    init_defaults(c);
    return c;
}

static Clip* clip_create_nested_base(ClipType type, u32 width, u32 height, double fps) {
    Clip* c = malloc(sizeof(Clip));
    memset(c, 0, sizeof(Clip));
    c->type = type;
    c->duration = 5.0;
    c->has_video = true;
    c->has_audio = false;
    c->width = width;
    c->height = height;
    init_defaults(c);
    c->fps = fps;
    return c;
}

Clip* clip_create_group(u32 width, u32 height, double fps) {
    return clip_create_nested_base(CLIP_TYPE_GROUP, width, height, fps);
}

Clip* clip_create_precomp(u32 width, u32 height, double fps) {
    return clip_create_nested_base(CLIP_TYPE_PRECOMP, width, height, fps);
}

void clip_free(Clip* clip) {
    if (!clip) return;
    if (clip->path) free(clip->path);
  
    if (clip->type == CLIP_TYPE_TEXT) {
        Allocator* allocator = resolve_clip_allocator(clip);
        if (clip->text.content) free(clip->text.content);
        if (clip->text.font_path) free(clip->text.font_path);
        if (allocator && clip->text.animators) {
            for (u32 i = 0; i < clip->text.animator_count; i++) {
                free_text_animator(&clip->text.animators[i], allocator);
            }
            MEM_FREE_ARRAY(allocator, TextAnimator, clip->text.animators, clip->text.animator_capacity);
        }
    }
  
    free(clip);
}

TextAnimator* clip_text_add_animator(Clip* clip) {
    Allocator* allocator;
    TextAnimator* new_animators;
    u32 new_capacity;

    if (!clip || clip->type != CLIP_TYPE_TEXT) return NULL;
    allocator = resolve_clip_allocator(clip);
    if (!allocator) return NULL;
    if (clip->text.animator_count >= clip->text.animator_capacity) {
        new_capacity = clip->text.animator_capacity < 4 ? 4 : clip->text.animator_capacity * 2;
        new_animators = MEM_GROW_ARRAY(allocator, TextAnimator, clip->text.animators,
                                       clip->text.animator_capacity, new_capacity);
        if (!new_animators) return NULL;
        clip->text.animators = new_animators;
        memset(clip->text.animators + clip->text.animator_capacity, 0,
               (new_capacity - clip->text.animator_capacity) * sizeof(TextAnimator));
        clip->text.animator_capacity = new_capacity;
    }
    init_text_animator(&clip->text.animators[clip->text.animator_count], allocator);
    return &clip->text.animators[clip->text.animator_count++];
}

u32 clip_text_get_animator_count(const Clip* clip) {
    return (clip && clip->type == CLIP_TYPE_TEXT) ? clip->text.animator_count : 0;
}

TextAnimator* clip_text_get_animator(Clip* clip, u32 index) {
    if (!clip || clip->type != CLIP_TYPE_TEXT || index >= clip->text.animator_count) return NULL;
    return &clip->text.animators[index];
}

TextRangeSelector* text_animator_add_range_selector(TextAnimator* animator) {
    Allocator* allocator;
    TextRangeSelector* new_selectors;
    u32 new_capacity;
    if (!animator) return NULL;
    allocator = animator->allocator;
    if (!allocator) return NULL;
    if (animator->range_selector_count >= animator->range_selector_capacity) {
        new_capacity = animator->range_selector_capacity < 4 ? 4 : animator->range_selector_capacity * 2;
        new_selectors = MEM_GROW_ARRAY(allocator, TextRangeSelector, animator->range_selectors,
                                       animator->range_selector_capacity, new_capacity);
        if (!new_selectors) return NULL;
        animator->range_selectors = new_selectors;
        memset(animator->range_selectors + animator->range_selector_capacity, 0,
               (new_capacity - animator->range_selector_capacity) * sizeof(TextRangeSelector));
        animator->range_selector_capacity = new_capacity;
    }
    init_text_range_selector(&animator->range_selectors[animator->range_selector_count], allocator);
    return &animator->range_selectors[animator->range_selector_count++];
}

u32 text_animator_get_range_selector_count(const TextAnimator* animator) {
    return animator ? animator->range_selector_count : 0;
}

TextRangeSelector* text_animator_get_range_selector(TextAnimator* animator, u32 index) {
    if (!animator || index >= animator->range_selector_count) return NULL;
    return &animator->range_selectors[index];
}

TextExpressionSelector* text_animator_add_expression_selector(TextAnimator* animator) {
    Allocator* allocator;
    TextExpressionSelector* new_selectors;
    u32 new_capacity;
    if (!animator) return NULL;
    allocator = animator->allocator;
    if (!allocator) return NULL;
    if (animator->expression_selector_count >= animator->expression_selector_capacity) {
        new_capacity = animator->expression_selector_capacity < 4 ? 4 : animator->expression_selector_capacity * 2;
        new_selectors = MEM_GROW_ARRAY(allocator, TextExpressionSelector, animator->expression_selectors,
                                       animator->expression_selector_capacity, new_capacity);
        if (!new_selectors) return NULL;
        animator->expression_selectors = new_selectors;
        memset(animator->expression_selectors + animator->expression_selector_capacity, 0,
               (new_capacity - animator->expression_selector_capacity) * sizeof(TextExpressionSelector));
        animator->expression_selector_capacity = new_capacity;
    }
    init_text_expression_selector(&animator->expression_selectors[animator->expression_selector_count], allocator);
    return &animator->expression_selectors[animator->expression_selector_count++];
}

u32 text_animator_get_expression_selector_count(const TextAnimator* animator) {
    return animator ? animator->expression_selector_count : 0;
}

TextExpressionSelector* text_animator_get_expression_selector(TextAnimator* animator, u32 index) {
    if (!animator || index >= animator->expression_selector_count) return NULL;
    return &animator->expression_selectors[index];
}

TextWigglySelector* text_animator_add_wiggly_selector(TextAnimator* animator) {
    Allocator* allocator;
    TextWigglySelector* new_selectors;
    u32 new_capacity;
    if (!animator) return NULL;
    allocator = animator->allocator;
    if (!allocator) return NULL;
    if (animator->wiggly_selector_count >= animator->wiggly_selector_capacity) {
        new_capacity = animator->wiggly_selector_capacity < 4 ? 4 : animator->wiggly_selector_capacity * 2;
        new_selectors = MEM_GROW_ARRAY(allocator, TextWigglySelector, animator->wiggly_selectors,
                                       animator->wiggly_selector_capacity, new_capacity);
        if (!new_selectors) return NULL;
        animator->wiggly_selectors = new_selectors;
        memset(animator->wiggly_selectors + animator->wiggly_selector_capacity, 0,
               (new_capacity - animator->wiggly_selector_capacity) * sizeof(TextWigglySelector));
        animator->wiggly_selector_capacity = new_capacity;
    }
    init_text_wiggly_selector(&animator->wiggly_selectors[animator->wiggly_selector_count], allocator);
    return &animator->wiggly_selectors[animator->wiggly_selector_count++];
}

u32 text_animator_get_wiggly_selector_count(const TextAnimator* animator) {
    return animator ? animator->wiggly_selector_count : 0;
}

TextWigglySelector* text_animator_get_wiggly_selector(TextAnimator* animator, u32 index) {
    if (!animator || index >= animator->wiggly_selector_count) return NULL;
    return &animator->wiggly_selectors[index];
}
