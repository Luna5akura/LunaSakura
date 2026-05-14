// src/engine/model/clip.c

#include "engine/model/clip.h"
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
        if (clip->text.content) free(clip->text.content);
        if (clip->text.font_path) free(clip->text.font_path);
    }
  
    free(clip);
}
