// src/binding/bind_video.c

#include <stdio.h>
#include <string.h>
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/video.h"
#include "engine/timeline.h"

// --- 宏定义：简化操作 ---

// 获取当前实例 (this)，在 Native Init 中，this 位于 args[-1]
#define GET_SELF (AS_INSTANCE(args[-1]))

// 安全地将 C double 属性同步到 Luna 实例字段中
// copyString 会处理字符串驻留，确保 GC 安全
#define SET_PROP(obj, key, val) \
    do { \
        ObjString* k = copyString(vm, key, strlen(key)); \
        push(vm, OBJ_VAL(k)); \
        tableSet(vm, &obj->fields, OBJ_VAL(k), NUMBER_VAL(val)); \
        pop(vm); \
    } while(0)

// 供 main.c 调用
Project* get_active_project(VM* vm) {
    return vm->active_project;
}
void reset_active_project(VM* vm) {
    vm->active_project = NULL;
}

// --- 内部辅助函数 ---

// [修复] 移除 static ObjString* str_handle，防止 GC 悬空指针

static Obj* getHandle(VM* vm, Value instanceVal, ObjType expectedType) {
    if (!IS_INSTANCE(instanceVal)) return NULL;
    ObjInstance* instance = AS_INSTANCE(instanceVal);

    // [修复] 每次调用时获取字符串，确保对象有效
    ObjString* handleKey = copyString(vm, "_handle", 7);
    push(vm, OBJ_VAL(handleKey)); // 压栈保护，防止下一次分配触发 GC 回收它

    Value handleVal;
    bool found = tableGet(&instance->fields, OBJ_VAL(handleKey), &handleVal);
    
    pop(vm); // 使用完毕弹出

    if (!found) return NULL;
    if (!IS_OBJ(handleVal)) return NULL;
    
    Obj* obj = AS_OBJ(handleVal);
    if (obj->type != expectedType) return NULL;
    return obj;
}

static void setHandle(VM* vm, ObjInstance* instance, Obj* internalObj) {
    // [修复] 每次调用时获取字符串
    ObjString* handleKey = copyString(vm, "_handle", 7);
    push(vm, OBJ_VAL(handleKey)); // 压栈保护

    Value val = OBJ_VAL(internalObj);
    tableSet(vm, &instance->fields, OBJ_VAL(handleKey), val);

    pop(vm); // 弹出
}

// --- Clip 类实现 ---

// 构造函数: Clip(path)
// VM 传递 argCount = 1 (path)
Value clipInit(VM* vm, i32 argCount, Value* args) {
    // 检查参数数量 (期望 1 个显式参数)
    if (argCount != 1 || !IS_STRING(args[0])) {
        fprintf(stderr, "Usage: Clip(path: String)\n");
        return NIL_VAL;
    }
    
    ObjInstance* thisObj = GET_SELF; 
    ObjString* path = AS_STRING(args[0]);

    // 加载元数据
    VideoMeta meta = load_video_metadata(vm, path->chars);
    if (!meta.success) {
        fprintf(stderr, "Runtime Error: Could not load video metadata from '%s'\n", path->chars);
        // 初始化失败返回自身，但 handle 为空，后续调用会报错，或者可以在此返回 NIL
        return OBJ_VAL(thisObj);
    }

    // 创建底层对象
    ObjClip* clip = newClip(vm, path);
    clip->duration = meta.duration;
    clip->width = meta.width;
    clip->height = meta.height;
    clip->fps = meta.fps;

    // 绑定 Handle
    setHandle(vm, thisObj, (Obj*)clip);

    // [关键修复] 同步属性到 Luna 实例，以便 print clip.width 能工作
    SET_PROP(thisObj, "width", clip->width);
    SET_PROP(thisObj, "height", clip->height);
    SET_PROP(thisObj, "fps", clip->fps);
    SET_PROP(thisObj, "duration", clip->duration);
    
    // 初始化其他可读属性，防止访问时报错
    SET_PROP(thisObj, "in_point", clip->in_point);
    SET_PROP(thisObj, "default_scale_x", clip->default_scale_x);
    SET_PROP(thisObj, "default_scale_y", clip->default_scale_y);
    SET_PROP(thisObj, "default_x", clip->default_x);
    SET_PROP(thisObj, "default_y", clip->default_y);
    SET_PROP(thisObj, "default_opacity", clip->default_opacity);

    // 返回实例本身
    return OBJ_VAL(thisObj);
}

