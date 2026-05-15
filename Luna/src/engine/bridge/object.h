// src/engine/bridge/object.h

#pragma once
#include "core/object.h"
// 引入纯 C 模型定义
#include "engine/model/clip.h"
#include "engine/model/timeline.h"
#include "engine/model/project.h"

typedef struct ObjTimeline ObjTimeline;
typedef struct ObjEffectHandle ObjEffectHandle;
typedef struct ObjAnimatedPropertyHandle ObjAnimatedPropertyHandle;
typedef struct ObjTextAnimatorHandle ObjTextAnimatorHandle;
typedef struct ObjTextRangeSelectorHandle ObjTextRangeSelectorHandle;
typedef struct ObjTextExpressionSelectorHandle ObjTextExpressionSelectorHandle;
typedef struct ObjTextWigglySelectorHandle ObjTextWigglySelectorHandle;
// --- Host Objects Definitions ---
// 1. Clip Object (VM Wrapper)
typedef struct {
    ObjForeign header;
    Clip* clip;
    ObjTimeline* timelineObj;
    Allocator allocator;
} ObjClip;
// 2. Timeline Object
struct ObjTimeline {
    ObjForeign header;
    Timeline* timeline;
};
// 3. Project Object
typedef struct {
    ObjForeign header;
    Project* project;
    ObjTimeline* timelineObj; // [新增] 强引用
} ObjProject;
// 新增: TimelineClip Object (用于时间线实例的关键帧)
typedef struct {
    ObjForeign header;
    u32 clip_id;
    Timeline* timeline;
    ObjTimeline* timelineObj;
    Allocator* allocator;  // 存储分配器，用于动画内存分配
} ObjTimelineClip;
struct ObjEffectHandle {
    ObjForeign header;
    u32 clip_id;
    Timeline* timeline;
    ObjTimeline* timelineObj;
    EffectInstance* effect;
    Allocator* allocator;
    bool owns_effect;
};

typedef enum {
    ANIMATED_PROPERTY_SOURCE_CLIP = 0,
    ANIMATED_PROPERTY_SOURCE_EFFECT = 1,
    ANIMATED_PROPERTY_SOURCE_TEXT_ANIMATOR = 2,
    ANIMATED_PROPERTY_SOURCE_TEXT_RANGE_SELECTOR = 3,
    ANIMATED_PROPERTY_SOURCE_TEXT_EXPRESSION_SELECTOR = 4,
    ANIMATED_PROPERTY_SOURCE_TEXT_WIGGLY_SELECTOR = 5
} AnimatedPropertySourceKind;

typedef enum {
    ANIMATED_PROPERTY_VALUE_NUMBER = 0,
    ANIMATED_PROPERTY_VALUE_COLOR = 1
} AnimatedPropertyValueKind;

struct ObjAnimatedPropertyHandle {
    ObjForeign header;
    Obj* owner_handle;
    AnimatedPropertySourceKind source_kind;
    AnimatedPropertyValueKind value_kind;
    char key[32];
};

struct ObjTextAnimatorHandle {
    ObjForeign header;
    ObjClip* clip_obj;
    u32 animator_index;
};

struct ObjTextRangeSelectorHandle {
    ObjForeign header;
    ObjClip* clip_obj;
    u32 animator_index;
    u32 selector_index;
};

struct ObjTextExpressionSelectorHandle {
    ObjForeign header;
    ObjClip* clip_obj;
    u32 animator_index;
    u32 selector_index;
};

