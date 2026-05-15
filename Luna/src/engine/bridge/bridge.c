// src/engine/bridge/bridge.c

#include "object.h"
#include "core/memory.h" // 这里包含真正的 memory 实现
#include "core/vm/vm.h" // 这里包含 VM 定义
#include "engine/effect/filter_base.h"
#include "engine/model/timeline.h"
#include <string.h>
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
    ObjClip* oClip = (ObjClip*)obj;
    if (oClip->timelineObj) {
        markObject(vm, (Obj*)oClip->timelineObj);
    }
    if (oClip->clip && oClip->clip->type == CLIP_TYPE_TEXT) {
        for (u32 i = 0; i < oClip->clip->text.animator_count; i++) {
            TextAnimator* animator = &oClip->clip->text.animators[i];
            for (u32 j = 0; j < animator->expression_selector_count; j++) {
                if (animator->expression_selectors[j].has_callback) {
                    markValue(vm, animator->expression_selectors[j].callback);
                }
            }
        }
    }
}
static void clipFree(VM* vm, Obj* obj) {
    ObjClip* oClip = (ObjClip*)obj;
    if (oClip->clip) {
        clip_free(oClip->clip);
        oClip->clip = NULL;
    }
    oClip->timelineObj = NULL;
}
const ForeignClassMethods ClipMethods = {
    "clip",
    NULL,
    clipFree,
    clipMark
};
ObjClip* newClip(VM* vm, ObjString* path) {
    ObjClip* obj = (ObjClip*)newForeign(vm, sizeof(ObjClip), &ClipMethods);
    init_allocator(&obj->allocator, vm);
    obj->clip = clip_create_media(path ? path->chars : NULL);
    obj->clip->user_data = obj;
    obj->timelineObj = NULL;
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
    obj->timelineObj = NULL;
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
    ObjTimelineClip* oTc = (ObjTimelineClip*)obj;
    if (oTc->timelineObj) {
        markObject(vm, (Obj*)oTc->timelineObj);
    }
}
static void timelineClipFree(VM* vm, Obj* obj) {
    (void)vm;
    ObjTimelineClip* oTc = (ObjTimelineClip*)obj;
    oTc->clip_id = 0;
    oTc->timeline = NULL;
    oTc->timelineObj = NULL;
    oTc->allocator = NULL;
}
const ForeignClassMethods TimelineClipMethods = {
    "timeline_clip",
    NULL,
    timelineClipFree,
    timelineClipMark
};
ObjTimelineClip* newTimelineClip(VM* vm, TimelineClip* tc, ObjTimeline* timeline_obj, Timeline* timeline, Allocator* allocator) {
    ObjTimelineClip* obj = (ObjTimelineClip*)newForeign(vm, sizeof(ObjTimelineClip), &TimelineClipMethods);
    obj->clip_id = tc->id;
    obj->timeline = timeline;
    obj->timelineObj = timeline_obj;
    obj->allocator = allocator;
    return obj;
}

static void effectHandleMark(VM* vm, Obj* obj) {
    ObjEffectHandle* effect = (ObjEffectHandle*)obj;
    if (effect->timelineObj) {
        markObject(vm, (Obj*)effect->timelineObj);
    }
    if (effect->effect && effect->effect->processor && effect->effect->processor->mark) {
        effect->effect->processor->mark(vm, effect->effect->data);
    }
}

static void effectHandleFree(VM* vm, Obj* obj) {
    ObjEffectHandle* effect = (ObjEffectHandle*)obj;
    if (effect->owns_effect && effect->effect) {
        Allocator allocator;
        init_allocator(&allocator, vm);
        effect_instance_destroy(&allocator, effect->effect);
    }
    effect->clip_id = 0;
    effect->timeline = NULL;
    effect->timelineObj = NULL;
    effect->effect = NULL;
    effect->allocator = NULL;
    effect->owns_effect = false;
}

const ForeignClassMethods EffectHandleMethods = {
    "effect_handle",
    NULL,
    effectHandleFree,
    effectHandleMark
};