// trim(start, duration)
Value clipTrim(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* clip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), OBJ_CLIP);
    if (!clip || argCount != 2) return NIL_VAL;

    double start = AS_NUMBER(args[0]);
    double duration = AS_NUMBER(args[1]);
    
    if (start < 0) start = 0;
    clip->in_point = start;
    clip->duration = duration;

    // 同步更新属性
    SET_PROP(thisObj, "in_point", start);
    SET_PROP(thisObj, "duration", duration);
    
    return NIL_VAL;
}

Value clipExport(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* clip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), OBJ_CLIP);
    if (!clip || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    
    ObjString* filename = AS_STRING(args[0]);
    export_video_clip(vm, clip, filename->chars);
    return NIL_VAL;
}

Value clipSetScale(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* clip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), OBJ_CLIP);
    if (!clip || argCount < 1) return NIL_VAL;
    
    double sx = AS_NUMBER(args[0]);
    double sy = (argCount > 1) ? AS_NUMBER(args[1]) : sx;
    
    clip->default_scale_x = sx;
    clip->default_scale_y = sy;

    // 同步更新属性
    SET_PROP(thisObj, "default_scale_x", sx);
    SET_PROP(thisObj, "default_scale_y", sy);

    return NIL_VAL;
}

Value clipSetPos(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* clip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), OBJ_CLIP);
    if (!clip || argCount != 2) return NIL_VAL;
    
    clip->default_x = AS_NUMBER(args[0]);
    clip->default_y = AS_NUMBER(args[1]);

    // 同步更新属性
    SET_PROP(thisObj, "default_x", clip->default_x);
    SET_PROP(thisObj, "default_y", clip->default_y);

    return NIL_VAL;
}

Value clipSetOpacity(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* clip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), OBJ_CLIP);
    if (!clip || argCount != 1) return NIL_VAL;
    
    double val = AS_NUMBER(args[0]);
    if (val < 0.0) val = 0.0; if (val > 1.0) val = 1.0;
    clip->default_opacity = val;

    // 同步更新属性
    SET_PROP(thisObj, "default_opacity", val);

    return NIL_VAL;
}

// --- Timeline 类实现 ---

// Timeline(width, height, fps)
Value timelineInit(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3) {
        fprintf(stderr, "Usage: Timeline(width, height, fps)\n");
        return NIL_VAL;
    }
    
    ObjInstance* thisObj = GET_SELF;
    double w = AS_NUMBER(args[0]);
    double h = AS_NUMBER(args[1]);
    double fps = AS_NUMBER(args[2]);
    
    ObjTimeline* tl = newTimeline(vm, (u32)w, (u32)h, fps);
    setHandle(vm, thisObj, (Obj*)tl);

    SET_PROP(thisObj, "duration", 0); 
    
    return OBJ_VAL(thisObj);
}

// add(trackId, clipInstance, start)
Value timelineAdd(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), OBJ_TIMELINE);
    if (!tlObj || argCount != 3) return NIL_VAL;
    
    i32 trackIdx = (i32)AS_NUMBER(args[0]);
    Value clipVal = args[1];
    double start = AS_NUMBER(args[2]);
    
    ObjClip* clip = (ObjClip*)getHandle(vm, clipVal, OBJ_CLIP);
    if (clip == NULL) {
        fprintf(stderr, "Runtime Error: Timeline.add argument 2 must be a Clip instance.\n");
        return NIL_VAL;
    }
    
    while (tlObj->timeline->track_count <= (u32)trackIdx) {
        timeline_add_track(vm, tlObj->timeline);
    }
    
    timeline_add_clip(vm, tlObj->timeline, trackIdx, clip, start);

    // 更新 timeline 的 duration
    SET_PROP(thisObj, "duration", tlObj->timeline->duration);

    return NIL_VAL;
}

