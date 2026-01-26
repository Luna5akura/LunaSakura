// src/engine/bridge/object.h

#pragma once
#include "core/object.h"
// 引入纯 C 模型定义
#include "engine/model/clip.h"
#include "engine/model/timeline.h"
#include "engine/model/project.h"
// --- Host Objects Definitions ---
// 1. Clip Object (VM Wrapper)
typedef struct {
    ObjForeign header;
    Clip* clip;
} ObjClip;
// 2. Timeline Object
typedef struct {
    ObjForeign header;
    Timeline* timeline;
} ObjTimeline;
// 3. Project Object
typedef struct {
    ObjForeign header;
    Project* project;
    struct ObjTimeline* timelineObj; // [新增] 强引用
} ObjProject;
// 新增: TimelineClip Object (用于时间线实例的关键帧)
typedef struct {
    ObjForeign header;
    TimelineClip* clip;
    Allocator* allocator;  // 存储分配器，用于动画内存分配
} ObjTimelineClip;
// --- Method Tables ---
extern const ForeignClassMethods ClipMethods;
extern const ForeignClassMethods TimelineMethods;
extern const ForeignClassMethods ProjectMethods;
extern const ForeignClassMethods TimelineClipMethods;  // 新增
// --- Macros ---
#define IS_CLIP(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ClipMethods)
#define IS_TIMELINE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TimelineMethods)
#define IS_PROJECT(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ProjectMethods)
#define IS_TIMELINE_CLIP(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TimelineClipMethods)  // 新增
#define AS_CLIP(v) ((ObjClip*)AS_OBJ(v))
#define AS_TIMELINE(v) ((ObjTimeline*)AS_OBJ(v))
#define AS_PROJECT(v) ((ObjProject*)AS_OBJ(v))
#define AS_TIMELINE_CLIP(v) ((ObjTimelineClip*)AS_OBJ(v))  // 新增
// --- Constructors API ---
ObjClip* newClip(VM* vm, struct sObjString* path);
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps);
ObjProject* newProject(VM* vm, u32 width, u32 height, double fps);
ObjTimelineClip* newTimelineClip(VM* vm, TimelineClip* tc, Allocator* allocator);  // 新增

// 新增: 暴露 init_allocator（从 bridge.c 移动声明）
void init_allocator(Allocator* allocator, VM* vm);