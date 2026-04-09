// src/binding/bind_video.c

#include <stdio.h>
#include <string.h>
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/engine.h"
#include "engine/effect/registry.h"   // 新增
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
static void sync_common_props(VM* vm, ObjInstance* obj, Clip* inner) {
    SET_PROP(obj, "default_scale_x", inner->default_scale_x);
    SET_PROP(obj, "default_scale_y", inner->default_scale_y);
    SET_PROP(obj, "default_x", inner->default_x);
    SET_PROP(obj, "default_y", inner->default_y);
    SET_PROP(obj, "default_rotation", inner->default_rotation);
    SET_PROP(obj, "default_opacity", inner->default_opacity);
    SET_PROP(obj, "volume", inner->volume);
    SET_PROP(obj, "in_point", inner->in_point);
    SET_PROP(obj, "duration", inner->duration);
}
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
Value videoInit(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        fprintf(stderr, "Usage: Clip(path: String)\n");
        return NIL_VAL;
    }
 
    ObjInstance* thisObj = GET_SELF;
    ObjString* path = AS_STRING(args[0]);
  
    // 1. Probe
    VideoMeta meta = load_video_metadata(path->chars);
    if (!meta.success) {
        fprintf(stderr, "Runtime Error: Could not load video metadata from '%s'\n", path->chars);
        return OBJ_VAL(thisObj); // 返回空壳或抛出异常
    }
  
    // 2. Create Media Clip
    ObjClip* objClip = newClip(vm, path);
    if (objClip->clip) clip_free(objClip->clip);
    objClip->clip = clip_create_media(path->chars);
  
    Clip* inner = objClip->clip;
    inner->duration = meta.duration;
    inner->width = meta.width;
    inner->height = meta.height;
    inner->fps = meta.fps;
    inner->has_audio = true; // 实际应从 meta 获取
    inner->has_video = true;
  
    // 3. Bind
    setHandle(vm, thisObj, (Obj*)objClip);
  
    // 4. Sync Properties
    SET_PROP(thisObj, "width", inner->width);
    SET_PROP(thisObj, "height", inner->height);
    SET_PROP(thisObj, "fps", inner->fps);
    SET_PROP(thisObj, "has_video", 1);
    SET_PROP(thisObj, "has_audio", 1);
    sync_common_props(vm, thisObj, inner);
  
    return OBJ_VAL(thisObj);
}
// 构造函数: Text(content: String)
Value textInit(VM* vm, i32 argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        fprintf(stderr, "Usage: Text(content: String)\n");
        return NIL_VAL;
    }
  
    ObjInstance* thisObj = GET_SELF;
    char* content = AS_CSTRING(args[0]);
    // 1. Create Text Clip (使用默认值)
    ObjClip* objClip = newClip(vm, AS_STRING(args[0]));
    if (objClip->clip) clip_free(objClip->clip);
  
    // 默认配置：Arial, 32px, 白色
    // 注意：这里的 font path 必须存在，否则渲染会失败。
    // 在实际项目中，可以使用内嵌字体或相对路径。
    objClip->clip = clip_create_text(content, "arial.ttf", 32, 255, 255, 255);
    objClip->clip->text.letter_spacing = 0.0f;
    objClip->clip->text.stroke_enabled = false;
    objClip->clip->text.stroke_width = 2.0f;
    objClip->clip->text.stroke_color.r = 0;
    objClip->clip->text.stroke_color.g = 0;
    objClip->clip->text.stroke_color.b = 0;
    objClip->clip->text.stroke_color.a = 255;
  
    Clip* inner = objClip->clip;
    // 2. Bind
    setHandle(vm, thisObj, (Obj*)objClip);
    // 3. Sync Properties
    SET_PROP(thisObj, "width", 0);
    SET_PROP(thisObj, "height", 0);
    SET_PROP(thisObj, "fps", 0);
    SET_PROP(thisObj, "has_video", 0);
    SET_PROP(thisObj, "has_audio", 0);
    sync_common_props(vm, thisObj, inner);
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
Value clipSetRotation(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1) return NIL_VAL;
  
    double val = AS_NUMBER(args[0]);
  
    objClip->clip->default_rotation = val;
    SET_PROP(thisObj, "default_rotation", val);
    return NIL_VAL;
}
// --- Text 专用 Setters ---
Value textSetFont(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
  
    if (objClip->clip->type == CLIP_TYPE_TEXT) {
        if (objClip->clip->text.font_path) free(objClip->clip->text.font_path);
        objClip->clip->text.font_path = strdup(AS_CSTRING(args[0]));
    }
    return NIL_VAL;
}
Value textSetSize(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
  
    if (objClip->clip->type == CLIP_TYPE_TEXT) {
        objClip->clip->text.font_size = (u32)AS_NUMBER(args[0]);
    }
    return NIL_VAL;
}
Value textSetColor(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 3) return NIL_VAL;
  
    if (objClip->clip->type == CLIP_TYPE_TEXT) {
        objClip->clip->text.color.r = (u8)AS_NUMBER(args[0]);
        objClip->clip->text.color.g = (u8)AS_NUMBER(args[1]);
        objClip->clip->text.color.b = (u8)AS_NUMBER(args[2]);
    }
    return NIL_VAL;
}

