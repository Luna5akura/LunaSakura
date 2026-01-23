// src/binding/bind_video.c

#include <stdio.h>
#include <string.h>
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/engine.h" // 包含 bridge/object.h (其中定义了 ObjClip, ObjTimeline 等)

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
    EngineContext* ctx = (EngineContext*)vm->user_data;
    return ctx ? ctx->active_project : NULL;
}

void reset_active_project(VM* vm) {
    EngineContext* ctx = (EngineContext*)vm->user_data;
    if (ctx) ctx->active_project = NULL;
}

// --- 内部辅助函数 ---

// 获取 Handle 并校验类型
static Obj* getHandle(VM* vm, Value instanceVal, const ForeignClassMethods* expectedMethods) {
    if (!IS_INSTANCE(instanceVal)) return NULL;
    ObjInstance* instance = AS_INSTANCE(instanceVal);

    ObjString* handleKey = copyString(vm, "_handle", 7);
    push(vm, OBJ_VAL(handleKey)); // 压栈保护

    Value handleVal;
    bool found = tableGet(&instance->fields, OBJ_VAL(handleKey), &handleVal);
    
    pop(vm); // 使用完毕弹出

    if (!found) return NULL;
    
    // 1. 必须是对象
    if (!IS_OBJ(handleVal)) return NULL;
    
    // 2. 必须是宿主对象 (OBJ_FOREIGN)
    if (!IS_FOREIGN(handleVal)) return NULL;

    // 3. 必须匹配具体的方法表指针 (Is instance of Clip/Timeline/...)
    ObjForeign* foreign = AS_FOREIGN(handleVal);
    if (foreign->methods != expectedMethods) return NULL;
    
    return (Obj*)foreign;
}

static void setHandle(VM* vm, ObjInstance* instance, Obj* internalObj) {
    ObjString* handleKey = copyString(vm, "_handle", 7);
    push(vm, OBJ_VAL(handleKey)); // 压栈保护

    Value val = OBJ_VAL(internalObj);
    tableSet(vm, &instance->fields, OBJ_VAL(handleKey), val);

    pop(vm); // 弹出
}

// --- Clip 类实现 ---

// 构造函数: Clip(path)
Value clipInit(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        fprintf(stderr, "Usage: Clip(path: String)\n");
        return NIL_VAL;
    }
   
    ObjInstance* thisObj = GET_SELF;
    ObjString* path = AS_STRING(args[0]);
    
    // 1. 加载元数据 (Probe)
    VideoMeta meta = load_video_metadata(vm, path->chars);
    if (!meta.success) {
        fprintf(stderr, "Runtime Error: Could not load video metadata from '%s'\n", path->chars);
        // 即便失败也可以返回对象，但属性可能为空，或者在这里返回 NIL 表示失败
        return OBJ_VAL(thisObj);
    }
    
    // 2. 创建底层对象 (ObjClip 内部会自动调用 clip_create)
    ObjClip* objClip = newClip(vm, path);
    
    // [修改] 获取内部纯 C 结构体指针
    Clip* inner = objClip->clip; 
    
    // [修改] 填充纯 C 结构体数据
    inner->duration = meta.duration;
    inner->width = meta.width;
    inner->height = meta.height;
    inner->fps = meta.fps;
    inner->has_audio = true; // 简化假设，实际应从 meta 读取
    inner->has_video = true;
    
    // clip_create 已经设置了默认值，但这里覆盖确保一致
    inner->default_scale_x = 1.0;
    inner->default_scale_y = 1.0;
    inner->default_opacity = 1.0;
    inner->volume = 1.0;
    inner->default_x = 0.0; 
    inner->default_y = 0.0;
    
    // 3. 绑定 Handle
    setHandle(vm, thisObj, (Obj*)objClip);
    
    // 4. 同步属性到 Luna 实例 (供脚本读取)
    SET_PROP(thisObj, "width", inner->width);
    SET_PROP(thisObj, "height", inner->height);
    SET_PROP(thisObj, "volume", inner->volume);
    SET_PROP(thisObj, "fps", inner->fps);
    SET_PROP(thisObj, "duration", inner->duration);
    SET_PROP(thisObj, "has_audio", inner->has_audio ? 1 : 0);
    SET_PROP(thisObj, "has_video", inner->has_video ? 1 : 0);
   
    SET_PROP(thisObj, "in_point", inner->in_point);
    SET_PROP(thisObj, "default_scale_x", inner->default_scale_x);
    SET_PROP(thisObj, "default_scale_y", inner->default_scale_y);
    SET_PROP(thisObj, "default_x", inner->default_x);
    SET_PROP(thisObj, "default_y", inner->default_y);
    SET_PROP(thisObj, "default_opacity", inner->default_opacity);
    
    return OBJ_VAL(thisObj);
}

Value clipSetVolume(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1) return NIL_VAL;
    
    double val = AS_NUMBER(args[0]);
    if (val < 0.0) val = 0.0;
    
    // [修改] 写入内部结构
    objClip->clip->volume = val;
    
    SET_PROP(thisObj, "volume", val);
    return NIL_VAL;
}

