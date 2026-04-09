// src/engine/model/clip.h

#pragma once
#include "common.h"
#include "engine/model/transform.h"

typedef enum {
    CLIP_TYPE_MEDIA,
    CLIP_TYPE_TEXT
} ClipType;

typedef struct {
    char* content;
    char* font_path;
    u32   font_size;
    struct { u8 r, g, b, a; } color;          // 填充颜色

    // === 新增 AE 风格属性 ===
    float letter_spacing;                     // 字符间距（tracking），单位：像素，默认为 0.0
    bool  stroke_enabled;                     // 是否启用描边
    float stroke_width;                       // 描边宽度（像素）
    struct { u8 r, g, b, a; } stroke_color;  // 描边颜色

    // 缓存
    float cached_width;
    float cached_height;
} TextData;

typedef struct Clip {
    void* user_data;
    ClipType type;
    // --- 通用属性 ---
    char* path; // MEDIA: 文件路径; TEXT: 可为空
    double duration;
    double start_time;
    double in_point;
    double out_point;
    double fps;
    // --- MEDIA 属性 ---
    bool has_video;
    bool has_audio;
    i32 audio_channels;
    i32 audio_sample_rate;
    // --- TEXT 属性 ---
    TextData text;
    // --- 变换属性 ---
    double default_scale_x;
    double default_scale_y;
    double default_x;
    double default_y;
    double default_rotation;
    double default_opacity;
    double volume;
    u32 width;
    u32 height;
    i32 layer;
} Clip;

Clip* clip_create_media(const char* path);
// r,g,b range: 0-255
Clip* clip_create_text(const char* content, const char* font_path, u32 size, u8 r, u8 g, u8 b);
void clip_free(Clip* clip);