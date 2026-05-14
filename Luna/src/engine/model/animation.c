#include "animation.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// 内置预设
const KeyframePreset builtin_presets[] = {
    {"hold", KEYFRAME_HOLD, 0.0},
    {"linear", KEYFRAME_LINEAR, 0.0},
    {"ease_in", KEYFRAME_BEZIER, 0.42},  // 标准 ease-in 权重
    {"ease_out", KEYFRAME_BEZIER, 0.42},  // 标准 ease-out 权重（在添加时区分 in/out）
    {"ease_in_out", KEYFRAME_BEZIER, 0.42}  // 对称权重
};
const int builtin_preset_count = sizeof(builtin_presets) / sizeof(KeyframePreset);

// 用户自定义预设
KeyframePreset* user_presets = NULL;
int user_preset_count = 0;
int user_preset_capacity = 0;

void init_animation(Animation* anim, Allocator* allocator, double default_value) {
    memset(anim, 0, sizeof(Animation));
    anim->default_value = default_value;
    anim->capacity = 4;
    anim->keyframes = (Keyframe*)allocator->fn(allocator->ctx, NULL, 0, sizeof(Keyframe) * anim->capacity);
}

void free_animation(Animation* anim, Allocator* allocator) {
    if (anim->keyframes) {
        allocator->fn(allocator->ctx, anim->keyframes, sizeof(Keyframe) * anim->capacity, 0);
    }
    memset(anim, 0, sizeof(Animation));
}

static int find_insert_index(const Animation* anim, double time) {
    int left = 0, right = anim->count - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (time < anim->keyframes[mid].time) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    return left;
}

static int find_exact_index(const Animation* anim, double time) {
    int left = 0;
    int right = (int)anim->count - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        double mid_time = anim->keyframes[mid].time;
        if (time < mid_time) {
            right = mid - 1;
        } else if (time > mid_time) {
            left = mid + 1;
        } else {
            return mid;
        }
    }
    return -1;
}

static void ensure_capacity(Animation* anim, Allocator* allocator) {
    if (anim->count < anim->capacity) return;
    uint32_t new_cap = anim->capacity * 2;
    anim->keyframes = (Keyframe*)allocator->fn(
        allocator->ctx,
        anim->keyframes,
        sizeof(Keyframe) * anim->capacity,
        sizeof(Keyframe) * new_cap
    );
    anim->capacity = new_cap;
}

void add_keyframe(Animation* anim, Allocator* allocator, double time, double value, KeyframeType type, double weight) {
    ensure_capacity(anim, allocator);
    int insert_idx = find_insert_index(anim, time);
    if (insert_idx < (int)anim->count) {
        memmove(&anim->keyframes[insert_idx + 1], &anim->keyframes[insert_idx], (anim->count - insert_idx) * sizeof(Keyframe));
    }
    anim->keyframes[insert_idx].time = time;
    anim->keyframes[insert_idx].value = value;
    anim->keyframes[insert_idx].type = type;
    anim->keyframes[insert_idx].bezier_weight = weight;
    anim->count++;
}

void set_keyframe(Animation* anim, Allocator* allocator, double time, double value, KeyframeType type, double weight) {
    int exact_idx = find_exact_index(anim, time);
    if (exact_idx >= 0) {
        anim->keyframes[exact_idx].value = value;
        anim->keyframes[exact_idx].type = type;
        anim->keyframes[exact_idx].bezier_weight = weight;
        return;
    }
    add_keyframe(anim, allocator, time, value, type, weight);
}

bool remove_keyframe(Animation* anim, double time) {
    int idx = find_exact_index(anim, time);
    if (idx < 0) return false;
    if (idx < (int)anim->count - 1) {
        memmove(
            &anim->keyframes[idx],
            &anim->keyframes[idx + 1],
            (anim->count - (uint32_t)idx - 1) * sizeof(Keyframe)
        );
    }
    anim->count--;
    return true;
}

void clear_keyframes(Animation* anim) {
    anim->count = 0;
}

uint32_t get_keyframe_count(const Animation* anim) {
    return anim->count;
}

const Keyframe* get_keyframe_at(const Animation* anim, uint32_t index) {
    if (index >= anim->count) return NULL;
    return &anim->keyframes[index];
}

const Keyframe* find_keyframe(const Animation* anim, double time) {
    int idx = find_exact_index(anim, time);
    if (idx < 0) return NULL;
    return &anim->keyframes[idx];
}

void shift_keyframe_times(Animation* anim, double delta) {
    for (uint32_t i = 0; i < anim->count; i++) {
        anim->keyframes[i].time += delta;
    }
}

void scale_keyframe_times(Animation* anim, double factor) {
    for (uint32_t i = 0; i < anim->count; i++) {
        anim->keyframes[i].time *= factor;
    }
}

