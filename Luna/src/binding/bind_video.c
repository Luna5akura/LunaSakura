// src/binding/bind_video.c

#include <stdio.h>
#include "vm/vm.h"
#include "vm/object.h"
#include "engine/video.h"
#include "engine/timeline.h"

// 声明全局 vm（假设在 vm.c 中定义）
extern VM vm;

// === 全局变量：用于在 C 和 脚本之间传递 Timeline ===
// 注意：实际工程中最好用 Context 结构体传递，这里为了简单使用静态全局变量
static Timeline* g_active_timeline = NULL;

// 供 main.c 调用，获取脚本生成的 Timeline
Timeline* get_active_timeline() {
    return g_active_timeline;
}

// 供 main.c 调用，在重载前重置
void reset_active_timeline() {
    g_active_timeline = NULL;
}

// --- Native Functions ---
// Constructor: Video("path.mp4")
Value nativeCreateClip(int argCount, Value* args) {
    // Optimization: Tag error checks as unlikely branch
    if (UNLIKELY(argCount != 1 || !IS_STRING(args[0]))) {
        fprintf(stderr, "Usage: Video(path: String)\n");
        return NIL_VAL;
    }
    ObjString* path = AS_STRING(args[0]);
   
    // IO Blocking Call: Probes video header
    VideoMeta meta = load_video_metadata(path->chars);
   
    if (UNLIKELY(!meta.success)) {
        fprintf(stderr, "Runtime Error: Could not load video metadata from '%s'\n", path->chars);
        return NIL_VAL;
    }
    // Allocation (Safe: args[0] roots 'path' on stack if GC triggers here)
    ObjClip* clip = newClip(path);
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
Value nativeProject(int argCount, Value* args) {
    if (argCount != 3) {
        fprintf(stderr, "Usage: Project(width, height, fps)\n");
        return NIL_VAL;
    }
    double w = AS_NUMBER(args[0]);
    double h = AS_NUMBER(args[1]);
    double fps = AS_NUMBER(args[2]);
   
    return OBJ_VAL(newTimeline((u32)w, (u32)h, fps));
}

// add(timeline, track_id, clip, start_time)
Value nativeAdd(int argCount, Value* args) {
    if (argCount != 4) {
        fprintf(stderr, "Usage: add(timeline, track, clip, time)\n");
        return NIL_VAL;
    }
    // 类型检查 (省略详细报错以节省篇幅)
    if (!IS_TIMELINE(args[0]) || !IS_NUMBER(args[1]) || !IS_CLIP(args[2]) || !IS_NUMBER(args[3])) {
        return NIL_VAL;
    }
    ObjTimeline* tlObj = AS_TIMELINE(args[0]);
    int trackIdx = (int)AS_NUMBER(args[1]);
    ObjClip* clip = AS_CLIP(args[2]);
    double start = AS_NUMBER(args[3]);
    // 确保轨道存在 (简单的自动扩容逻辑，或者由用户保证)
    while (tlObj->timeline->track_count <= trackIdx) {
        timeline_add_track(tlObj->timeline);
    }
    timeline_add_clip(tlObj->timeline, trackIdx, clip, start);
   
    return NIL_VAL;
}

// [修改] nativePreview
// 不再阻塞播放，只是注册当前要预览的时间轴
Value nativePreview(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;
    if (IS_TIMELINE(args[0])) {
        // 获取 Timeline 指针存入全局变量
        g_active_timeline = AS_TIMELINE(args[0])->timeline;
        printf("[Binding] Timeline registered for preview.\n");
    } else if (IS_CLIP(args[0])) {
        // 如果只是预览单个 Clip，为了统一逻辑，我们可以临时创建一个 Timeline 包裹它
        // 但为了简单，暂时只支持 preview(Project) 进行热重载
        printf("[Warning] Hot-reload currently only supports preview(Project).\n");
    }
    return NIL_VAL;
}

// trim(clip, start, duration)
Value nativeTrim(int argCount, Value* args) {
    if (UNLIKELY(argCount != 3)) {
        fprintf(stderr, "Usage: trim(clip, start, duration)\n");
        return NIL_VAL;
    }
   
    // Fast type check macros
    if (UNLIKELY(!IS_CLIP(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2]))) {
        fprintf(stderr, "TypeError: trim() requires (Clip, Number, Number).\n");
        return NIL_VAL;
    }
    ObjClip* clip = AS_CLIP(args[0]);
    double start = AS_NUMBER(args[1]);
    double duration = AS_NUMBER(args[2]);
    // Logic: Clamp start to 0, direct property modification
    if (start < 0) start = 0;
   
    clip->in_point = start;
    clip->duration = duration;
    return NIL_VAL;
}

// export(clip, "out.mp4")
Value nativeExport(int argCount, Value* args) {
    if (UNLIKELY(argCount != 2)) {
        fprintf(stderr, "Usage: export(clip, filename)\n");
        return NIL_VAL;
    }
   
    if (UNLIKELY(!IS_CLIP(args[0]) || !IS_STRING(args[1]))) {
        fprintf(stderr, "TypeError: export() requires (Clip, String).\n");
        return NIL_VAL;
    }
    ObjClip* clip = AS_CLIP(args[0]);
    ObjString* filename = AS_STRING(args[1]);
    // Heavy IO/Computing operation (Blocking)
    export_video_clip(clip, filename->chars);
   
    return NIL_VAL;
}

Value nativeSetScale(int argCount, Value* args) {
    if (argCount < 2 || !IS_CLIP(args[0]) || !IS_NUMBER(args[1])) {
        // 出错时不崩溃，静默失败或打印错误均可
        return NIL_VAL;
    }
   
    ObjClip* clip = AS_CLIP(args[0]);
    double sx = AS_NUMBER(args[1]);
    double sy = sx; // 如果只传一个参数，默认等比缩放
   
    if (argCount > 2 && IS_NUMBER(args[2])) {
        sy = AS_NUMBER(args[2]);
    }
    clip->default_scale_x = sx;
    clip->default_scale_y = sy;
    return NIL_VAL;
}

// setPos(clip, x, y)
Value nativeSetPos(int argCount, Value* args) {
    if (argCount != 3 || !IS_CLIP(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        return NIL_VAL;
    }
    ObjClip* clip = AS_CLIP(args[0]);
    clip->default_x = AS_NUMBER(args[1]);
    clip->default_y = AS_NUMBER(args[2]);
    return NIL_VAL;
}

// [新增] setOpacity(clip, 0.5)
Value nativeSetOpacity(int argCount, Value* args) {
    if (argCount != 2 || !IS_CLIP(args[0]) || !IS_NUMBER(args[1])) {
        return NIL_VAL;
    }
    ObjClip* clip = AS_CLIP(args[0]);
    double val = AS_NUMBER(args[1]);
    
    // 限制范围在 0.0 到 1.0 之间
    if (val < 0.0) val = 0.0;
    if (val > 1.0) val = 1.0;
    
    clip->default_opacity = val;
    return NIL_VAL;
}

void registerVideoBindings() {
    defineNative(&vm, "Video", nativeCreateClip);  // 传入 &vm
    defineNative(&vm, "Project", nativeProject);
    defineNative(&vm, "add", nativeAdd);
    defineNative(&vm, "preview", nativePreview);
    defineNative(&vm, "trim", nativeTrim);
    defineNative(&vm, "export", nativeExport);
   
    // === [新增] ===
    defineNative(&vm, "setScale", nativeSetScale);
    defineNative(&vm, "setPos", nativeSetPos);
    defineNative(&vm, "setOpacity", nativeSetOpacity);
}