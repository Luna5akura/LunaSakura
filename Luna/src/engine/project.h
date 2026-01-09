#pragma once
#include "engine/timeline.h"  // 依赖Timeline

typedef struct {
    u32 width;
    u32 height;
    double fps;
    Timeline* timeline;  // 持有的Timeline
} Project;