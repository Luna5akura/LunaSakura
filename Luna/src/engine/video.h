// src/engine/video.h

#ifndef LUNA_ENGINE_VIDEO_H
#define LUNA_ENGINE_VIDEO_H
#include <stdbool.h>
#include "common.h" // for u32
#include "vm/object.h"

// --- Video Metadata ---
// Layout optimized for alignment:
// [Duration(8)] [FPS(8)] [W(4)|H(4)] [Success(1)|Pad(7)]
// Total size: 32 bytes
typedef struct {
    double duration;
    double fps;
    u32 width;
    u32 height;
    bool success;
} VideoMeta;

// --- Engine Interface ---
// Synchronously probes video file headers.
VideoMeta load_video_metadata(const char* filepath);
// Previews the clip in a window (Blocking/UI thread).
void play_video_clip(ObjClip* clip);
void play_timeline(struct Timeline* tl);
// Renders the clip to a file (Blocking/Worker thread).
void export_video_clip(ObjClip* clip, const char* output_filename);

#endif