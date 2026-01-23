// src/engine/bridge/bridge.c

#include "object.h"
#include "core/memory.h"  // 需要 ALLOCATE, FREE, markObject
#include "engine/engine.h"
#include "core/vm/vm.h"      // 需要 VM 定义

// ============================================================================
// 1. Clip Implementation
// ============================================================================

static void clipFree(VM* vm, Obj* obj) {
    ObjClip* oClip = (ObjClip*)obj;
    if (oClip->clip) {
        // 调用纯 C 的释放函数
        clip_free(oClip->clip);
        oClip->clip = NULL;
    }
}

// Clip 内部现在只持有 malloc 出来的内存，不再直接持有 ObjString (path 已经深拷贝了)
// 所以通常不需要 mark，除非我们在 Clip->user_data 里藏了别的 VM 对象。
// 这里暂时留空或保留基本的结构。
static void clipMark(VM* vm, Obj* obj) {
    // 如果 Clip 结构体未来需要引用其他 Obj (如滤镜列表)，则通过 oClip->clip 访问并标记
}

const ForeignClassMethods ClipMethods = {
    "clip",
    NULL,
    clipFree, // 必须注册，因为 Clip* 是 malloc 的
    clipMark
};

ObjClip* newClip(VM* vm, ObjString* path) {
    ObjClip* obj = (ObjClip*)newForeign(vm, sizeof(ObjClip), &ClipMethods);

    // 1. 创建底层 C 模型
    // 注意：path->chars 是 raw char*
    obj->clip = clip_create(path->chars);
    
    // 2. 建立反向连接 (用于 GC 标记阶段从 Timeline 回溯)
    obj->clip->user_data = obj;

    return obj;
}

// ============================================================================
// 2. Timeline Implementation
// ============================================================================

// GC 释放阶段回调：必须释放 timeline_create 分配的 C 内存
static void timelineFree(VM* vm, Obj* obj) {
    ObjTimeline* oTl = (ObjTimeline*)obj;
    if (oTl->timeline) {
        // 调用 engine/timeline.c 中的销毁函数
        timeline_free(vm, oTl->timeline); 
        oTl->timeline = NULL;
    }
}

// GC 标记阶段回调
static void timelineMark(VM* vm, Obj* obj) {
    ObjTimeline* oTl = (ObjTimeline*)obj;
    if (oTl->timeline) {
        // 调用 engine/timeline.c 中的标记函数，遍历所有 Track 和 Clips
        timeline_mark(vm, oTl->timeline);
    }
}

const ForeignClassMethods TimelineMethods = {
    "timeline",
    NULL,
    timelineFree, // 必须注册
    timelineMark
};

ObjTimeline* newTimeline(VM* vm, u32 width, u32 height, double fps) {
    ObjTimeline* obj = (ObjTimeline*)newForeign(vm, sizeof(ObjTimeline), &TimelineMethods);
    // 调用实际的引擎创建逻辑
    obj->timeline = timeline_create(vm, width, height, fps);
    return obj;
}

// ============================================================================
// 3. Project Implementation
// ============================================================================

static void projectFree(VM* vm, Obj* obj) {
    ObjProject* oProj = (ObjProject*)obj;
    if (oProj->project) {
        // Project 结构体是 malloc 出来的，需要释放
        // 注意：Project 内部的 timeline 对象由 VM 管理，不需要在这里手动释放 timeline，
        // 只要断开引用，GC 会自动处理。但如果 Project 强持有 timeline 指针而非 ObjTimeline，
        // 则逻辑会有所不同。
        // 根据你之前的代码逻辑，Project 似乎持有的是 Timeline* (raw pointer) 还是 ObjTimeline*?
        // 假设 Project 是为了渲染上下文存在的，这里简单释放结构体内存：
        FREE(vm, Project, oProj->project);
        oProj->project = NULL;
    }
}

static void projectMark(VM* vm, Obj* obj) {
    ObjProject* oProj = (ObjProject*)obj;
    if (oProj->project && oProj->project->timeline) {
        // 标记 Project 持有的 Timeline
        // 注意：如果 Project->timeline 是 Timeline* (Raw C Ptr)，我们需要手动递归标记
        timeline_mark(vm, oProj->project->timeline);
    }
}

const ForeignClassMethods ProjectMethods = {
    "project",
    NULL,
    projectFree,
    projectMark
};

ObjProject* newProject(VM* vm, u32 width, u32 height, double fps) {
    ObjProject* obj = (ObjProject*)newForeign(vm, sizeof(ObjProject), &ProjectMethods);
    
    // 分配 C 层 Project 结构
    obj->project = ALLOCATE(vm, Project, 1);
    
    // 初始化
    obj->project->width = width;
    obj->project->height = height;
    obj->project->fps = fps;
    obj->project->timeline = NULL;
    obj->project->use_preview_range = false;
    obj->project->preview_start = 0.0;
    obj->project->preview_end = 0.0;

    return obj;
}