Value textSetLetterSpacing(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1) return NIL_VAL;
    objClip->clip->text.letter_spacing = AS_NUMBER(args[0]);
    SET_PROP(thisObj, "letter_spacing", objClip->clip->text.letter_spacing);
    return NIL_VAL;
}

Value textSetStroke(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 5) {  // 改为 5 个参数（enabled, width, r, g, b）
        fprintf(stderr, "Usage: setStroke(enabled: bool, width: number, r: number, g: number, b: number)\n");
        return NIL_VAL;
    }

    if (objClip->clip->type == CLIP_TYPE_TEXT) {
        objClip->clip->text.stroke_enabled = AS_BOOL(args[0]);
        objClip->clip->text.stroke_width   = AS_NUMBER(args[1]);
        objClip->clip->text.stroke_color.r = (u8)AS_NUMBER(args[2]);
        objClip->clip->text.stroke_color.g = (u8)AS_NUMBER(args[3]);
        objClip->clip->text.stroke_color.b = (u8)AS_NUMBER(args[4]);
        objClip->clip->text.stroke_color.a = 255;  // 描边默认不透明
    }

    SET_PROP(thisObj, "stroke_enabled", objClip->clip->text.stroke_enabled);
    SET_PROP(thisObj, "stroke_width",   objClip->clip->text.stroke_width);
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
// add(trackId, clipInstance, start) -> ClipInstance
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
        timeline_add_track(tlObj->timeline);
    }
  
    // 添加剪辑并获取索引
    i32 clipIdx = timeline_add_clip(tlObj->timeline, trackIdx, objClip->clip, start);
    if (clipIdx < 0) return NIL_VAL;
  
    // 创建 ObjTimelineClip
    TimelineClip* tc = &tlObj->timeline->tracks[trackIdx].clips[clipIdx];
    ObjTimelineClip* objTc = newTimelineClip(vm, tc, &tlObj->timeline->allocator);
  
    // 创建 Luna 端的 ClipInstance 实例
    ObjString* className = copyString(vm, "ClipInstance", 12);
    Value classVal;
    if (!tableGet(&vm->globals, OBJ_VAL(className), &classVal) || !IS_CLASS(classVal)) {
        fprintf(stderr, "Runtime Error: ClipInstance class not found.\n");
        return NIL_VAL;
    }
    ObjInstance* instance = newInstance(vm, AS_CLASS(classVal));
    setHandle(vm, instance, (Obj*)objTc);
  
    // 更新时长
    SET_PROP(thisObj, "duration", tlObj->timeline->duration);
    return OBJ_VAL(instance);
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
// --- 新增: ClipInstance 方法 ---
// ClipInstance 无 init (dummy，如果需要)
Value clipInstanceInit(VM* vm, i32 argCount, Value* args) {
    return NIL_VAL;  // 无需初始化，用户不直接 new
}
// addKeyframe(property, time, value, type, [weight])
Value clipInstanceAddKeyframe(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    if (!objTc || argCount < 4 || !IS_STRING(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2]) || !IS_STRING(args[3])) return NIL_VAL;
  
    const char* prop = AS_CSTRING(args[0]);
    double time = AS_NUMBER(args[1]);
    double value = AS_NUMBER(args[2]);
    const char* type_str = AS_CSTRING(args[3]);
    double weight = (argCount >= 5) ? AS_NUMBER(args[4]) : 0.0;
  
    KeyframeType type;
    if (strcmp(type_str, "hold") == 0) type = KEYFRAME_HOLD;
    else if (strcmp(type_str, "linear") == 0) type = KEYFRAME_LINEAR;
    else if (strcmp(type_str, "bezier") == 0) type = KEYFRAME_BEZIER;
    else {
        fprintf(stderr, "Invalid keyframe type: %s\n", type_str);
        return NIL_VAL;
    }
  
    Animation* anim = NULL;
    if (strcmp(prop, "x") == 0) anim = &objTc->clip->anim.x;
    else if (strcmp(prop, "y") == 0) anim = &objTc->clip->anim.y;
    else if (strcmp(prop, "scale_x") == 0) anim = &objTc->clip->anim.scale_x;
    else if (strcmp(prop, "scale_y") == 0) anim = &objTc->clip->anim.scale_y;
    else if (strcmp(prop, "rotation") == 0) anim = &objTc->clip->anim.rotation;
    else if (strcmp(prop, "opacity") == 0) anim = &objTc->clip->anim.opacity;
    else if (strcmp(prop, "volume") == 0) anim = &objTc->clip->anim.volume;
    else if (strcmp(prop, "font_size") == 0) anim = &objTc->clip->anim.font_size;
    else {
        fprintf(stderr, "Invalid property: %s\n", prop);
        return NIL_VAL;
    }
  
    add_keyframe(anim, objTc->allocator, time, value, type, weight);
    return NIL_VAL;
}
// addKeyframeWithPreset(property, time, value, preset)
Value clipInstanceAddKeyframeWithPreset(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    if (!objTc || argCount != 4 || !IS_STRING(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2]) || !IS_STRING(args[3])) return NIL_VAL;
  
    const char* prop = AS_CSTRING(args[0]);
    double time = AS_NUMBER(args[1]);
    double value = AS_NUMBER(args[2]);
    const char* preset = AS_CSTRING(args[3]);
  
    Animation* anim = NULL;
    if (strcmp(prop, "x") == 0) anim = &objTc->clip->anim.x;
    else if (strcmp(prop, "y") == 0) anim = &objTc->clip->anim.y;
    else if (strcmp(prop, "scale_x") == 0) anim = &objTc->clip->anim.scale_x;
    else if (strcmp(prop, "scale_y") == 0) anim = &objTc->clip->anim.scale_y;
    else if (strcmp(prop, "rotation") == 0) anim = &objTc->clip->anim.rotation;
    else if (strcmp(prop, "opacity") == 0) anim = &objTc->clip->anim.opacity;
    else if (strcmp(prop, "volume") == 0) anim = &objTc->clip->anim.volume;
    else if (strcmp(prop, "font_size") == 0) anim = &objTc->clip->anim.font_size;
    else {
        fprintf(stderr, "Invalid property: %s\n", prop);
        return NIL_VAL;
    }
  
    add_keyframe_with_preset(anim, objTc->allocator, time, value, preset);
    return NIL_VAL;
}
// --- 新增: 全局函数 addUserPreset(name, type, weight) ---
Value globalAddUserPreset(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3 || !IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_NUMBER(args[2])) return NIL_VAL;
  
    const char* name = AS_CSTRING(args[0]);
    const char* type_str = AS_CSTRING(args[1]);
    double weight = AS_NUMBER(args[2]);
  
    KeyframeType type;
    if (strcmp(type_str, "hold") == 0) type = KEYFRAME_HOLD;
    else if (strcmp(type_str, "linear") == 0) type = KEYFRAME_LINEAR;
    else if (strcmp(type_str, "bezier") == 0) type = KEYFRAME_BEZIER;
    else {
        fprintf(stderr, "Invalid preset type: %s\n", type_str);
        return NIL_VAL;
    }
  
    // 使用 vm 作为 allocator 上下文 (假设 vm_realloc_wrapper 已定义)
    Allocator allocator;
    init_allocator(&allocator, vm);
    add_user_preset(&allocator, name, type, weight);
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
  
    if (initFn) defineNativeMethod(vm, klass, "init", initFn);
    if (methodRegistrar) methodRegistrar(vm, klass);
  
    tableSet(vm, &vm->globals, OBJ_VAL(className), OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}
