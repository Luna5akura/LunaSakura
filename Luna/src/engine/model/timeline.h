// src/engine/model/timeline.h

#pragma once

#include "common.h" // 假设你有这个，或者使用 <stdint.h>
#include "engine/binding/object.h" // 需要 ObjClip 定义
#include "transform.h"

typedef struct VM VM;

// === 基础组件 ===

// 时间轴片段
typedef struct {
    ObjClip* media;         // 8 bytes
    double timeline_start;  // 8 bytes
    double timeline_duration; // 8 bytes
    double source_in;       // 8 bytes
    Transform transform;    // 32 bytes
    u32 _padding;           // padding to 64 bytes cache line
} TimelineClip;

// === 容器结构 ===

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
    u32 track_count;
    u32 track_capacity;
} Timeline;

// === API ===
Timeline* timeline_create(VM* vm, u32 width, u32 height, double fps);
void timeline_free(VM* vm, Timeline* tl);

i32 timeline_add_track(VM* vm, Timeline* tl);
void timeline_remove_track(VM* vm, Timeline* tl, i32 track_index);

i32 timeline_add_clip(VM* vm, Timeline* tl, i32 track_index, ObjClip* media, double start_time);
void timeline_remove_clip(Timeline* tl, i32 track_index, i32 clip_index);

TimelineClip* timeline_get_clip_at(Track* track, double time);
void timeline_update_duration(Timeline* tl);
void timeline_mark(VM* vm, Timeline* tl);