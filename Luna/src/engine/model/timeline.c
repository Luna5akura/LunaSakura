// src/engine/model/timeline.c

#include "engine/model/timeline.h"
#include <stdio.h>
#include <string.h>

#define INITIAL_TRACK_CAPACITY 4

// === Helper Functions ===
static void effect_chain_free(Allocator* a, EffectInstance* chain) {
    while (chain) {
        EffectInstance* next = chain->next;
        if (chain->data && chain->processor && chain->processor->destroy) {
            chain->processor->destroy(a, chain->data);
        }
        MEM_FREE(a, EffectInstance, chain);
        chain = next;
    }
}

static double get_clip_end_time(TimelineClip* clip) {
    return clip->timeline_start + clip->timeline_duration;
}

static double recompute_track_max_end_time(Track* track) {
    double max_end = 0.0;
    for (u32 i = 0; i < track->clip_count; i++) {
        double end = get_clip_end_time(&track->clips[i]);
        if (end > max_end) max_end = end;
    }
    track->max_end_time = max_end;
    return max_end;
}

static void refresh_timeline_duration_from_tracks(Timeline* tl) {
    double max_duration = 0.0;
    for (u32 i = 0; i < tl->track_count; i++) {
        if (tl->tracks[i].max_end_time > max_duration) {
            max_duration = tl->tracks[i].max_end_time;
        }
    }
    tl->duration = max_duration;
}

static void free_timeline_clip(Allocator* a, TimelineClip* tc) {
    free_animation(&tc->anim.x, a);
    free_animation(&tc->anim.y, a);
    free_animation(&tc->anim.scale_x, a);
    free_animation(&tc->anim.scale_y, a);
    free_animation(&tc->anim.rotation, a);
    free_animation(&tc->anim.opacity, a);
    free_animation(&tc->anim.volume, a);
    free_animation(&tc->anim.font_size, a);
    effect_chain_free(a, tc->effectChain);
}

static void ensure_track_clip_capacity(Allocator* a, Track* track) {
    if (track->clip_count < track->clip_capacity) return;
    {
        u32 new_cap = track->clip_capacity * 2;
        track->clips = MEM_GROW_ARRAY(a, TimelineClip, track->clips, track->clip_capacity, new_cap);
        memset(track->clips + track->clip_count, 0, (new_cap - track->clip_count) * sizeof(TimelineClip));
        track->clip_capacity = new_cap;
    }
}

static i32 find_insert_index_for_start(Track* track, double start_time) {
    i32 left = 0;
    i32 right = track->clip_count - 1;
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

    return insert_idx;
}

static i32 insert_existing_clip(Timeline* tl, i32 track_index, TimelineClip clip) {
    Track* track = &tl->tracks[track_index];
    i32 insert_idx;

    ensure_track_clip_capacity(&tl->allocator, track);
    insert_idx = find_insert_index_for_start(track, clip.timeline_start);
    if (insert_idx < (i32)track->clip_count) {
        memmove(
            &track->clips[insert_idx + 1],
            &track->clips[insert_idx],
            (track->clip_count - insert_idx) * sizeof(TimelineClip)
        );
    }
    track->clips[insert_idx] = clip;
    track->clip_count++;
    return insert_idx;
}

static void copy_timeline_clip(Allocator* a, TimelineClip* dst, const TimelineClip* src) {
    memset(dst, 0, sizeof(TimelineClip));
    *dst = *src;
    dst->effectChain = NULL;

    init_animation(&dst->anim.x, a, src->anim.x.default_value);
    copy_keyframes(&dst->anim.x, a, &src->anim.x);
    init_animation(&dst->anim.y, a, src->anim.y.default_value);
    copy_keyframes(&dst->anim.y, a, &src->anim.y);
    init_animation(&dst->anim.scale_x, a, src->anim.scale_x.default_value);
    copy_keyframes(&dst->anim.scale_x, a, &src->anim.scale_x);
    init_animation(&dst->anim.scale_y, a, src->anim.scale_y.default_value);
    copy_keyframes(&dst->anim.scale_y, a, &src->anim.scale_y);
    init_animation(&dst->anim.rotation, a, src->anim.rotation.default_value);
    copy_keyframes(&dst->anim.rotation, a, &src->anim.rotation);
    init_animation(&dst->anim.opacity, a, src->anim.opacity.default_value);
    copy_keyframes(&dst->anim.opacity, a, &src->anim.opacity);
    init_animation(&dst->anim.volume, a, src->anim.volume.default_value);
    copy_keyframes(&dst->anim.volume, a, &src->anim.volume);
    init_animation(&dst->anim.font_size, a, src->anim.font_size.default_value);
    copy_keyframes(&dst->anim.font_size, a, &src->anim.font_size);
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
    tl->next_clip_id = 1;
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
        for (uint32_t j = 0; j < track->clip_count; j++) {
            TimelineClip* tc = &track->clips[j];
            free_timeline_clip(a, tc);
        }
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
    for (uint32_t j = 0; j < track->clip_count; j++) {
        TimelineClip* tc = &track->clips[j];
        free_timeline_clip(a, tc);
    }
    if (track->clips) MEM_FREE_ARRAY(a, TimelineClip, track->clips, track->clip_capacity);
  
    // Shift remaining tracks
    for (i32 i = track_index; i < (i32)tl->track_count - 1; i++) {
        tl->tracks[i] = tl->tracks[i + 1];
        tl->tracks[i].id = i; // Update ID logic if necessary
    }
    memset(&tl->tracks[tl->track_count - 1], 0, sizeof(Track));
    tl->track_count--;
    refresh_timeline_duration_from_tracks(tl);
}

