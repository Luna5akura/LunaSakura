// src/engine/object.h

#pragma once

#include "core/object.h"

typedef struct Timeline Timeline;

// --- Raw Data Structures ---
// (原 sObjProject 里的纯数据部分，建议放在这里或专门的 project.h)
typedef struct {
    u32 width;
    u32 height;
    double fps;
    Timeline* timeline; // 指向 C 层的 Timeline 结构
    
    // 预览范围控制
    bool use_preview_range;
    double preview_start;
    double preview_end;
} Project;

// --- Host Objects Definitions ---

// 1. Clip Object
// 继承自 ObjForeign，内存布局为: [ObjForeign header] [Fields...]
typedef struct {
    ObjForeign header; 

    // 原 sObjClip 的字段
    struct sObjString* path;
    double duration;
    double start_time;
    double in_point;
    double out_point;
    double fps;

    bool has_video;
    bool has_audio;
    i32 audio_channels;
    i32 audio_sample_rate;

    // 变换属性默认值
    double default_scale_x;
    double default_scale_y;
    double default_x;
    double default_y;
    double default_opacity;

    double volume; 
    u32 width;
    u32 height;
    i32 layer;
} ObjClip;

// 2. Timeline Object
typedef struct {
    ObjForeign header;
    Timeline* timeline; // 持有 engine/timeline.h 中的 Timeline 指针
} ObjTimeline;

// 3. Project Object
typedef struct {
    ObjForeign header;
    Project* project;   // 持有上方的 Project 指针
} ObjProject;

// --- Method Tables (用于类型识别) ---
// 这些变量在 engine/object.c 中定义，这里声明以便宏使用
extern const ForeignClassMethods ClipMethods;
extern const ForeignClassMethods TimelineMethods;
extern const ForeignClassMethods ProjectMethods;

// --- Macros (类型检查与转换) ---

// 检查是否为 Clip: 先检查是否为 Foreign，再检查方法表地址是否匹配
#define IS_CLIP(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ClipMethods)
#define IS_TIMELINE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TimelineMethods)
#define IS_PROJECT(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ProjectMethods)

// 转换宏
#define AS_CLIP(v) ((ObjClip*)AS_OBJ(v))
#define AS_TIMELINE(v) ((ObjTimeline*)AS_OBJ(v))
#define AS_PROJECT(v) ((ObjProject*)AS_OBJ(v))

// --- Constructors API ---
// 这些函数将替代原本在 core/object.h 中的声明
ObjClip* newClip(VM* vm, struct sObjString* path);
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps);
ObjProject* newProject(VM* vm, u32 width, u32 height, double fps);