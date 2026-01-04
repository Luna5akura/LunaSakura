// src/engine/timeline.h
#pragma once // 1. 编译优化
#include "common.h"
#include "vm/object.h"

// === 基础组件 ===
// 变换属性 (Transform)
// 2. 优化：16字节对齐，适配 SIMD 指令 (SSE/NEON)
// 大小调整为 32 字节
typedef struct __attribute__((aligned(16))) {
    float x, y; // 0-8
    float scale_x; // 8-12
    float scale_y; // 12-16 (128-bit boundary)
   
    float rotation; // 16-20
    float opacity; // 20-24
    i32 z_index; // 24-28
    u32 _padding; // 28-32 (Padding for alignment)
} Transform;

// 时间轴片段 (Instance)
// 3. 优化：移除 prev/next 指针，改为纯数据结构
// 该结构体现在可以被紧凑地存储在数组中
typedef struct {
    // --- Data Source ---
    ObjClip* media; // 8 bytes
   
    // --- Time Mapping ---
    // Double precision is good, but consider keeping them together
    double timeline_start; // 8 bytes
    double timeline_duration; // 8 bytes
    double source_in; // 8 bytes
   
    // --- Spatial Props ---
    Transform transform; // 32 bytes (Aligned)
    // --- Runtime Flags ---
    // u8 is_selected;
    // u8 is_muted;
} TimelineClip;

// === 容器结构 ===
// 轨道 (Track)
// 4. 优化：从链表改为动态数组 (Vector 模式)
// 渲染时这是热数据 (Hot Data)
typedef struct {
    int id;
   
    // 5. 优化：使用位掩码代替多个 bool
    // bit 0: visible, bit 1: locked, bit 2: solo
    u8 flags;
   
    char name[31]; // 调整大小使得 Track 头部对齐 (4+1+31 = 36 bytes)
   
    // 核心优化：连续内存数组
    // Clips 必须始终保持按 timeline_start 升序排序
    TimelineClip* clips;
    int clip_count;
    int clip_capacity;
    // 6. 优化：渲染游标缓存
    // 视频播放通常是线性的，记录上次访问的索引可将查询降为 O(1)
    int last_lookup_index;
} Track;

// 时间轴工程 (Project Root)
typedef struct Timeline {
    // 全局设置
    u32 width;
    u32 height;
    double fps;
    double duration;
   
    struct {
        u8 r, g, b, a;
    } background_color;
    // 7. 优化：使用扁平数组而非指针数组
    // 减少内存碎片和间接跳转
    Track* tracks;
    int track_count;
    int track_capacity;
} Timeline;

// === API 声明 ===
Timeline* timeline_create(u32 width, u32 height, double fps);
void timeline_free(Timeline* tl);
// Track
int timeline_add_track(Timeline* tl);
void timeline_remove_track(Timeline* tl, int track_index);
Track* timeline_get_track(Timeline* tl, int track_index);
// Clip 管理
// 注意：由于改为数组存储，add_clip 可能会触发 realloc，导致之前的 TimelineClip* 指针失效。
// 因此，API 最好返回索引 (int) 而非指针，或者明确文档说明指针不持久。
int timeline_add_clip(Timeline* tl, int track_index, ObjClip* media, double start_time);
void timeline_remove_clip(Timeline* tl, int track_index, int clip_index);
// 查询优化
// 内部使用二分查找 (Binary Search) + 游标缓存
TimelineClip* timeline_get_clip_at(Track* track, double time);
// 渲染遍历辅助
// 获取某个时间范围内所有的 Clips (用于渲染优化，只处理视口内的)
void timeline_get_visible_clips(Timeline* tl, double time, TimelineClip*** out_clips, int* out_count);
void timeline_update_duration(Timeline* tl);