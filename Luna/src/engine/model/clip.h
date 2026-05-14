// src/engine/model/clip.h

#pragma once
#include "common.h"
#include "engine/model/transform.h"

typedef enum {
    CLIP_TYPE_MEDIA,
    CLIP_TYPE_TEXT,
    CLIP_TYPE_IMAGE,
    CLIP_TYPE_SOLID,
    CLIP_TYPE_ADJUSTMENT,
    CLIP_TYPE_GROUP,
    CLIP_TYPE_PRECOMP
} ClipType;

typedef struct {
    u32 source_width;
    u32 source_height;
} ImageData;

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

typedef struct {
    struct { u8 r, g, b, a; } color;
} SolidData;

typedef struct {
    u8 blend_mode;
    u8 mask_mode;
    bool affects_whole_frame;
    bool mask_invert;
    double feather;
    u32 mask_source_clip_id;
} AdjustmentData;

typedef struct Timeline Timeline;

typedef struct {
    Timeline* timeline;
} NestedTimelineData;

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
    ImageData image;
    // --- TEXT 属性 ---
    TextData text;
    SolidData solid;
    AdjustmentData adjustment;
    NestedTimelineData nested_timeline;
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
Clip* clip_create_image(const char* path, u32 width, u32 height);
// r,g,b range: 0-255
Clip* clip_create_text(const char* content, const char* font_path, u32 size, u8 r, u8 g, u8 b);
Clip* clip_create_solid(u32 width, u32 height, u8 r, u8 g, u8 b, u8 a);
Clip* clip_create_adjustment(u32 width, u32 height);
Clip* clip_create_group(u32 width, u32 height, double fps);
Clip* clip_create_precomp(u32 width, u32 height, double fps);
void clip_free(Clip* clip);