// === Clip Management ===
void timeline_update_duration(Timeline* tl) {
    double max_duration = 0.0;
    for (i32 i = 0; i < (int)tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        double track_max_end_time = 0.0;
        for (i32 j = 0; j < (i32)track->clip_count; j++) {
            double end = get_clip_end_time(&track->clips[j]);
            if (end > track_max_end_time) {
                track_max_end_time = end;
            }
        }
        track->max_end_time = track_max_end_time;
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
  
    ensure_track_clip_capacity(a, track);
  
    // Create Instance
    TimelineClip clip;
    memset(&clip, 0, sizeof(TimelineClip));
    clip.id = tl->next_clip_id++;
    clip.media = media;
    clip.timeline_start = start_time;
    clip.timeline_duration = media->duration; // Default: full length
    clip.source_in = media->in_point;
    clip.flags = 1;
    clip.effectChain = NULL;                    // 新增：初始化效果链
  
    // Default Transform
    clip.transform.scale_x = (float)media->default_scale_x;
    clip.transform.scale_y = (float)media->default_scale_y;
    clip.transform.x = (float)media->default_x;
    clip.transform.y = (float)media->default_y;
    clip.transform.opacity = (float)media->default_opacity;
    clip.transform.rotation = (float)media->default_rotation;
    clip.transform.z_index = 0;
  
    // 新增：初始化动画
    init_animation(&clip.anim.x, a, media->default_x);
    init_animation(&clip.anim.y, a, media->default_y);
    init_animation(&clip.anim.scale_x, a, media->default_scale_x);
    init_animation(&clip.anim.scale_y, a, media->default_scale_y);
    init_animation(&clip.anim.rotation, a, media->default_rotation);
    init_animation(&clip.anim.opacity, a, media->default_opacity);
    init_animation(&clip.anim.volume, a, media->volume);
    init_animation(&clip.anim.font_size, a, (media->type == CLIP_TYPE_TEXT ? (double)media->text.font_size : 0.0));
  
    i32 insert_idx = insert_existing_clip(tl, track_index, clip);
  
    double end = get_clip_end_time(&clip);
    if (end > track->max_end_time) track->max_end_time = end;
    if (end > tl->duration) tl->duration = end;
    return insert_idx;
}

void timeline_remove_clip(Timeline* tl, i32 track_index, i32 clip_index) {
    if (track_index < 0 || track_index >= (i32)tl->track_count) return;
    Track* track = &tl->tracks[track_index];
    if (clip_index < 0 || clip_index >= (i32)track->clip_count) return;
    Allocator* a = &tl->allocator;
    TimelineClip* tc = &track->clips[clip_index];
    double removed_end = get_clip_end_time(tc);
    free_timeline_clip(a, tc);
    // Shift remaining clips
    memmove(&track->clips[clip_index],
            &track->clips[clip_index + 1],
            (track->clip_count - clip_index - 1) * sizeof(TimelineClip));
          
    track->clip_count--;
    if (track->last_lookup_index >= (i32)track->clip_count) {
        track->last_lookup_index = (i32)track->clip_count - 1;
    }

    if (removed_end >= track->max_end_time) {
        recompute_track_max_end_time(track);
        if (removed_end >= tl->duration) {
            refresh_timeline_duration_from_tracks(tl);
        }
    }
}

// === Query ===
TimelineClip* timeline_get_clip_at(Track* track, double time) {
    if (track->clip_count == 0) return NULL;
    if (time < track->clips[0].timeline_start || time >= track->max_end_time) return NULL;
    i32 i = track->last_lookup_index;
    if (i >= (i32)track->clip_count) i = 0;
    if (i >= 0 && i < (i32)track->clip_count) {
        TimelineClip* last = &track->clips[i];
        double last_end = last->timeline_start + last->timeline_duration;
        if (time >= last->timeline_start && time < last_end) {
            return last;
        }
        if (i + 1 < (i32)track->clip_count) {
            TimelineClip* next = &track->clips[i + 1];
            double next_end = next->timeline_start + next->timeline_duration;
            if (time >= next->timeline_start && time < next_end) {
                track->last_lookup_index = i + 1;
                return next;
            }
        }
    }
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

TimelineClip* timeline_get_clip(Timeline* tl, i32 track_index, i32 clip_index) {
    Track* track;
    if (track_index < 0 || track_index >= (i32)tl->track_count) return NULL;
    track = &tl->tracks[track_index];
    if (clip_index < 0 || clip_index >= (i32)track->clip_count) return NULL;
    return &track->clips[clip_index];
}

i32 timeline_get_clip_count(Timeline* tl, i32 track_index) {
    if (track_index < 0 || track_index >= (i32)tl->track_count) return -1;
    return (i32)tl->tracks[track_index].clip_count;
}

TimelineClip* timeline_find_clip_by_id(Timeline* tl, u32 clip_id, i32* out_track_index, i32* out_clip_index) {
    for (i32 i = 0; i < (i32)tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        for (i32 j = 0; j < (i32)track->clip_count; j++) {
            if (track->clips[j].id == clip_id) {
                if (out_track_index) *out_track_index = i;
                if (out_clip_index) *out_clip_index = j;
                return &track->clips[j];
            }
        }
    }
    return NULL;
}

bool timeline_remove_clip_by_id(Timeline* tl, u32 clip_id) {
    i32 track_index;
    i32 clip_index;
    if (!timeline_find_clip_by_id(tl, clip_id, &track_index, &clip_index)) return false;
    timeline_remove_clip(tl, track_index, clip_index);
    return true;
}

bool timeline_move_clip_by_id(Timeline* tl, u32 clip_id, i32 to_track_index, double new_start_time) {
    i32 from_track_index;
    i32 clip_index;
    TimelineClip clip;
    Track* from_track;
    double removed_end;
    bool moved_from_max_track;
    double inserted_end;

    if (to_track_index < 0) return false;
    while (tl->track_count <= (u32)to_track_index) {
        timeline_add_track(tl);
    }
    if (!timeline_find_clip_by_id(tl, clip_id, &from_track_index, &clip_index)) return false;

    from_track = &tl->tracks[from_track_index];
    clip = from_track->clips[clip_index];
    removed_end = get_clip_end_time(&clip);
    moved_from_max_track = removed_end >= from_track->max_end_time;
    if (clip_index < (i32)from_track->clip_count - 1) {
        memmove(
            &from_track->clips[clip_index],
            &from_track->clips[clip_index + 1],
            (from_track->clip_count - clip_index - 1) * sizeof(TimelineClip)
        );
    }
    from_track->clip_count--;
    if (from_track->last_lookup_index >= (i32)from_track->clip_count) {
        from_track->last_lookup_index = (i32)from_track->clip_count - 1;
    }

    clip.timeline_start = new_start_time;
    insert_existing_clip(tl, to_track_index, clip);
    inserted_end = get_clip_end_time(&clip);
    if (inserted_end > tl->tracks[to_track_index].max_end_time) {
        tl->tracks[to_track_index].max_end_time = inserted_end;
    }
    if (inserted_end > tl->duration) {
        tl->duration = inserted_end;
    }
    if (moved_from_max_track) {
        recompute_track_max_end_time(from_track);
        if (removed_end >= tl->duration) {
            refresh_timeline_duration_from_tracks(tl);
        }
    }
    return true;
}

u32 timeline_duplicate_clip_by_id(Timeline* tl, u32 clip_id, i32 to_track_index, double new_start_time) {
    TimelineClip* src;
    TimelineClip duplicate;

    if (to_track_index < 0) return 0;
    while (tl->track_count <= (u32)to_track_index) {
        timeline_add_track(tl);
    }

    src = timeline_find_clip_by_id(tl, clip_id, NULL, NULL);
    if (!src) return 0;

    copy_timeline_clip(&tl->allocator, &duplicate, src);
    duplicate.id = tl->next_clip_id++;
    duplicate.timeline_start = new_start_time;
    insert_existing_clip(tl, to_track_index, duplicate);
    {
        double end = get_clip_end_time(&duplicate);
        if (end > tl->tracks[to_track_index].max_end_time) {
            tl->tracks[to_track_index].max_end_time = end;
        }
        if (end > tl->duration) {
            tl->duration = end;
        }
    }
    return duplicate.id;
}
