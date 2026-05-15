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
    u32 id;
    Clip* media;
    double timeline_start;
    double timeline_duration;
    double source_in;
    u8 flags; // bit 0: visible
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
    u8 position_mode;
    u8 alignment_target_type;
    u8 self_anchor;
    u8 target_anchor;
    u32 alignment_target_clip_id;
} TimelineClip;

typedef enum {
    TIMELINE_POSITION_MODE_POSITION = 0,
    TIMELINE_POSITION_MODE_ANCHOR = 1
} TimelinePositionMode;

typedef enum {
    TIMELINE_ALIGNMENT_TARGET_COMPOSITION = 0,
    TIMELINE_ALIGNMENT_TARGET_CLIP = 1
} TimelineAlignmentTargetType;

typedef enum {
    TIMELINE_ANCHOR_TOP_LEFT = 0,
    TIMELINE_ANCHOR_TOP_CENTER = 1,
    TIMELINE_ANCHOR_TOP_RIGHT = 2,
    TIMELINE_ANCHOR_CENTER_LEFT = 3,
    TIMELINE_ANCHOR_CENTER = 4,
    TIMELINE_ANCHOR_CENTER_RIGHT = 5,
    TIMELINE_ANCHOR_BOTTOM_LEFT = 6,
    TIMELINE_ANCHOR_BOTTOM_CENTER = 7,
    TIMELINE_ANCHOR_BOTTOM_RIGHT = 8
} TimelineAnchorPoint;

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
    u32 next_clip_id;
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
TimelineClip* timeline_get_clip(Timeline* tl, i32 track_index, i32 clip_index);
i32 timeline_get_clip_count(Timeline* tl, i32 track_index);
TimelineClip* timeline_find_clip_by_id(Timeline* tl, u32 clip_id, i32* out_track_index, i32* out_clip_index);
bool timeline_remove_clip_by_id(Timeline* tl, u32 clip_id);
bool timeline_move_clip_by_id(Timeline* tl, u32 clip_id, i32 to_track_index, double new_start_time);
u32 timeline_duplicate_clip_by_id(Timeline* tl, u32 clip_id, i32 to_track_index, double new_start_time);