// 通用方法 (Clip 和 Text 都有)
static void registerCommonMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "trim", clipTrim);
    defineNativeMethod(vm, klass, "export", clipExport);
    defineNativeMethod(vm, klass, "setScale", clipSetScale);
    defineNativeMethod(vm, klass, "setPos", clipSetPos);
    defineNativeMethod(vm, klass, "setRotation", clipSetRotation);
    defineNativeMethod(vm, klass, "setOpacity", clipSetOpacity);
}
// Video Clip 独有
static void registerClipMethods(VM* vm, ObjClass* klass) {
    registerCommonMethods(vm, klass);
    defineNativeMethod(vm, klass, "setVolume", clipSetVolume);
    effect_bindings_register(vm);
}
// Text 独有
static void registerTextMethods(VM* vm, ObjClass* klass) {
    registerCommonMethods(vm, klass);
    defineNativeMethod(vm, klass, "setFont", textSetFont);
    defineNativeMethod(vm, klass, "setSize", textSetSize);
    defineNativeMethod(vm, klass, "setColor", textSetColor);
    defineNativeMethod(vm, klass, "setLetterSpacing", textSetLetterSpacing);
    defineNativeMethod(vm, klass, "setStroke", textSetStroke);
}
static void registerTimelineMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "add", timelineAdd);
}
static void registerProjectMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setTimeline", projectSetTimeline);
    defineNativeMethod(vm, klass, "preview", projectPreview);
}
static void registerClipInstanceMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "addKeyframe", clipInstanceAddKeyframe);
    defineNativeMethod(vm, klass, "addKeyframeWithPreset", clipInstanceAddKeyframeWithPreset);
}
void registerVideoBindings(VM* vm) {
    defineClass(vm, "Clip", videoInit, registerClipMethods);
    defineClass(vm, "Text", textInit, registerTextMethods);
    defineClass(vm, "Timeline", timelineInit, registerTimelineMethods);
    defineClass(vm, "Project", projectInit, registerProjectMethods);
    // 新增 ClipInstance (无 init)
    defineClass(vm, "ClipInstance", clipInstanceInit, registerClipInstanceMethods);
    // 新增全局 addUserPreset
    defineNative(vm, "addUserPreset", globalAddUserPreset);
}