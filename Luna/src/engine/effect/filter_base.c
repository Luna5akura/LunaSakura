#include "engine/effect/filter_base.h"
#include "engine/model/timeline.h"
#include "engine/model/animation.h"
#include <string.h>

static bool effect_evaluate_number(EffectInstance* effect, const char* key, double time, double* out_value) {
    Animation* anim;
    bool bool_value;
    if (!effect || !out_value) return false;
    anim = effect->processor && effect->processor->get_number_animation
        ? effect->processor->get_number_animation(effect->data, key)
        : NULL;
    if (anim) {
        *out_value = evaluate_animation(anim, time);
        return true;
    }
    if (effect->processor && effect->processor->get_number &&
        effect->processor->get_number(effect->data, key, out_value)) {
        return true;
    }
    if (effect->processor && effect->processor->get_bool &&
        effect->processor->get_bool(effect->data, key, &bool_value)) {
        *out_value = bool_value ? 1.0 : 0.0;
        return true;
    }
    return false;
}

static bool effect_evaluate_color(EffectInstance* effect, const char* key, double time,
                                  double* out_r, double* out_g, double* out_b, double* out_a) {
    Animation *r, *g, *b, *a;
    if (!effect || !effect->processor || !effect->processor->get_color_animations) return false;
    if (!effect->processor->get_color_animations(effect->data, key, &r, &g, &b, &a)) return false;
    if (out_r) *out_r = evaluate_animation(r, time) * 255.0;
    if (out_g) *out_g = evaluate_animation(g, time) * 255.0;
    if (out_b) *out_b = evaluate_animation(b, time) * 255.0;
    if (out_a) *out_a = evaluate_animation(a, time) * 255.0;
    return true;
}

static void free_links(Allocator* a, EffectLink* link) {
    while (link) {
        EffectLink* next = link->next;
        MEM_FREE(a, EffectLink, link);
        link = next;
    }
}

static EffectLink* find_link(EffectInstance* target, const char* target_key, EffectLinkKind kind) {
    EffectLink* link = target ? target->links : NULL;
    while (link) {
        if (link->kind == kind && strcmp(link->target_key, target_key) == 0) return link;
        link = link->next;
    }
    return NULL;
}

EffectInstance* effect_instance_create(Allocator* a, const EffectProcessor* proc, Value* params, i32 param_count) {
    EffectInstance* effect;
    if (!a || !proc || !proc->create) return NULL;

    effect = MEM_ALLOC(a, EffectInstance, 1);
    if (!effect) return NULL;
    effect->name = proc->name;
    effect->processor = proc;
    effect->data = proc->create(a, params, param_count);
    effect->links = NULL;
    effect->next = NULL;
    if (!effect->data) {
        MEM_FREE(a, EffectInstance, effect);
        return NULL;
    }
    return effect;
}

bool effect_chain_append_existing(EffectInstance** chain, EffectInstance* effect) {
    EffectInstance* tail;
    if (!chain || !effect) return false;
    effect->next = NULL;

    if (!*chain) {
        *chain = effect;
        return true;
    }

    tail = *chain;
    while (tail->next) tail = tail->next;
    tail->next = effect;
    return true;
}

EffectInstance* effect_chain_append(Allocator* a, EffectInstance** chain, const EffectProcessor* proc, Value* params, i32 param_count) {
    EffectInstance* effect = effect_instance_create(a, proc, params, param_count);
    if (!effect) return NULL;
    if (!effect_chain_append_existing(chain, effect)) {
        effect_instance_destroy(a, effect);
        return NULL;
    }
    return effect;
}

i32 effect_chain_count(EffectInstance* chain) {
    i32 count = 0;
    while (chain) {
        count++;
        chain = chain->next;
    }
    return count;
}

EffectInstance* effect_chain_get(EffectInstance* chain, i32 index) {
    i32 i = 0;
    while (chain) {
        if (i == index) return chain;
        chain = chain->next;
        i++;
    }
    return NULL;
}

bool effect_chain_remove_at(Allocator* a, EffectInstance** chain, i32 index) {
    EffectInstance* current;
    EffectInstance* previous = NULL;
    i32 i = 0;

    if (!a || !chain || index < 0) return false;
    current = *chain;
    while (current) {
        if (i == index) {
            if (previous) previous->next = current->next;
            else *chain = current->next;
            effect_instance_destroy(a, current);
            return true;
        }
        previous = current;
        current = current->next;
        i++;
    }
    return false;
}

