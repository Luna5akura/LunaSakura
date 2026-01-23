// src/engine/model/timeline.h

#pragma once

#include "common.h"
#include "allocator.h"
#include "engine/bridge/object.h" // 需要 ObjClip 定义
#include "transform.h"

// === 基础组件 ===

// 时间轴片段

typedef struct {
    Clip* media;            // 修改：使用 Clip* 而非 ObjClip*
    double timeline_start;
    double timeline_duration;
    double source_in;
    Transform transform;
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
    Allocator allocator;

    u32 track_count;
    u32 track_capacity;
} Timeline;

// === API ===

Timeline* timeline_create(Allocator* allocator, u32 width, u32 height, double fps);
void timeline_free(Timeline* tl); // 内部使用 tl->allocator 释放

// 注意：后续操作不需要再传 VM，因为 timeline 内部存了 allocator
i32 timeline_add_track(Timeline* tl); 
void timeline_remove_track(Timeline* tl, i32 track_index);
i32 timeline_add_clip(Timeline* tl, i32 track_index, Clip* media, double start_time);

void timeline_update_duration(Timeline* tl);
void timeline_remove_clip(Timeline* tl, i32 track_index, i32 clip_index);

TimelineClip* timeline_get_clip_at(Track* track, double time);