ObjEffectHandle* newEffectHandle(VM* vm, TimelineClip* tc, ObjTimeline* timeline_obj, Timeline* timeline, EffectInstance* effect, Allocator* allocator) {
    ObjEffectHandle* obj = (ObjEffectHandle*)newForeign(vm, sizeof(ObjEffectHandle), &EffectHandleMethods);
    obj->clip_id = tc->id;
    obj->timeline = timeline;
    obj->timelineObj = timeline_obj;
    obj->effect = effect;
    obj->allocator = allocator;
    obj->owns_effect = false;
    return obj;
}

ObjEffectHandle* newStandaloneEffectHandle(VM* vm, EffectInstance* effect) {
    ObjEffectHandle* obj = (ObjEffectHandle*)newForeign(vm, sizeof(ObjEffectHandle), &EffectHandleMethods);
    obj->clip_id = 0;
    obj->timeline = NULL;
    obj->timelineObj = NULL;
    obj->effect = effect;
    obj->allocator = NULL;
    obj->owns_effect = true;
    return obj;
}

static void animatedPropertyHandleMark(VM* vm, Obj* obj) {
    ObjAnimatedPropertyHandle* handle = (ObjAnimatedPropertyHandle*)obj;
    if (handle->owner_handle) {
        markObject(vm, handle->owner_handle);
    }
}

static void animatedPropertyHandleFree(VM* vm, Obj* obj) {
    (void)vm;
    ObjAnimatedPropertyHandle* handle = (ObjAnimatedPropertyHandle*)obj;
    handle->owner_handle = NULL;
    handle->source_kind = ANIMATED_PROPERTY_SOURCE_CLIP;
    handle->value_kind = ANIMATED_PROPERTY_VALUE_NUMBER;
    handle->key[0] = '\0';
}

const ForeignClassMethods AnimatedPropertyHandleMethods = {
    "animated_property_handle",
    NULL,
    animatedPropertyHandleFree,
    animatedPropertyHandleMark
};

static void textAnimatorHandleMark(VM* vm, Obj* obj) {
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)obj;
    if (handle->clip_obj) {
        markObject(vm, (Obj*)handle->clip_obj);
    }
}

static void textAnimatorHandleFree(VM* vm, Obj* obj) {
    (void)vm;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)obj;
    handle->clip_obj = NULL;
    handle->animator_index = 0;
}

const ForeignClassMethods TextAnimatorHandleMethods = {
    "text_animator_handle",
    NULL,
    textAnimatorHandleFree,
    textAnimatorHandleMark
};

static void textRangeSelectorHandleMark(VM* vm, Obj* obj) {
    ObjTextRangeSelectorHandle* handle = (ObjTextRangeSelectorHandle*)obj;
    if (handle->clip_obj) {
        markObject(vm, (Obj*)handle->clip_obj);
    }
}

static void textRangeSelectorHandleFree(VM* vm, Obj* obj) {
    (void)vm;
    ObjTextRangeSelectorHandle* handle = (ObjTextRangeSelectorHandle*)obj;
    handle->clip_obj = NULL;
    handle->animator_index = 0;
    handle->selector_index = 0;
}

const ForeignClassMethods TextRangeSelectorHandleMethods = {
    "text_range_selector_handle",
    NULL,
    textRangeSelectorHandleFree,
    textRangeSelectorHandleMark
};

static void textExpressionSelectorHandleMark(VM* vm, Obj* obj) {
    ObjTextExpressionSelectorHandle* handle = (ObjTextExpressionSelectorHandle*)obj;
    if (handle->clip_obj) {
        markObject(vm, (Obj*)handle->clip_obj);
    }
}

static void textExpressionSelectorHandleFree(VM* vm, Obj* obj) {
    (void)vm;
    ObjTextExpressionSelectorHandle* handle = (ObjTextExpressionSelectorHandle*)obj;
    handle->clip_obj = NULL;
    handle->animator_index = 0;
    handle->selector_index = 0;
}

