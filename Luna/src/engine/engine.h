// src/engine/engine.h

#pragma once
#ifndef ENGINE_H
#define ENGINE_H
// === Core Types & Bindings ===
#include "engine/bridge/object.h"
// === Data Models ===
#include "engine/model/timeline.h"
#include "engine/model/transform.h"
#include "engine/model/project.h"
// === Rendering ===
#include "engine/render/compositor.h"
// === Media & Utils ===
#include "engine/media/utils/probe.h"
#include "engine/media/audio/mixer.h"
// === High Level Services ===
#include "engine/service/exporter.h"
#include "engine/service/preview.h"
#include "engine/service/transcoder.h"

#include "effect/registry.h"   // 新增

enum {
    ENGINE_BINDING_PROP_DEFAULT_SCALE_X = 0,
    ENGINE_BINDING_PROP_DEFAULT_SCALE_Y,
    ENGINE_BINDING_PROP_DEFAULT_X,
    ENGINE_BINDING_PROP_DEFAULT_Y,
    ENGINE_BINDING_PROP_DEFAULT_ROTATION,
    ENGINE_BINDING_PROP_DEFAULT_OPACITY,
    ENGINE_BINDING_PROP_VOLUME,
    ENGINE_BINDING_PROP_IN_POINT,
    ENGINE_BINDING_PROP_DURATION,
    ENGINE_BINDING_PROP_WIDTH,
    ENGINE_BINDING_PROP_HEIGHT,
    ENGINE_BINDING_PROP_FPS,
    ENGINE_BINDING_PROP_HAS_VIDEO,
    ENGINE_BINDING_PROP_HAS_AUDIO,
    ENGINE_BINDING_PROP_AFFECTS_WHOLE_FRAME,
    ENGINE_BINDING_PROP_FEATHER,
    ENGINE_BINDING_PROP_MASK_INVERT,
    ENGINE_BINDING_PROP_LETTER_SPACING,
    ENGINE_BINDING_PROP_STROKE_ENABLED,
    ENGINE_BINDING_PROP_STROKE_WIDTH,
    ENGINE_BINDING_PROP_CACHE_COUNT
};
typedef struct {
    Project* active_project;
    ObjProject* active_project_obj;
    ObjString* handle_key;
    ObjClass* clip_instance_class;
    ObjClass* timeline_class;
    ObjClass* effect_class;
    ObjString* prop_cache[ENGINE_BINDING_PROP_CACHE_COUNT];
} EngineContext;
#endif // ENGINE_H
