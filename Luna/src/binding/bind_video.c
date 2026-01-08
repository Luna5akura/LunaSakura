// src/binding/bind_video.c

#include <stdio.h>
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/video.h"
#include "engine/timeline.h"
// 供 main.c 调用
Timeline* get_active_timeline(VM* vm) {
    return vm->active_timeline;
}
// 供 main.c 调用
void reset_active_timeline(VM* vm) {
    vm->active_timeline = NULL;
}
// --- Native Functions ---
// Constructor: Video("path.mp4")
Value nativeCreateClip(VM* vm, i32 argCount, Value* args) {
    if (UNLIKELY(argCount != 1 || !IS_STRING(args[0]))) {
        fprintf(stderr, "Usage: Video(path: String)\n");
        return NIL_VAL;
    }
    ObjString* path = AS_STRING(args[0]);
    // IO Blocking Call
    VideoMeta meta = load_video_metadata(path->chars);
    if (UNLIKELY(!meta.success)) {
        fprintf(stderr, "Runtime Error: Could not load video metadata from '%s'\n", path->chars);
        return NIL_VAL;
    }
    ObjClip* clip = newClip(vm, path);
 
    clip->duration = meta.duration;
    clip->width = meta.width;
    clip->height = meta.height;
    clip->fps = meta.fps;
#ifdef DEBUG_TRACE_EXECUTION
    printf("[Native] Video Loaded: %s (%.2fs, %ux%u, %.2f fps)\n",
           path->chars, clip->duration, clip->width, clip->height, clip->fps);
#endif
    return OBJ_VAL(clip);
}
// Project(width, height, fps)
Value nativeProject(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3) {
        fprintf(stderr, "Usage: Project(width, height, fps)\n");
        return NIL_VAL;
    }
    double w = AS_NUMBER(args[0]);
    double h = AS_NUMBER(args[1]);
    double fps = AS_NUMBER(args[2]);
    return OBJ_VAL(newTimeline(vm, (u32)w, (u32)h, fps));
}
// add(timeline, track_id, clip, start_time)
Value nativeAdd(VM* vm, i32 argCount, Value* args) {
    if (argCount != 4) return NIL_VAL; // 简化错误处理以聚焦上下文
 
    if (!IS_TIMELINE(args[0]) || !IS_NUMBER(args[1]) || !IS_CLIP(args[2]) || !IS_NUMBER(args[3])) {
        return NIL_VAL;
    }
    ObjTimeline* tlObj = AS_TIMELINE(args[0]);
    i32 trackIdx = (i32)AS_NUMBER(args[1]);
    ObjClip* clip = AS_CLIP(args[2]);
    double start = AS_NUMBER(args[3]);
 
    while (tlObj->timeline->track_count <= (u32)trackIdx) {
        timeline_add_track(vm, tlObj->timeline);
    }
    timeline_add_clip(vm, tlObj->timeline, trackIdx, clip, start);
    return NIL_VAL;
}
// preview(timeline)
Value nativePreview(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_TIMELINE(args[0])) {
        vm->active_timeline = AS_TIMELINE(args[0])->timeline;
        printf("[Binding] Timeline registered for preview.\n");
    }
    return NIL_VAL;
}
Value nativeTrim(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    double start = AS_NUMBER(args[1]);
    double duration = AS_NUMBER(args[2]);
    if (start < 0) start = 0;
    clip->in_point = start;
    clip->duration = duration;
    return NIL_VAL;
}
Value nativeExport(VM* vm, i32 argCount, Value* args) {
    if (argCount != 2) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    ObjString* filename = AS_STRING(args[1]);
    export_video_clip(vm, clip, filename->chars);
    return NIL_VAL;
}
Value nativeSetScale(VM* vm, i32 argCount, Value* args) {
    if (argCount < 2) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    double sx = AS_NUMBER(args[1]);
    double sy = sx;
    if (argCount > 2) sy = AS_NUMBER(args[2]);
    clip->default_scale_x = sx;
    clip->default_scale_y = sy;
    return NIL_VAL;
}
Value nativeSetPos(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    clip->default_x = AS_NUMBER(args[1]);
    clip->default_y = AS_NUMBER(args[2]);
    return NIL_VAL;
}
Value nativeSetOpacity(VM* vm, i32 argCount, Value* args) {
    if (argCount != 2) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    double val = AS_NUMBER(args[1]);
    if (val < 0.0) val = 0.0;
    if (val > 1.0) val = 1.0;
    clip->default_opacity = val;
    return NIL_VAL;
}
// === 注册函数 ===
void registerVideoBindings(VM* vm) {
    defineNative(vm, "Video", nativeCreateClip);
    defineNative(vm, "Project", nativeProject);
    defineNative(vm, "add", nativeAdd);
    defineNative(vm, "preview", nativePreview);
    defineNative(vm, "trim", nativeTrim);
    defineNative(vm, "export", nativeExport);
    defineNative(vm, "setScale", nativeSetScale);
    defineNative(vm, "setPos", nativeSetPos);
    defineNative(vm, "setOpacity", nativeSetOpacity);
}