const ForeignClassMethods TextExpressionSelectorHandleMethods = {
    "text_expression_selector_handle",
    NULL,
    textExpressionSelectorHandleFree,
    textExpressionSelectorHandleMark
};

static void textWigglySelectorHandleMark(VM* vm, Obj* obj) {
    ObjTextWigglySelectorHandle* handle = (ObjTextWigglySelectorHandle*)obj;
    if (handle->clip_obj) {
        markObject(vm, (Obj*)handle->clip_obj);
    }
}

static void textWigglySelectorHandleFree(VM* vm, Obj* obj) {
    (void)vm;
    ObjTextWigglySelectorHandle* handle = (ObjTextWigglySelectorHandle*)obj;
    handle->clip_obj = NULL;
    handle->animator_index = 0;
    handle->selector_index = 0;
}

const ForeignClassMethods TextWigglySelectorHandleMethods = {
    "text_wiggly_selector_handle",
    NULL,
    textWigglySelectorHandleFree,
    textWigglySelectorHandleMark
};

ObjAnimatedPropertyHandle* newAnimatedPropertyHandle(VM* vm, Obj* owner_handle,
                                                     AnimatedPropertySourceKind source_kind,
                                                     AnimatedPropertyValueKind value_kind,
                                                     const char* key) {
    ObjAnimatedPropertyHandle* obj =
        (ObjAnimatedPropertyHandle*)newForeign(vm, sizeof(ObjAnimatedPropertyHandle), &AnimatedPropertyHandleMethods);
    obj->owner_handle = owner_handle;
    obj->source_kind = source_kind;
    obj->value_kind = value_kind;
    if (key) {
        strncpy(obj->key, key, sizeof(obj->key) - 1);
        obj->key[sizeof(obj->key) - 1] = '\0';
    } else {
        obj->key[0] = '\0';
    }
    return obj;
}

ObjTextAnimatorHandle* newTextAnimatorHandle(VM* vm, ObjClip* clip_obj, u32 animator_index) {
    ObjTextAnimatorHandle* obj =
        (ObjTextAnimatorHandle*)newForeign(vm, sizeof(ObjTextAnimatorHandle), &TextAnimatorHandleMethods);
    obj->clip_obj = clip_obj;
    obj->animator_index = animator_index;
    return obj;
}

ObjTextRangeSelectorHandle* newTextRangeSelectorHandle(VM* vm, ObjClip* clip_obj, u32 animator_index, u32 selector_index) {
    ObjTextRangeSelectorHandle* obj =
        (ObjTextRangeSelectorHandle*)newForeign(vm, sizeof(ObjTextRangeSelectorHandle), &TextRangeSelectorHandleMethods);
    obj->clip_obj = clip_obj;
    obj->animator_index = animator_index;
    obj->selector_index = selector_index;
    return obj;
}

ObjTextExpressionSelectorHandle* newTextExpressionSelectorHandle(VM* vm, ObjClip* clip_obj,
                                                                 u32 animator_index, u32 selector_index) {
    ObjTextExpressionSelectorHandle* obj =
        (ObjTextExpressionSelectorHandle*)newForeign(vm, sizeof(ObjTextExpressionSelectorHandle),
                                                     &TextExpressionSelectorHandleMethods);
    obj->clip_obj = clip_obj;
    obj->animator_index = animator_index;
    obj->selector_index = selector_index;
    return obj;
}

ObjTextWigglySelectorHandle* newTextWigglySelectorHandle(VM* vm, ObjClip* clip_obj,
                                                         u32 animator_index, u32 selector_index) {
    ObjTextWigglySelectorHandle* obj =
        (ObjTextWigglySelectorHandle*)newForeign(vm, sizeof(ObjTextWigglySelectorHandle),
                                                 &TextWigglySelectorHandleMethods);
    obj->clip_obj = clip_obj;
    obj->animator_index = animator_index;
    obj->selector_index = selector_index;
    return obj;
}
