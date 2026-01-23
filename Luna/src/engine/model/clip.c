// src/engine/model/clip.c

#include "engine/model/clip.h"
#include <stdlib.h>
#include <string.h>

Clip* clip_create(const char* path) {
    Clip* clip = (Clip*)malloc(sizeof(Clip));
    if (!clip) return NULL;

    memset(clip, 0, sizeof(Clip));

    if (path) {
        // 深拷贝路径字符串，确保 Clip 拥有其生命周期
        size_t len = strlen(path);
        clip->path = (char*)malloc(len + 1);
        if (clip->path) {
            memcpy(clip->path, path, len + 1);
        }
    }

    // 默认值初始化
    clip->default_scale_x = 1.0;
    clip->default_scale_y = 1.0;
    clip->default_opacity = 1.0;
    clip->volume = 1.0;
    clip->user_data = NULL;

    return clip;
}

void clip_free(Clip* clip) {
    if (!clip) return;

    if (clip->path) {
        free(clip->path);
    }
    // user_data 由 VM 管理，这里不负责释放
    free(clip);
}