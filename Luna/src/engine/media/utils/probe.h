// src/engine/media/utils/probe.h

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Forward decl
struct VM;

typedef struct {
    double duration;
    double fps;
    uint32_t width;
    uint32_t height;
    bool success;
} VideoMeta;

VideoMeta load_video_metadata(struct VM* vm, const char* filepath);