// src/engine/model/timeline.h

#pragma once
#include "common.h"
#include "allocator.h"
#include "engine/model/clip.h"
#include "transform.h"
#include "animation.h"  // 新增
#include "engine/effect/filter_base.h"

// === 基础组件 ===
typedef struct {
    Clip* media;
    double timeline_start;
    double timeline_duration;
    double source_in;
    Transform transform;
    struct {  // 新增动画系统
        Animation x;
        Animation y;
        Animation scale_x;
        Animation scale_y;
        Animation rotation;
        Animation opacity;
        Animation volume;
        Animation font_size;  // 用于文字图层等
    } anim;
    EffectInstance* effectChain;   // 新增：效果链
} TimelineClip;

typedef struct {
    i32 id;
    u8 flags; // bit 0: visible, bit 1: locked
    char name[27];
   
    TimelineClip* clips;
    u32 clip_count;
    u32 clip_capacity;
   
    i32 last_lookup_index;
    double max_end_time;
} Track;

typedef struct Timeline {
    u32 width;
    u32 height;
    double fps;
    double duration;
    struct { u8 r, g, b, a; } background_color;
   
    Track* tracks;
    Allocator allocator;
    u32 track_count;
    u32 track_capacity;
} Timeline;

// === API ===
Timeline* timeline_create(Allocator* allocator, u32 width, u32 height, double fps);
void timeline_free(Timeline* tl);
i32 timeline_add_track(Timeline* tl);
void timeline_remove_track(Timeline* tl, i32 track_index);
i32 timeline_add_clip(Timeline* tl, i32 track_index, Clip* media, double start_time);
void timeline_update_duration(Timeline* tl);
void timeline_remove_clip(Timeline* tl, i32 track_index, i32 clip_index);
TimelineClip* timeline_get_clip_at(Track* track, double time);