// src/binding/bind_video.c

#include <stdio.h>
#include "vm/vm.h"
#include "vm/object.h"
#include "engine/video.h"
#include "engine/timeline.h"


// === 全局变量：用于在 C 和 脚本之间传递 Timeline ===
// 注意：这是为了热重载机制保留的静态变量。
// 在多 VM 实例场景下，这仍然是不安全的（它应该是 VM 结构体的一部分，或者通过 UserData 传递）。
// 但为了保持本次重构的范围聚焦于 "VM 上下文传递"，我们暂时保留它，
// 只要确保 Native 函数本身不依赖全局 'vm' 变量即可。
static Timeline* g_active_timeline = NULL;

// 供 main.c 调用
Timeline* get_active_timeline() {
    return g_active_timeline;
}

// 供 main.c 调用
void reset_active_timeline() {
    g_active_timeline = NULL;
}

// --- Native Functions ---

// [修改] 增加 VM* vm 参数
// Constructor: Video("path.mp4")
Value nativeCreateClip(VM* vm, int argCount, Value* args) {
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

    // [修改] 传递 vm 给 newClip
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

// [修改] 增加 VM* vm 参数
// Project(width, height, fps)
Value nativeProject(VM* vm, int argCount, Value* args) {
    if (argCount != 3) {
        fprintf(stderr, "Usage: Project(width, height, fps)\n");
        return NIL_VAL;
    }
    double w = AS_NUMBER(args[0]);
    double h = AS_NUMBER(args[1]);
    double fps = AS_NUMBER(args[2]);
   
    // [修改] 传递 vm 给 newTimeline
    return OBJ_VAL(newTimeline(vm, (u32)w, (u32)h, fps));
}

// [修改] 增加 VM* vm 参数
// add(timeline, track_id, clip, start_time)
Value nativeAdd(VM* vm, int argCount, Value* args) {
    if (argCount != 4) return NIL_VAL; // 简化错误处理以聚焦上下文
    
    if (!IS_TIMELINE(args[0]) || !IS_NUMBER(args[1]) || !IS_CLIP(args[2]) || !IS_NUMBER(args[3])) {
        return NIL_VAL;
    }
    ObjTimeline* tlObj = AS_TIMELINE(args[0]);
    int trackIdx = (int)AS_NUMBER(args[1]);
    ObjClip* clip = AS_CLIP(args[2]);
    double start = AS_NUMBER(args[3]);
    
    // 纯 Engine 逻辑，不涉及 VM 内存分配，所以这里不需要使用 vm 参数
    // 但为了保持函数签名一致，参数必须存在
    while (tlObj->timeline->track_count <= trackIdx) {
        timeline_add_track(tlObj->timeline);
    }
    timeline_add_clip(tlObj->timeline, trackIdx, clip, start);
   
    return NIL_VAL;
}

// [修改] 增加 VM* vm 参数
Value nativePreview(VM* vm, int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_TIMELINE(args[0])) {
        g_active_timeline = AS_TIMELINE(args[0])->timeline;
        printf("[Binding] Timeline registered for preview.\n");
    }
    return NIL_VAL;
}

// [修改] 增加 VM* vm 参数
Value nativeTrim(VM* vm, int argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    double start = AS_NUMBER(args[1]);
    double duration = AS_NUMBER(args[2]);
    if (start < 0) start = 0;
    clip->in_point = start;
    clip->duration = duration;
    return NIL_VAL;
}

// [修改] 增加 VM* vm 参数
Value nativeExport(VM* vm, int argCount, Value* args) {
    if (argCount != 2) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    ObjString* filename = AS_STRING(args[1]);
    export_video_clip(clip, filename->chars);
    return NIL_VAL;
}

// [修改] 增加 VM* vm 参数
Value nativeSetScale(VM* vm, int argCount, Value* args) {
    if (argCount < 2) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    double sx = AS_NUMBER(args[1]);
    double sy = sx;
    if (argCount > 2) sy = AS_NUMBER(args[2]);
    clip->default_scale_x = sx;
    clip->default_scale_y = sy;
    return NIL_VAL;
}

// [修改] 增加 VM* vm 参数
Value nativeSetPos(VM* vm, int argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    clip->default_x = AS_NUMBER(args[1]);
    clip->default_y = AS_NUMBER(args[2]);
    return NIL_VAL;
}

// [修改] 增加 VM* vm 参数
Value nativeSetOpacity(VM* vm, int argCount, Value* args) {
    if (argCount != 2) return NIL_VAL;
    ObjClip* clip = AS_CLIP(args[0]);
    double val = AS_NUMBER(args[1]);
    if (val < 0.0) val = 0.0;
    if (val > 1.0) val = 1.0;
    clip->default_opacity = val;
    return NIL_VAL;
}

// === 注册函数 ===
// [修改] 接收 VM* 参数，替换原本的 &vm 全局引用
void registerVideoBindings(VM* vm) {
    // 这里的 defineNative 实现也需要对应更新以接收 function pointer
    // 假设 vm.c 中的 defineNative 已经能够处理新的 NativeFn 签名
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