// --- Project 类实现 ---

// Project(width, height, fps)
Value projectInit(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3) {
        fprintf(stderr, "Usage: Project(width, height, fps)\n");
        return NIL_VAL;
    }
   
    ObjInstance* thisObj = GET_SELF;
    double w = AS_NUMBER(args[0]);
    double h = AS_NUMBER(args[1]);
    double fps = AS_NUMBER(args[2]);
   
    ObjProject* proj = newProject(vm, (u32)w, (u32)h, fps);
    setHandle(vm, thisObj, (Obj*)proj);

    // 同步属性
    SET_PROP(thisObj, "width", w);
    SET_PROP(thisObj, "height", h);
    SET_PROP(thisObj, "fps", fps);
    SET_PROP(thisObj, "duration", 0);
   
    return OBJ_VAL(thisObj);
}

// setTimeline(tl)
Value projectSetTimeline(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), OBJ_PROJECT);
    if (!proj || argCount != 1) return NIL_VAL;
   
    Value tlVal = args[0];
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, tlVal, OBJ_TIMELINE);
    if (!tlObj) {
        fprintf(stderr, "Runtime Error: Project.setTimeline argument must be a Timeline instance.\n");
        return NIL_VAL;
    }
   
    proj->project->timeline = tlObj->timeline;
    return NIL_VAL;
}

Value projectPreview(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), OBJ_PROJECT);
    if (proj) {
        vm->active_project = proj->project;
        printf("[Binding] Project registered for preview.\n");
    }
    return NIL_VAL;
}

// --- 注册系统 ---

static void defineNativeMethod(VM* vm, ObjClass* klass, const char* name, NativeFn func) {
    ObjNative* native = newNative(vm, func);
    push(vm, OBJ_VAL(native));
    ObjString* methodName = copyString(vm, name, (int)strlen(name));
    push(vm, OBJ_VAL(methodName));
    tableSet(vm, &klass->methods, OBJ_VAL(methodName), OBJ_VAL(native));
    pop(vm);
    pop(vm);
}

static void defineClass(VM* vm, const char* name, NativeFn initFn, void (*methodRegistrar)(VM*, ObjClass*)) {
    ObjString* className = copyString(vm, name, (int)strlen(name));
    push(vm, OBJ_VAL(className));
    ObjClass* klass = newClass(vm, className);
    push(vm, OBJ_VAL(klass));
    
    defineNativeMethod(vm, klass, "init", initFn);
    if (methodRegistrar) methodRegistrar(vm, klass);
    
    tableSet(vm, &vm->globals, OBJ_VAL(className), OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

static void registerClipMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "trim", clipTrim);
    defineNativeMethod(vm, klass, "export", clipExport);
    defineNativeMethod(vm, klass, "setScale", clipSetScale);
    defineNativeMethod(vm, klass, "setPos", clipSetPos);
    defineNativeMethod(vm, klass, "setOpacity", clipSetOpacity);
}

static void registerTimelineMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "add", timelineAdd);
}

static void registerProjectMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setTimeline", projectSetTimeline);
    defineNativeMethod(vm, klass, "preview", projectPreview);
}

void registerVideoBindings(VM* vm) {
    // 移除了 static 字符串预分配，避免 GC 悬空指针
    defineClass(vm, "Clip", clipInit, registerClipMethods);
    defineClass(vm, "Timeline", timelineInit, registerTimelineMethods);
    defineClass(vm, "Project", projectInit, registerProjectMethods);
}