Value clipTrim(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 2) return NIL_VAL;

    double start = AS_NUMBER(args[0]);
    double duration = AS_NUMBER(args[1]);
    
    if (start < 0) start = 0;
    
    // [修改] 写入内部结构
    objClip->clip->in_point = start;
    objClip->clip->duration = duration; // 注意：这里的 duration 是截取后的时长

    SET_PROP(thisObj, "in_point", start);
    SET_PROP(thisObj, "duration", duration);
    
    return NIL_VAL;
}

Value clipExport(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    
    ObjString* filename = AS_STRING(args[0]);
    
    // transcode_clip 签名通常接受 ObjClip* (bridge对象)，
    // 内部再访问 objClip->clip 进行处理。
    // 如果 transcode_clip 签名已改为接受 Clip*，这里则传 objClip->clip
    // 假设 transcoder.h 依然接受 ObjClip* 以方便 bridge 调用：
    transcode_clip(vm, objClip->clip, filename->chars);
    
    return NIL_VAL;
}

Value clipSetScale(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount < 1) return NIL_VAL;
    
    double sx = AS_NUMBER(args[0]);
    double sy = (argCount > 1) ? AS_NUMBER(args[1]) : sx;
    
    // [修改]
    objClip->clip->default_scale_x = sx;
    objClip->clip->default_scale_y = sy;

    SET_PROP(thisObj, "default_scale_x", sx);
    SET_PROP(thisObj, "default_scale_y", sy);

    return NIL_VAL;
}

Value clipSetPos(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 2) return NIL_VAL;
    
    // [修改]
    objClip->clip->default_x = AS_NUMBER(args[0]);
    objClip->clip->default_y = AS_NUMBER(args[1]);

    SET_PROP(thisObj, "default_x", objClip->clip->default_x);
    SET_PROP(thisObj, "default_y", objClip->clip->default_y);

    return NIL_VAL;
}

Value clipSetOpacity(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1) return NIL_VAL;
    
    double val = AS_NUMBER(args[0]);
    if (val < 0.0) val = 0.0; 
    if (val > 1.0) val = 1.0;
    
    // [修改]
    objClip->clip->default_opacity = val;

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
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    if (!tlObj || argCount != 3) return NIL_VAL;
    
    i32 trackIdx = (i32)AS_NUMBER(args[0]);
    Value clipVal = args[1];
    double start = AS_NUMBER(args[2]);
    
    ObjClip* objClip = (ObjClip*)getHandle(vm, clipVal, &ClipMethods);
    if (objClip == NULL) {
        fprintf(stderr, "Runtime Error: Timeline.add argument 2 must be a Clip instance.\n");
        return NIL_VAL;
    }
    
    while (tlObj->timeline->track_count <= (u32)trackIdx) {
        timeline_add_track(vm, tlObj->timeline);
    }
    
    // [修改] 关键点：将封装对象解包，传递纯数据指针 (Clip*) 给引擎核心
    // timeline_add_clip 现在定义在 engine/model/timeline.h 中，接受 Clip*
    timeline_add_clip(vm, tlObj->timeline, trackIdx, objClip->clip, start);

    // 更新时长
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

    // 假设 Project 结构体也有同样的字段
    SET_PROP(thisObj, "width", w);
    SET_PROP(thisObj, "height", h);
    SET_PROP(thisObj, "fps", fps);
    SET_PROP(thisObj, "duration", 0);
   
    return OBJ_VAL(thisObj);
}

// setTimeline(tl)
Value projectSetTimeline(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), &ProjectMethods);
    if (!proj || argCount != 1) return NIL_VAL;
   
    Value tlVal = args[0];
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, tlVal, &TimelineMethods);
    if (!tlObj) {
        fprintf(stderr, "Runtime Error: Project.setTimeline argument must be a Timeline instance.\n");
        return NIL_VAL;
    }
   
    // 指针赋值：将 Timeline 挂载到 Project 上
    // Project* 和 Timeline* 都是纯 C 结构体
    proj->project->timeline = tlObj->timeline;
    
    return NIL_VAL;
}

Value projectPreview(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), &ProjectMethods);
    
    if (!proj) return NIL_VAL;

    proj->project->use_preview_range = false;

    if (argCount == 2) {
        if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
             fprintf(stderr, "Usage: Project.preview(start: Number, end: Number)\n");
             return NIL_VAL;
        }
        double start = AS_NUMBER(args[0]);
        double end = AS_NUMBER(args[1]);

        if (end > start) {
            proj->project->use_preview_range = true;
            proj->project->preview_start = start;
            proj->project->preview_end = end;
            printf("[Binding] Project preview range set: %.2f - %.2f\n", start, end);
        }
    }

    EngineContext* ctx = (EngineContext*)vm->user_data;
    if (ctx) {
        ctx->active_project = proj->project;
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
    defineNativeMethod(vm, klass, "setVolume", clipSetVolume);
}

static void registerTimelineMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "add", timelineAdd);
}

static void registerProjectMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setTimeline", projectSetTimeline);
    defineNativeMethod(vm, klass, "preview", projectPreview);
}

void registerVideoBindings(VM* vm) {
    defineClass(vm, "Clip", clipInit, registerClipMethods);
    defineClass(vm, "Timeline", timelineInit, registerTimelineMethods);
    defineClass(vm, "Project", projectInit, registerProjectMethods);
}