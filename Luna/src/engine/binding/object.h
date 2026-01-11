// src/engine/binding/object.h

#pragma once

#include "core/object.h"
// 必须前向声明 Timeline，因为 Timeline 指针被 ObjTimeline 持有
// 但 Timeline 结构体定义在 model/timeline.h 中
typedef struct Timeline Timeline;
typedef struct Project Project;

// --- Host Objects Definitions ---

// 1. Clip Object
typedef struct {
    ObjForeign header; 

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
    Timeline* timeline; // 指向 C 层的 Timeline 结构
} ObjTimeline;

// 3. Project Object
typedef struct {
    ObjForeign header;
    struct Project* project; // 指向 C 层的 Project 结构 (如果 project.h 存在)
} ObjProject;

// --- Method Tables ---
extern const ForeignClassMethods ClipMethods;
extern const ForeignClassMethods TimelineMethods;
extern const ForeignClassMethods ProjectMethods;

// --- Macros ---
#define IS_CLIP(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ClipMethods)
#define IS_TIMELINE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TimelineMethods)
#define IS_PROJECT(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ProjectMethods)

#define AS_CLIP(v) ((ObjClip*)AS_OBJ(v))
#define AS_TIMELINE(v) ((ObjTimeline*)AS_OBJ(v))
#define AS_PROJECT(v) ((ObjProject*)AS_OBJ(v))

// --- Constructors API ---
ObjClip* newClip(VM* vm, struct sObjString* path);
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps);
ObjProject* newProject(VM* vm, u32 width, u32 height, double fps);