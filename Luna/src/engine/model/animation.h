#pragma once
#include "common.h"
#include "allocator.h"

typedef enum {
    KEYFRAME_HOLD,
    KEYFRAME_LINEAR,
    KEYFRAME_BEZIER
} KeyframeType;

typedef struct {
    double time;
    double value;
    KeyframeType type;  // 出插值类型（从此关键帧到下一个）
    double bezier_weight;  // 贝塞尔权重（0-1，影响曲线强度）
} Keyframe;

typedef struct {
    Keyframe* keyframes;
    uint32_t count;
    uint32_t capacity;
    double default_value;
} Animation;

// 预设结构
typedef struct {
    const char* name;
    KeyframeType type;
    double weight;
} KeyframePreset;

// 内置预设
extern const KeyframePreset builtin_presets[];
extern const int builtin_preset_count;

// 用户自定义预设（可通过脚本添加，初始为空）
extern KeyframePreset* user_presets;
extern int user_preset_count;
extern int user_preset_capacity;

// 初始化动画
void init_animation(Animation* anim, Allocator* allocator, double default_value);

// 释放动画
void free_animation(Animation* anim, Allocator* allocator);

// 添加关键帧（按时间排序插入）
void add_keyframe(Animation* anim, Allocator* allocator, double time, double value, KeyframeType type, double weight);

// 使用预设添加关键帧
void add_keyframe_with_preset(Animation* anim, Allocator* allocator, double time, double value, const char* preset_name);

// 评估属性值
double evaluate_animation(const Animation* anim, double time);

// 添加用户自定义预设
void add_user_preset(Allocator* allocator, const char* name, KeyframeType type, double weight);