void effect_chain_clear(Allocator* a, EffectInstance** chain) {
    while (chain && *chain) {
        effect_chain_remove_at(a, chain, 0);
    }
}

void effect_instance_destroy(Allocator* a, EffectInstance* effect) {
    if (!a || !effect) return;
    if (effect->processor && effect->processor->destroy) {
        effect->processor->destroy(a, effect->data);
    }
    free_links(a, effect->links);
    MEM_FREE(a, EffectInstance, effect);
}

bool effect_link_number(Allocator* a, EffectInstance* target, const char* target_key,
                        u32 source_clip_id, EffectInstance* source_effect, const char* source_key,
                        double scale, double offset) {
    EffectLink* link;
    if (!a || !target || !target_key || !source_key || !source_effect) return false;
    link = find_link(target, target_key, EFFECT_LINK_NUMBER);
    if (!link) {
        link = MEM_ALLOC(a, EffectLink, 1);
        if (!link) return false;
        memset(link, 0, sizeof(EffectLink));
        link->kind = EFFECT_LINK_NUMBER;
        link->cached_track_index = -1;
        link->cached_clip_index = -1;
        strncpy(link->target_key, target_key, sizeof(link->target_key) - 1);
        link->next = target->links;
        target->links = link;
    }
    strncpy(link->source_key, source_key, sizeof(link->source_key) - 1);
    link->source_clip_id = source_clip_id;
    link->source_effect = source_effect;
    link->scale = scale;
    link->offset = offset;
    return true;
}

bool effect_link_color(Allocator* a, EffectInstance* target, const char* target_key,
                       u32 source_clip_id, EffectInstance* source_effect, const char* source_key) {
    EffectLink* link;
    if (!a || !target || !target_key || !source_key || !source_effect) return false;
    link = find_link(target, target_key, EFFECT_LINK_COLOR);
    if (!link) {
        link = MEM_ALLOC(a, EffectLink, 1);
        if (!link) return false;
        memset(link, 0, sizeof(EffectLink));
        link->kind = EFFECT_LINK_COLOR;
        link->cached_track_index = -1;
        link->cached_clip_index = -1;
        strncpy(link->target_key, target_key, sizeof(link->target_key) - 1);
        link->next = target->links;
        target->links = link;
    }
    strncpy(link->source_key, source_key, sizeof(link->source_key) - 1);
    link->source_clip_id = source_clip_id;
    link->source_effect = source_effect;
    link->scale = 1.0;
    link->offset = 0.0;
    return true;
}

bool effect_unlink(Allocator* a, EffectInstance* target, const char* target_key, EffectLinkKind kind) {
    EffectLink* current;
    EffectLink* previous = NULL;
    if (!a || !target || !target_key) return false;
    current = target->links;
    while (current) {
        if (current->kind == kind && strcmp(current->target_key, target_key) == 0) {
            if (previous) previous->next = current->next;
            else target->links = current->next;
            MEM_FREE(a, EffectLink, current);
            return true;
        }
        previous = current;
        current = current->next;
    }
    return false;
}

void effect_apply_links(Timeline* timeline, EffectInstance* target, double time) {
    EffectLink* link;
    if (!timeline || !target || !target->processor) return;
    link = target->links;
    while (link) {
        TimelineClip* clip;
        EffectInstance* source;
        clip = NULL;
        if (link->cached_track_index >= 0 && link->cached_clip_index >= 0) {
            clip = timeline_get_clip(timeline, link->cached_track_index, link->cached_clip_index);
            if (!clip || clip->id != link->source_clip_id) {
                clip = NULL;
            }
        }
        if (!clip) {
            clip = timeline_find_clip_by_id(timeline, link->source_clip_id, &link->cached_track_index, &link->cached_clip_index);
        }
        source = clip ? clip->effectChain : NULL;
        while (source && source != link->source_effect) source = source->next;
        if (!source) {
            link = link->next;
            continue;
        }
        if (link->kind == EFFECT_LINK_NUMBER && target->processor->set_number) {
            double value;
            if (effect_evaluate_number(source, link->source_key, time, &value)) {
                target->processor->set_number(target->data, link->target_key, value * link->scale + link->offset);
            }
        } else if (link->kind == EFFECT_LINK_COLOR && target->processor->set_color) {
            double r, g, b, a_value;
            if (effect_evaluate_color(source, link->source_key, time, &r, &g, &b, &a_value)) {
                target->processor->set_color(target->data, link->target_key, r, g, b, a_value);
            }
        }
        link = link->next;
    }
}
