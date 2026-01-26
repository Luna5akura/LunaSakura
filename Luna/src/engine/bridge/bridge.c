// src/engine/bridge/bridge.c

#include "object.h"
#include "core/memory.h" // 这里包含真正的 memory 实现
#include "core/vm/vm.h" // 这里包含 VM 定义
#include "engine/model/timeline.h"
static void* vm_realloc_wrapper(void* ctx, void* ptr, size_t old_size, size_t new_size) {
    VM* vm = (VM*)ctx;
    return reallocate(vm, ptr, old_size, new_size);
}
void init_allocator(Allocator* allocator, VM* vm) {
    allocator->ctx = vm;
    allocator->fn = vm_realloc_wrapper;
}
static void markRawTimeline(VM* vm, Timeline* tl) {
    if (!tl) return;
    for (uint32_t i = 0; i < tl->track_count; i++) {
        Track* track = &tl->tracks[i];
        for (uint32_t j = 0; j < track->clip_count; j++) {
            TimelineClip* tc = &track->clips[j];
            if (tc->media && tc->media->user_data) {
                markObject(vm, (Obj*)tc->media->user_data);
            }
        }
    }
}
static void timelineMark(VM* vm, Obj* obj) {
    ObjTimeline* oTl = (ObjTimeline*)obj;
    if (oTl->timeline) {
        markRawTimeline(vm, oTl->timeline);
    }
}
static void timelineFree(VM* vm, Obj* obj) {
    ObjTimeline* oTl = (ObjTimeline*)obj;
    if (oTl->timeline) {
        timeline_free(oTl->timeline);
        oTl->timeline = NULL;
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
    Allocator allocator;
    init_allocator(&allocator, vm);
    obj->timeline = timeline_create(&allocator, width, height, fps);
    return obj;
}
static void clipMark(VM* vm, Obj* obj) {
    // 如果 Clip 结构体未来需要引用其他 Obj (如滤镜列表)，则通过 oClip->clip 访问并标记
}
static void clipFree(VM* vm, Obj* obj) {
    ObjClip* oClip = (ObjClip*)obj;
    if (oClip->clip) {
        clip_free(oClip->clip);
        oClip->clip = NULL;
    }
}
const ForeignClassMethods ClipMethods = {
    "clip",
    NULL,
    clipFree,
    clipMark
};
ObjClip* newClip(VM* vm, ObjString* path) {
    ObjClip* obj = (ObjClip*)newForeign(vm, sizeof(ObjClip), &ClipMethods);
    obj->clip = clip_create_media(path->chars);
    obj->clip->user_data = obj;
    return obj;
}
static void projectMark(VM* vm, Obj* obj) {
    ObjProject* oProj = (ObjProject*)obj;
    if (oProj->timelineObj) {
        markObject(vm, (Obj*)oProj->timelineObj);
    }
}
static void projectFree(VM* vm, Obj* obj) {
    ObjProject* oProj = (ObjProject*)obj;
    if (oProj->project) {
        FREE(vm, Project, oProj->project);
        oProj->project = NULL;
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
    obj->project = ALLOCATE(vm, Project, 1);
    obj->project->width = width;
    obj->project->height = height;
    obj->project->fps = fps;
    obj->project->timeline = NULL;
    obj->project->use_preview_range = false;
    obj->project->preview_start = 0.0;
    obj->project->preview_end = 0.0;
    return obj;
}
// 新增: TimelineClip Mark/Free
static void timelineClipMark(VM* vm, Obj* obj) {
    // 如果需要标记内部引用，目前无
}
static void timelineClipFree(VM* vm, Obj* obj) {
    ObjTimelineClip* oTc = (ObjTimelineClip*)obj;
    oTc->clip = NULL;  // 不释放 TimelineClip*，因为它属于 Timeline
    oTc->allocator = NULL;
}
const ForeignClassMethods TimelineClipMethods = {
    "timeline_clip",
    NULL,
    timelineClipFree,
    timelineClipMark
};
ObjTimelineClip* newTimelineClip(VM* vm, TimelineClip* tc, Allocator* allocator) {
    ObjTimelineClip* obj = (ObjTimelineClip*)newForeign(vm, sizeof(ObjTimelineClip), &TimelineClipMethods);
    obj->clip = tc;
    obj->allocator = allocator;
    return obj;
}