// src/engine/object.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/object.h"
#include "core/memory.h"  // 需要 ALLOCATE, FREE, markObject
#include "core/vm/vm.h"      // 需要 VM 定义

#include "engine/timeline.h" 

// ============================================================================
// 1. Clip Implementation
// ============================================================================

// GC 标记阶段回调：告诉 GC Clip 引用了哪些对象
static void clipMark(VM* vm, Obj* obj) {
    ObjClip* clip = (ObjClip*)obj;
    if (clip->path) {
        markObject(vm, (Obj*)clip->path);
    }
}

// Clip 没有引用 malloc 分配的外部 C 指针，只有 VM 管理的 ObjString，
// 所以不需要专门的 free 回调，VM 回收 ObjClip 内存时会自动断开引用。
const ForeignClassMethods ClipMethods = {
    "clip",      // typeName
    NULL,        // allocate (可选)
    NULL,        // free (不需要手动释放)
    clipMark     // mark
};

ObjClip* newClip(VM* vm, ObjString* path) {
    // 1. 调用 Core 的通用构造器
    ObjClip* clip = (ObjClip*)newForeign(vm, sizeof(ObjClip), &ClipMethods);

    // 2. 初始化字段
    clip->path = path;
    
    // 设置默认值
    clip->duration = 0.0;
    clip->start_time = 0.0;
    clip->in_point = 0.0;
    clip->out_point = 0.0;
    clip->fps = 0.0;
    clip->volume = 1.0;
    clip->width = 0;
    clip->height = 0;
    clip->layer = 0;
    
    clip->has_video = false;
    clip->has_audio = false;
    clip->audio_channels = 0;
    clip->audio_sample_rate = 0;

    clip->default_scale_x = 1.0;
    clip->default_scale_y = 1.0;
    clip->default_x = 0.0;
    clip->default_y = 0.0;
    clip->default_opacity = 1.0;

    return clip;
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