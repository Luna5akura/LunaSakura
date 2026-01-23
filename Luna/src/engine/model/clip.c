// src/engine/model/clip.c

#include "engine/model/clip.h"
#include <stdlib.h>
#include <string.h>

static void init_defaults(Clip* c) {
    c->default_scale_x = 1.0;
    c->default_scale_y = 1.0;
    c->default_opacity = 1.0;
    c->volume = 1.0;
    c->fps = 30.0; // 默认
}

Clip* clip_create_media(const char* path) {
    Clip* c = malloc(sizeof(Clip));
    memset(c, 0, sizeof(Clip));
    c->type = CLIP_TYPE_MEDIA;
    if (path) c->path = strdup(path);
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
    
    c->duration = 5.0; // 文字默认时长5秒
    init_defaults(c);
    return c;
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