// src/engine/model/timeline.c

#include "engine/model/timeline.h"
#include <stdio.h>
#include <string.h>

#define INITIAL_TRACK_CAPACITY 4

// === Helper Functions ===
static double get_clip_end_time(TimelineClip* clip) {
    return clip->timeline_start + clip->timeline_duration;
}

// === Lifecycle ===
Timeline* timeline_create(Allocator* allocator, uint32_t width, uint32_t height, double fps) {
    // 使用通用宏 MEM_ALLOC
    Timeline* tl = MEM_ALLOC(allocator, Timeline, 1);
    if (!tl) return NULL;
    memset(tl, 0, sizeof(Timeline));
   
    // 保存分配器上下文
    tl->allocator = *allocator;
    tl->width = width;
    tl->height = height;
    tl->fps = fps;
    tl->background_color.a = 255;
    tl->track_capacity = 4;
    // 使用 MEM_ALLOC
    tl->tracks = MEM_ALLOC(allocator, Track, tl->track_capacity);
    memset(tl->tracks, 0, sizeof(Track) * tl->track_capacity);
   
    return tl;
}

void timeline_free(Timeline* tl) {
    if (!tl) return;
    Allocator* a = &tl->allocator; // 获取保存的分配器
   
    for (int i = 0; i < (int)tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        if (track->clips) {
            // 使用 MEM_FREE_ARRAY
            MEM_FREE_ARRAY(a, TimelineClip, track->clips, track->clip_capacity);
        }
    }
   
    MEM_FREE_ARRAY(a, Track, tl->tracks, tl->track_capacity);
    MEM_FREE(a, Timeline, tl); // 释放自身，此时 a 指针失效，但函数也结束了
}

// === Track Management ===
i32 timeline_add_track(Timeline* tl) {
    Allocator* a = &tl->allocator;
    // Resize capacity if needed
    if (tl->track_count >= tl->track_capacity) {
        u32 new_capacity = MEM_GROW_CAPACITY(tl->track_capacity);
        tl->tracks = MEM_GROW_ARRAY(a, Track, tl->tracks, tl->track_capacity, new_capacity);
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
    track->clips = MEM_ALLOC(a, TimelineClip, track->clip_capacity);
    memset(track->clips, 0, sizeof(TimelineClip) * track->clip_capacity);
   
    track->clip_count = 0;
    track->last_lookup_index = 0;
    track->max_end_time = 0.0;
   
    return (i32)tl->track_count++;
}

void timeline_remove_track(Timeline* tl, i32 track_index) {
    Allocator* a = &tl->allocator;
    if (track_index < 0 || track_index >= (i32)tl->track_count) return;
    // Free the track clips
    Track* track = &tl->tracks[track_index];
    if (track->clips) MEM_FREE_ARRAY(a, TimelineClip, track->clips, track->clip_capacity);
   
    // Shift remaining tracks
    for (i32 i = track_index; i < (i32)tl->track_count - 1; i++) {
        tl->tracks[i] = tl->tracks[i + 1];
        tl->tracks[i].id = i; // Update ID logic if necessary
    }
    memset(&tl->tracks[tl->track_count - 1], 0, sizeof(Track));
    tl->track_count--;
    timeline_update_duration(tl);
}

// === Clip Management ===
void timeline_update_duration(Timeline* tl) {
    double max_duration = 0.0;
    for (i32 i = 0; i < (int)tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        if (track->max_end_time > max_duration) {
            max_duration = track->max_end_time;
        }
    }
    tl->duration = max_duration;
}

i32 timeline_add_clip(Timeline* tl, i32 track_index, Clip* media, double start_time) {
    Allocator* a = &tl->allocator;
    if (track_index < 0 || track_index >= (i32)tl->track_count) return -1;
    Track* track = &tl->tracks[track_index];
   
    // Resize clips if needed
    if (track->clip_count >= track->clip_capacity) {
        u32 new_cap = track->clip_capacity * 2;
        track->clips = MEM_GROW_ARRAY(a, TimelineClip, track->clips, track->clip_capacity, new_cap);
        if (!track->clips) return -1;
        // Zero init is optional here since we will memmove, but good for safety
        memset(track->clips + track->clip_count, 0, (new_cap - track->clip_count) * sizeof(TimelineClip));
        track->clip_capacity = new_cap;
    }
   
    // Create Instance
    TimelineClip clip;
    memset(&clip, 0, sizeof(TimelineClip));
    clip.media = media;
    clip.timeline_start = start_time;
    clip.timeline_duration = media->duration; // Default: full length
    clip.source_in = 0.0;
   
    // Default Transform
    clip.transform.scale_x = (float)media->default_scale_x;
    clip.transform.scale_y = (float)media->default_scale_y;
    clip.transform.x = (float)media->default_x;
    clip.transform.y = (float)media->default_y;
    clip.transform.opacity = (float)media->default_opacity;
    clip.transform.rotation = (float)media->default_rotation;
    clip.transform.z_index = 0;
    // Sorted Insertion (Insertion Sort / Binary Search)
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
   
    // Shift data
    if (insert_idx < (i32)track->clip_count) {
        memmove(&track->clips[insert_idx + 1],
                &track->clips[insert_idx],
                (track->clip_count - insert_idx) * sizeof(TimelineClip));
    }
   
    track->clips[insert_idx] = clip;
    track->clip_count++;
   
    double end = get_clip_end_time(&clip);
    if (end > track->max_end_time) track->max_end_time = end;
    timeline_update_duration(tl);
    return insert_idx;
}

void timeline_remove_clip(Timeline* tl, i32 track_index, i32 clip_index) {
    if (track_index < 0 || track_index >= (i32)tl->track_count) return;
    Track* track = &tl->tracks[track_index];
    if (clip_index < 0 || clip_index >= (i32)track->clip_count) return;
    // Shift remaining clips
    memmove(&track->clips[clip_index],
            &track->clips[clip_index + 1],
            (track->clip_count - clip_index - 1) * sizeof(TimelineClip));
           
    track->clip_count--;
   
    // Recalculate max_end_time
    track->max_end_time = 0.0;
    for (i32 j = 0; j < (i32)track->clip_count; j++) {
        double end = get_clip_end_time(&track->clips[j]);
        if (end > track->max_end_time) track->max_end_time = end;
    }
    timeline_update_duration(tl);
}

// === Query ===
TimelineClip* timeline_get_clip_at(Track* track, double time) {
    if (track->clip_count == 0) return NULL;
    // Optimization: Start from last_lookup_index
    i32 i = track->last_lookup_index;
    if (i >= (i32)track->clip_count) i = 0;
    // Forward search
    for (; i < (i32)track->clip_count; i++) {
        TimelineClip* clip = &track->clips[i];
        // Since sorted, if clip starts after time, we can stop early
        if (time < clip->timeline_start) break;
       
        if (time >= clip->timeline_start && time < clip->timeline_start + clip->timeline_duration) {
            track->last_lookup_index = i;
            return clip;
        }
    }
    // Backward search (if cursor moved back)
    for (i = track->last_lookup_index - 1; i >= 0; i--) {
        TimelineClip* clip = &track->clips[i];
        if (time >= clip->timeline_start && time < clip->timeline_start + clip->timeline_duration) {
            track->last_lookup_index = i;
            return clip;
        }
    }
   
    return NULL;
}