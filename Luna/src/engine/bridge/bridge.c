// src/engine/bridge/bridge.c

#include "object.h"
#include "core/memory.h"  // 这里包含真正的 memory 实现
#include "core/vm/vm.h"   // 这里包含 VM 定义
#include "engine/model/timeline.h" 


static void markRawTimeline(VM* vm, Timeline* tl) {
    if (!tl) return;

    for (uint32_t i = 0; i < tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        for (uint32_t j = 0; j < track->clip_count; j++) {
            TimelineClip* tc = &track->clips[j];
            
            // 关键点：通过 user_data 找回 ObjClip
            if (tc->media && tc->media->user_data) {
                markObject(vm, (Obj*)tc->media->user_data);
            }
        }
    }
}

// --- 1. 适配器实现 ---

// 这个静态函数符合 ReallocFn 签名
static void* vm_realloc_wrapper(void* ctx, void* ptr, size_t old_size, size_t new_size) {
    VM* vm = (VM*)ctx;
    // 调用 core/memory.c 中的 reallocate
    // 这样 Model 层的分配依然会被计入 vm->bytesAllocated，从而触发 GC
    return reallocate(vm, ptr, old_size, new_size);
}

// --- 2. Timeline 对象实现 ---

static void timelineFree(VM* vm, Obj* obj) {
    ObjTimeline* oTl = (ObjTimeline*)obj;
    if (oTl->timeline) {
        // 调用 pure C 的释放函数
        timeline_free(oTl->timeline);
        oTl->timeline = NULL;
    }
}

// --- 关键：手动标记逻辑 ---
// Model 层不知道 "markObject"，所以由 Bridge 层遍历 C 结构体来找到需要标记的对象
static void timelineMark(VM* vm, Obj* obj) {
    ObjTimeline* oTl = (ObjTimeline*)obj;
    if (oTl->timeline) {
        // 复用辅助函数
        markRawTimeline(vm, oTl->timeline);
    }
}

const ForeignClassMethods TimelineMethods = {
    "timeline",
    NULL,
    timelineFree,
    timelineMark
};

ObjTimeline* newTimeline(VM* vm, uint32_t width, uint32_t height, double fps) {
    ObjTimeline* obj = (ObjTimeline*)newForeign(vm, sizeof(ObjTimeline), &TimelineMethods);
    
    // 构造分配器接口，传入 vm 指针
    Allocator allocator;
    allocator.ctx = vm;
    allocator.fn = vm_realloc_wrapper;

    // 传入分配器创建 Model
    obj->timeline = timeline_create(&allocator, width, height, fps);
    return obj;
}

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
    if (oProj->timelineObj) {
        // 标记 ObjTimeline，由于 ObjTimeline 会标记它内部的 Clip，
        // 所以这里不需要再调用 markRawTimeline 了，一举两得。
        markObject(vm, (Obj*)oProj->timelineObj);
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