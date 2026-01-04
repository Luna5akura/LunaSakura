// src/engine/timeline.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/timeline.h"

#define INITIAL_TRACK_CAPACITY 4

// === Helper Functions ===
static double get_clip_end_time(TimelineClip* clip) {
    return clip->timeline_start + clip->timeline_duration;
}

// === Lifecycle ===
Timeline* timeline_create(u32 width, u32 height, double fps) {
    Timeline* tl = (Timeline*)malloc(sizeof(Timeline));
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
    tl->tracks = (Track*)calloc(tl->track_capacity, sizeof(Track));
    tl->track_count = 0;
    return tl;
}

void timeline_free(Timeline* tl) {
    if (!tl) return;
    // 1. Free all tracks and their clips
    for (int i = 0; i < tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        if (track->clips) {
            free(track->clips);
        }
    }
    // 2. Free container
    if (tl->tracks) free(tl->tracks);
    free(tl);
}

// === Track Management ===
int timeline_add_track(Timeline* tl) {
    // Resize capacity if needed
    if (tl->track_count >= tl->track_capacity) {
        int new_capacity = tl->track_capacity * 2;
        Track* new_tracks = (Track*)realloc(tl->tracks, new_capacity * sizeof(Track));
        if (!new_tracks) return -1;
       
        // Zero out new slots
        memset(new_tracks + tl->track_count, 0, (new_capacity - tl->track_count) * sizeof(Track));
       
        tl->tracks = new_tracks;
        tl->track_capacity = new_capacity;
    }
    // Allocate new track
    Track* track = &tl->tracks[tl->track_count];
   
    track->id = tl->track_count;
    track->flags = 1; // visible by default (bit 0)
    snprintf(track->name, sizeof(track->name), "Track %d", track->id + 1);
    track->clip_capacity = 8; // Initial capacity for clips
    track->clips = (TimelineClip*)calloc(track->clip_capacity, sizeof(TimelineClip));
    track->clip_count = 0;
    track->last_lookup_index = 0;
    return tl->track_count++;
}

void timeline_remove_track(Timeline* tl, int track_index) {
    if (track_index < 0 || track_index >= tl->track_count) return;
   
    // Free the track clips
    Track* track = &tl->tracks[track_index];
    if (track->clips) free(track->clips);
    // Shift remaining tracks
    for (int i = track_index; i < tl->track_count - 1; i++) {
        tl->tracks[i] = tl->tracks[i + 1];
        tl->tracks[i].id = i;
    }
   
    memset(&tl->tracks[tl->track_count - 1], 0, sizeof(Track));
    tl->track_count--;
}

// === Clip Management ===
void timeline_update_duration(Timeline* tl) {
    double max_duration = 0.0;
   
    for (int i = 0; i < tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        for (int j = 0; j < track->clip_count; j++) {
            double end = get_clip_end_time(&track->clips[j]);
            if (end > max_duration) max_duration = end;
        }
    }
    tl->duration = max_duration;
}

int timeline_add_clip(Timeline* tl, int track_index, ObjClip* media, double start_time) {
    if (track_index < 0 || track_index >= tl->track_count) return -1;
    Track* track = &tl->tracks[track_index];
    // Resize clips if needed
    if (track->clip_count >= track->clip_capacity) {
        int new_cap = track->clip_capacity * 2;
        TimelineClip* new_clips = (TimelineClip*)realloc(track->clips, new_cap * sizeof(TimelineClip));
        if (!new_clips) return -1;
        memset(new_clips + track->clip_count, 0, (new_cap - track->clip_count) * sizeof(TimelineClip));
        track->clips = new_clips;
        track->clip_capacity = new_cap;
    }
    // 插入并保持排序 (insertion sort, since additions are rare)
    TimelineClip* clip = &track->clips[track->clip_count];
    clip->media = media;
    clip->timeline_start = start_time;
    clip->timeline_duration = media->duration; // Default: full length
    clip->source_in = 0.0; // Default: start from beginning
    // Default Transform
    clip->transform.scale_x = (float)media->default_scale_x;
    clip->transform.scale_y = (float)media->default_scale_y;
    clip->transform.x = (float)media->default_x;
    clip->transform.y = (float)media->default_y;
   
    clip->transform.opacity = 1.0f;
    clip->transform.rotation = 0.0f;
    // Sorted Insertion (by timeline_start)
    int insert_idx = track->clip_count;
    for (int i = 0; i < track->clip_count; i++) {
        if (start_time < track->clips[i].timeline_start) {
            insert_idx = i;
            break;
        }
    }
    if (insert_idx < track->clip_count) {
        memmove(&track->clips[insert_idx + 1], &track->clips[insert_idx], (track->clip_count - insert_idx) * sizeof(TimelineClip));
    }
    track->clips[insert_idx] = *clip;
    track->clip_count++;
    timeline_update_duration(tl);
   
    return insert_idx; // Return index for reference
}

void timeline_remove_clip(Timeline* tl, int track_index, int clip_index) {
    if (track_index < 0 || track_index >= tl->track_count || clip_index < 0 || clip_index >= tl->tracks[track_index].clip_count) return;
    Track* track = &tl->tracks[track_index];
    // Shift remaining clips
    memmove(&track->clips[clip_index], &track->clips[clip_index + 1], (track->clip_count - clip_index - 1) * sizeof(TimelineClip));
    track->clip_count--;
    timeline_update_duration(tl);
}

// === Query (Hot Path for Renderer) ===
TimelineClip* timeline_get_clip_at(Track* track, double time) {
    if (track->clip_count == 0) return NULL;
   
    // Optimization: Start from last_lookup_index (assuming sequential access)
    int i = track->last_lookup_index;
    if (i >= track->clip_count) i = 0;
   
    // Forward search from cursor
    for (; i < track->clip_count; i++) {
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
void timeline_get_visible_clips(Timeline* tl, double time, TimelineClip*** out_clips, int* out_count) {
    // Implement if required
    *out_clips = NULL;
    *out_count = 0;
}