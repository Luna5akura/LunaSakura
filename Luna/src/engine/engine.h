// src/engine/engine.h

#pragma once
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

typedef struct {
    Project* active_project;
    // 未来可以加更多：AudioSystem* audio; 等等
} EngineContext;

