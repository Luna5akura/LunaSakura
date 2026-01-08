// src/engine/timeline.c

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "timeline.h"
#include "core/memory.h"
#define INITIAL_TRACK_CAPACITY 4
// === Helper Functions ===
static double get_clip_end_time(TimelineClip* clip) {
    return clip->timeline_start + clip->timeline_duration;
}
// === Lifecycle ===
Timeline* timeline_create(VM* vm, u32 width, u32 height, double fps) {
    Timeline* tl = ALLOCATE(vm, Timeline, 1);
    if (!tl) return NULL;
    memset(tl, 0, sizeof(Timeline));
    tl->width = width;
    tl->height = height;
    tl->fps = fps;
    tl->duration = 0.0;
 
    // Default background: Black
    tl->background_color.r = 0;
    tl->background_color.g = 0;
    tl->background_color.b = 0;
    tl->background_color.a = 255; // Opaque
    // Initialize tracks array
    tl->track_capacity = INITIAL_TRACK_CAPACITY;
    tl->tracks = ALLOCATE(vm, Track, tl->track_capacity);
    memset(tl->tracks, 0, sizeof(Track) * tl->track_capacity);
    tl->track_count = 0;
    return tl;
}
void timeline_free(VM* vm, Timeline* tl) {
    if (!tl) return;
    // 1. Free all tracks and their clips
    for (i32 i = 0; i < (i32)tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        if (track->clips) {
            FREE_ARRAY(vm, TimelineClip, track->clips, track->clip_capacity);
        }
    }
    // 2. Free container
    if (tl->tracks) FREE_ARRAY(vm, Track, tl->tracks, tl->track_capacity);
    FREE(vm, Timeline, tl);
}
// === Track Management ===
i32 timeline_add_track(VM* vm, Timeline* tl) {
    // Resize capacity if needed
    if (tl->track_count >= tl->track_capacity) {
        u32 new_capacity = tl->track_capacity * 2;
        tl->tracks = GROW_ARRAY(vm, Track, tl->tracks, tl->track_capacity, new_capacity);
        if (!tl->tracks) return -1;
     
        // Zero out new slots
        memset(tl->tracks + tl->track_count, 0, (new_capacity - tl->track_count) * sizeof(Track));
     
        tl->track_capacity = new_capacity;
    }
    // Allocate new track
    Track* track = &tl->tracks[tl->track_count];
 
    track->id = tl->track_count;
    track->flags = 1; // visible by default (bit 0)
    snprintf(track->name, sizeof(track->name), "Track %d", track->id + 1);
    track->clip_capacity = 8; // Initial capacity for clips
    track->clips = ALLOCATE(vm, TimelineClip, track->clip_capacity);
    memset(track->clips, 0, sizeof(TimelineClip) * track->clip_capacity);
    track->clip_count = 0;
    track->last_lookup_index = 0;
    track->max_end_time = 0.0;
    return (i32)tl->track_count++;
}
void timeline_remove_track(VM* vm, Timeline* tl, i32 track_index) {
    if (track_index < 0 || track_index >= (i32)tl->track_count) return;
 
    // Free the track clips
    Track* track = &tl->tracks[track_index];
    if (track->clips) FREE_ARRAY(vm, TimelineClip, track->clips, track->clip_capacity);
    // Shift remaining tracks
    for (i32 i = track_index; i < (i32)tl->track_count - 1; i++) {
        tl->tracks[i] = tl->tracks[i + 1];
        tl->tracks[i].id = i;
    }
 
    memset(&tl->tracks[tl->track_count - 1], 0, sizeof(Track));
    tl->track_count--;
    timeline_update_duration(tl);
}
// === Clip Management ===
void timeline_update_duration(Timeline* tl) {
    double max_duration = 0.0;
 
    for (i32 i = 0; i < (i32)tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        max_duration = fmax(max_duration, track->max_end_time);
    }
    tl->duration = max_duration;
}
i32 timeline_add_clip(VM* vm, Timeline* tl, i32 track_index, ObjClip* media, double start_time) {
    if (track_index < 0 || track_index >= (i32)tl->track_count) return -1;
    Track* track = &tl->tracks[track_index];
    // Resize clips if needed
    if (track->clip_count >= track->clip_capacity) {
        u32 new_cap = track->clip_capacity * 2;
        track->clips = GROW_ARRAY(vm, TimelineClip, track->clips, track->clip_capacity, new_cap);
        if (!track->clips) return -1;
        memset(track->clips + track->clip_count, 0, (new_cap - track->clip_count) * sizeof(TimelineClip));
        track->clip_capacity = new_cap;
    }
    // 插入并保持排序 (insertion sort, since additions are rare)
    TimelineClip clip;
    clip.media = media;
    clip.timeline_start = start_time;
    clip.timeline_duration = media->duration; // Default: full length
    clip.source_in = 0.0; // Default: start from beginning
    // Default Transform
    clip.transform.scale_x = (float)media->default_scale_x;
    clip.transform.scale_y = (float)media->default_scale_y;
    clip.transform.x = (float)media->default_x;
    clip.transform.y = (float)media->default_y;
    clip.transform.opacity = (float)media->default_opacity;
    clip.transform.rotation = 0.0f;
    clip.transform.z_index = 0;
    clip.transform._padding = 0;
    // Sorted Insertion (by timeline_start) using binary search
    i32 left = 0, right = track->clip_count - 1;
    i32 insert_idx = track->clip_count;
    while (left <= right) {
        i32 mid = left + (right - left) / 2;
        if (start_time < track->clips[mid].timeline_start) {
            insert_idx = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    if (insert_idx < (i32)track->clip_count) {
        memmove(&track->clips[insert_idx + 1], &track->clips[insert_idx], (track->clip_count - insert_idx) * sizeof(TimelineClip));
    }
    track->clips[insert_idx] = clip;
    track->clip_count++;
    double end = get_clip_end_time(&clip);
    if (end > track->max_end_time) track->max_end_time = end;
    timeline_update_duration(tl);
 
    return insert_idx; // Return index for reference
}
void timeline_remove_clip(Timeline* tl, i32 track_index, i32 clip_index) {
    if (track_index < 0 || track_index >= (i32)tl->track_count || clip_index < 0 || clip_index >= (i32)tl->tracks[track_index].clip_count) return;
    Track* track = &tl->tracks[track_index];
    // Shift remaining clips
    memmove(&track->clips[clip_index], &track->clips[clip_index + 1], (track->clip_count - clip_index - 1) * sizeof(TimelineClip));
    track->clip_count--;
    track->max_end_time = 0.0;
    for (i32 j = 0; j < (i32)track->clip_count; j++) {
        double end = get_clip_end_time(&track->clips[j]);
        if (end > track->max_end_time) track->max_end_time = end;
    }
    timeline_update_duration(tl);
}
// === Query (Hot Path for Renderer) ===
TimelineClip* timeline_get_clip_at(Track* track, double time) {
    if (track->clip_count == 0) return NULL;
 
    // Optimization: Start from last_lookup_index (assuming sequential access)
    i32 i = track->last_lookup_index;
    if (i >= (i32)track->clip_count) i = 0;
 
    // Forward search from cursor
    for (; i < (i32)track->clip_count; i++) {
        TimelineClip* clip = &track->clips[i];
        if (time < clip->timeline_start) break; // Since sorted, no more
        if (time >= clip->timeline_start && time < clip->timeline_start + clip->timeline_duration) {
            track->last_lookup_index = i;
            return clip;
        }
    }
 
    // Backward search if not found (rare)
    for (i = track->last_lookup_index - 1; i >= 0; i--) {
        TimelineClip* clip = &track->clips[i];
        if (time >= clip->timeline_start && time < clip->timeline_start + clip->timeline_duration) {
            track->last_lookup_index = i;
            return clip;
        }
    }
    return NULL;
}
// Placeholder for visible clips (if needed)
void timeline_get_visible_clips(Timeline* tl, double time, TimelineClip*** out_clips, i32* out_count) {
    // Implement if required
    *out_clips = NULL;
    *out_count = 0;
}
Track* timeline_get_track(Timeline* tl, i32 track_index) {
    if (track_index < 0 || track_index >= (i32)tl->track_count) return NULL;
    return &tl->tracks[track_index];
}
void timeline_mark(VM* vm, Timeline* tl) {
    if (!tl) return;
    for (u32 i = 0; i < tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        for (u32 j = 0; j < track->clip_count; j++) {
            TimelineClip* clip = &track->clips[j];
            if (clip->media) {
                // 标记引用的 ObjClip 对象
                markObject(vm, (Obj*)clip->media);
            }
        }
    }
}