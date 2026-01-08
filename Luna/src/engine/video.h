// src/engine/video.h

#pragma once
#include "core/object.h"
// Forward declare VM
typedef struct VM VM;
// --- Video Metadata ---
typedef struct {
    double duration;
    double fps;
    u32 width;
    u32 height;
    bool success;
} VideoMeta;
// --- Engine Interface ---
VideoMeta load_video_metadata(const char* filepath);
// Previews the clip in a separate window (Blocking).
void play_video_clip(VM* vm, ObjClip* clip);
// [已删除] play_timeline: Timeline preview is now handled by the host (main.c)
// Renders the clip to a file.
void export_video_clip(VM* vm, ObjClip* clip, const char* output_filename);
/* Thread-safe */ 