struct ObjTextWigglySelectorHandle {
    ObjForeign header;
    ObjClip* clip_obj;
    u32 animator_index;
    u32 selector_index;
};
// --- Method Tables ---
extern const ForeignClassMethods ClipMethods;
extern const ForeignClassMethods TimelineMethods;
extern const ForeignClassMethods ProjectMethods;
extern const ForeignClassMethods TimelineClipMethods;  // 新增
extern const ForeignClassMethods EffectHandleMethods;
extern const ForeignClassMethods AnimatedPropertyHandleMethods;
extern const ForeignClassMethods TextAnimatorHandleMethods;
extern const ForeignClassMethods TextRangeSelectorHandleMethods;
extern const ForeignClassMethods TextExpressionSelectorHandleMethods;
extern const ForeignClassMethods TextWigglySelectorHandleMethods;
// --- Macros ---
#define IS_CLIP(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ClipMethods)
#define IS_TIMELINE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TimelineMethods)
#define IS_PROJECT(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &ProjectMethods)
#define IS_TIMELINE_CLIP(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TimelineClipMethods)  // 新增
#define IS_EFFECT_HANDLE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &EffectHandleMethods)
#define IS_ANIMATED_PROPERTY_HANDLE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &AnimatedPropertyHandleMethods)
#define IS_TEXT_ANIMATOR_HANDLE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TextAnimatorHandleMethods)
#define IS_TEXT_RANGE_SELECTOR_HANDLE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TextRangeSelectorHandleMethods)
#define IS_TEXT_EXPRESSION_SELECTOR_HANDLE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TextExpressionSelectorHandleMethods)
#define IS_TEXT_WIGGLY_SELECTOR_HANDLE(v) (IS_FOREIGN(v) && AS_FOREIGN(v)->methods == &TextWigglySelectorHandleMethods)
#define AS_CLIP(v) ((ObjClip*)AS_OBJ(v))
#define AS_TIMELINE(v) ((ObjTimeline*)AS_OBJ(v))
#define AS_PROJECT(v) ((ObjProject*)AS_OBJ(v))
#define AS_TIMELINE_CLIP(v) ((ObjTimelineClip*)AS_OBJ(v))  // 新增
#define AS_EFFECT_HANDLE(v) ((ObjEffectHandle*)AS_OBJ(v))
#define AS_ANIMATED_PROPERTY_HANDLE(v) ((ObjAnimatedPropertyHandle*)AS_OBJ(v))
#define AS_TEXT_ANIMATOR_HANDLE(v) ((ObjTextAnimatorHandle*)AS_OBJ(v))
#define AS_TEXT_RANGE_SELECTOR_HANDLE(v) ((ObjTextRangeSelectorHandle*)AS_OBJ(v))
#define AS_TEXT_EXPRESSION_SELECTOR_HANDLE(v) ((ObjTextExpressionSelectorHandle*)AS_OBJ(v))
#define AS_TEXT_WIGGLY_SELECTOR_HANDLE(v) ((ObjTextWigglySelectorHandle*)AS_OBJ(v))
// --- Constructors API ---
ObjClip* newClip(VM* vm, struct sObjString* path);
ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps);
ObjProject* newProject(VM* vm, u32 width, u32 height, double fps);
ObjTimelineClip* newTimelineClip(VM* vm, TimelineClip* tc, ObjTimeline* timeline_obj, Timeline* timeline, Allocator* allocator);  // 新增
ObjEffectHandle* newEffectHandle(VM* vm, TimelineClip* tc, ObjTimeline* timeline_obj, Timeline* timeline, EffectInstance* effect, Allocator* allocator);
ObjEffectHandle* newStandaloneEffectHandle(VM* vm, EffectInstance* effect);
ObjAnimatedPropertyHandle* newAnimatedPropertyHandle(VM* vm, Obj* owner_handle,
                                                     AnimatedPropertySourceKind source_kind,
                                                     AnimatedPropertyValueKind value_kind,
                                                     const char* key);
ObjTextAnimatorHandle* newTextAnimatorHandle(VM* vm, ObjClip* clip_obj, u32 animator_index);
ObjTextRangeSelectorHandle* newTextRangeSelectorHandle(VM* vm, ObjClip* clip_obj, u32 animator_index, u32 selector_index);
ObjTextExpressionSelectorHandle* newTextExpressionSelectorHandle(VM* vm, ObjClip* clip_obj, u32 animator_index, u32 selector_index);
ObjTextWigglySelectorHandle* newTextWigglySelectorHandle(VM* vm, ObjClip* clip_obj, u32 animator_index, u32 selector_index);

// 新增: 暴露 init_allocator（从 bridge.c 移动声明）
void init_allocator(Allocator* allocator, VM* vm);
