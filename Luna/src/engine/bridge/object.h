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