void copy_keyframes(Animation* dst, Allocator* allocator, const Animation* src) {
    if (dst->keyframes) {
        allocator->fn(allocator->ctx, dst->keyframes, sizeof(Keyframe) * dst->capacity, 0);
        dst->keyframes = NULL;
    }

    dst->default_value = src->default_value;
    dst->count = src->count;
    dst->capacity = src->count > 4 ? src->count : 4;
    dst->keyframes = (Keyframe*)allocator->fn(
        allocator->ctx,
        NULL,
        0,
        sizeof(Keyframe) * dst->capacity
    );

    if (src->count > 0) {
        memcpy(dst->keyframes, src->keyframes, sizeof(Keyframe) * src->count);
    }
}

static const KeyframePreset* find_preset(const char* name) {
    for (int i = 0; i < builtin_preset_count; i++) {
        if (strcmp(builtin_presets[i].name, name) == 0) return &builtin_presets[i];
    }
    for (int i = 0; i < user_preset_count; i++) {
        if (strcmp(user_presets[i].name, name) == 0) return &user_presets[i];
    }
    return NULL;
}

void add_keyframe_with_preset(Animation* anim, Allocator* allocator, double time, double value, const char* preset_name) {
    const KeyframePreset* preset = find_preset(preset_name);
    if (preset) {
        add_keyframe(anim, allocator, time, value, preset->type, preset->weight);
    } else {
        // 默认线性
        add_keyframe(anim, allocator, time, value, KEYFRAME_LINEAR, 0.0);
    }
}

void add_user_preset(Allocator* allocator, const char* name, KeyframeType type, double weight) {
    (void)allocator;
    if (user_preset_count >= user_preset_capacity) {
        int new_cap = user_preset_capacity ? user_preset_capacity * 2 : 4;
        user_presets = (KeyframePreset*)realloc(user_presets, sizeof(KeyframePreset) * new_cap);
        user_preset_capacity = new_cap;
    }
    user_presets[user_preset_count].name = strdup(name);  // 注意：需管理内存，实际生产需释放
    user_presets[user_preset_count].type = type;
    user_presets[user_preset_count].weight = weight;
    user_preset_count++;
}

void reset_user_presets(void) {
    for (int i = 0; i < user_preset_count; i++) {
        free((void*)user_presets[i].name);
    }
    free(user_presets);
    user_presets = NULL;
    user_preset_count = 0;
    user_preset_capacity = 0;
}

static double cubic_bezier_y(double t, double p1x, double p1y, double p2x, double p2y) {
    double u = 1 - t;
    double tt = t * t;
    double uu = u * u;
    double uuu = uu * u;
    double ttt = tt * t;
    double y = 3 * uu * t * p1y + 3 * u * tt * p2y + ttt;
    return y;
}

static double get_bezier_progress(double t_time, double p1x, double p1y, double p2x, double p2y) {
    double t = t_time;
    for (int i = 0; i < 8; i++) {
        double u = 1 - t;
        double tt = t * t;
        double uu = u * u;
        double uuu = uu * u;
        double ttt = tt * t;
        double x = 3 * uu * t * p1x + 3 * u * tt * p2x + ttt;
        double dx = 3 * uu * p1x + 6 * u * t * (p2x - p1x) + 3 * tt * (1 - p2x);
        if (fabs(dx) < 1e-6) break;
        double diff = x - t_time;
        if (fabs(diff) < 1e-6) break;
        t -= diff / dx;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
    }
    return cubic_bezier_y(t, p1x, p1y, p2x, p2y);
}

double evaluate_animation(const Animation* anim, double time) {
    if (anim->count == 0) return anim->default_value;
    if (time <= anim->keyframes[0].time) return anim->keyframes[0].value;
    if (time >= anim->keyframes[anim->count - 1].time) return anim->keyframes[anim->count - 1].value;
    int left = 0, right = anim->count - 1;
    while (left < right - 1) {
        int mid = left + (right - left) / 2;
        if (time < anim->keyframes[mid].time) right = mid;
        else left = mid;
    }
    const Keyframe* kf_left = &anim->keyframes[left];
    const Keyframe* kf_right = &anim->keyframes[left + 1];
    double t = (time - kf_left->time) / (kf_right->time - kf_left->time);
    double delta = kf_right->value - kf_left->value;
    switch (kf_left->type) {
        case KEYFRAME_HOLD: return kf_left->value;
        case KEYFRAME_LINEAR: return kf_left->value + t * delta;
        case KEYFRAME_BEZIER: {
            double p1x = kf_left->bezier_weight;
            double p2x = 1.0 - kf_right->bezier_weight;
            double frac = get_bezier_progress(t, p1x, 0.0, p2x, 1.0);
            return kf_left->value + frac * delta;
        }
    }
    return kf_left->value;
}
