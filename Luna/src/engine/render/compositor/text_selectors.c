#include "internal.h"
static void set_dict_number_field(VM* vm, ObjDict* dict, const char* key_name, double value) {
    ObjString* key;
    if (!vm || !dict || !key_name) return;
    key = copyString(vm, key_name, (i32)strlen(key_name));
    push(vm, OBJ_VAL(key));
    tableSet(vm, &dict->items, OBJ_VAL(key), NUMBER_VAL(value));
    pop(vm);
}

static float evaluate_text_callback(VM* vm, Value callback, const TextExpressionContext* ctx, bool* out_ok) {
    ObjDict* dict;
    Value args[1];
    Value result = NIL_VAL;
    InterpretResult call_result;
    if (out_ok) *out_ok = false;
    if (!vm || !ctx || !IS_OBJ(callback)) return 0.0f;

    dict = newDict(vm);
    push(vm, OBJ_VAL(dict));
    set_dict_number_field(vm, dict, "index", ctx->index);
    set_dict_number_field(vm, dict, "count", ctx->count);
    set_dict_number_field(vm, dict, "time", ctx->time);
    set_dict_number_field(vm, dict, "position", ctx->position);
    set_dict_number_field(vm, dict, "charIndex", ctx->char_index);
    set_dict_number_field(vm, dict, "charCount", ctx->char_count);
    set_dict_number_field(vm, dict, "wordIndex", ctx->word_index);
    set_dict_number_field(vm, dict, "wordCount", ctx->word_count);
    set_dict_number_field(vm, dict, "lineIndex", ctx->line_index);
    set_dict_number_field(vm, dict, "lineCount", ctx->line_count);
    args[0] = OBJ_VAL(dict);
    call_result = vm_call_value(vm, callback, 1, args, &result);
    if (call_result != INTERPRET_OK) return 0.0f;
    if (IS_NUMBER(result)) {
        if (out_ok) *out_ok = true;
        return (float)AS_NUMBER(result);
    }
    if (IS_BOOL(result)) {
        if (out_ok) *out_ok = true;
        return AS_BOOL(result) ? 100.0f : 0.0f;
    }
    return 0.0f;
}

static float evaluate_range_selector_weight(const TextRangeSelector* selector, double time,
                                            const TextCharMeta* meta, int char_index, int char_count,
                                            int total_words, int total_lines) {
    float start;
    float end;
    float offset;
    float amount;
    float ease_high;
    float ease_low;
    float min_edge;
    float max_edge;
    float position;
    float normalized;
    float value;
    int unit_index;
    int unit_count;
    if (!selector || char_count <= 0) return 0.0f;
    if (!resolve_selector_unit(selector->based_on, meta, char_index, char_count, total_words, total_lines,
                               &unit_index, &unit_count)) return 0.0f;
    start = (float)evaluate_animation((Animation*)&selector->start, time);
    end = (float)evaluate_animation((Animation*)&selector->end, time);
    offset = (float)evaluate_animation((Animation*)&selector->offset, time);
    amount = (float)evaluate_animation((Animation*)&selector->amount, time) / 100.0f;
    ease_high = (float)evaluate_animation((Animation*)&selector->ease_high, time);
    ease_low = (float)evaluate_animation((Animation*)&selector->ease_low, time);
    start += offset;
    end += offset;
    min_edge = fminf(start, end);
    max_edge = fmaxf(start, end);
    position = (unit_count <= 1) ? 50.0f : ((float)unit_index * 100.0f) / (float)(unit_count - 1);
    if (fabsf(max_edge - min_edge) < 1e-6f) {
        normalized = position >= max_edge ? 1.0f : 0.0f;
    } else {
        normalized = (position - min_edge) / (max_edge - min_edge);
    }
    value = selector_shape_value(selector->shape, normalized);
    value = apply_selector_ease(value, ease_low, ease_high);
    return clamp01f(value * amount);
}

static float evaluate_expression_selector_weight(VM* vm, const TextExpressionSelector* selector, double time,
                                                 const TextCharMeta* meta, int char_index, int char_count,
                                                 int total_words, int total_lines) {
    TextExpressionContext ctx;
    bool ok = false;
    double raw_value;
    double amount;
    int unit_index;
    int unit_count;
    if (!selector || char_count <= 0) return 0.0f;
    if (!resolve_selector_unit(selector->based_on, meta, char_index, char_count, total_words, total_lines,
                               &unit_index, &unit_count)) return 0.0f;
    ctx.index = (double)unit_index;
    ctx.count = (double)unit_count;
    ctx.time = time;
    ctx.position = (unit_count <= 1) ? 50.0 : ((double)unit_index * 100.0) / (double)(unit_count - 1);
    ctx.char_index = (double)char_index;
    ctx.char_count = (double)char_count;
    ctx.word_index = meta ? (double)meta->word_index : -1.0;
    ctx.word_count = (double)total_words;
    ctx.line_index = meta ? (double)meta->line_index : 0.0;
    ctx.line_count = (double)total_lines;
    if (selector->has_callback) {
        raw_value = evaluate_text_callback(vm, selector->callback, &ctx, &ok);
    } else {
        raw_value = evaluate_text_expression(selector->expression, &ctx, &ok);
    }
    if (!ok) return 0.0f;
    if (fabs(raw_value) <= 1.0) raw_value *= 100.0;
    amount = evaluate_animation((Animation*)&selector->amount, time) / 100.0;
    return clamp01f((float)(clamp01f((float)(raw_value / 100.0)) * amount));
}

static float evaluate_wiggly_selector_weight(const TextWigglySelector* selector, double time,
                                             const TextCharMeta* meta, int char_index, int char_count,
                                             int total_words, int total_lines) {
    float amount;
    float wiggles_per_second;
    float correlation;
    float temporal_phase;
    float spatial_phase;
    float min_amount;
    float max_amount;
    float local_noise;
    float global_noise;
    float mixed_noise;
    int unit_index;
    int unit_count;
    if (!selector || char_count <= 0) return 0.0f;
    if (!resolve_selector_unit(selector->based_on, meta, char_index, char_count, total_words, total_lines,
                               &unit_index, &unit_count)) return 0.0f;
    amount = (float)evaluate_animation((Animation*)&selector->amount, time) / 100.0f;
    wiggles_per_second = (float)evaluate_animation((Animation*)&selector->wiggles_per_second, time);
    correlation = clamp01f((float)evaluate_animation((Animation*)&selector->correlation, time) / 100.0f);
    temporal_phase = (float)evaluate_animation((Animation*)&selector->temporal_phase, time);
    spatial_phase = (float)evaluate_animation((Animation*)&selector->spatial_phase, time);
    min_amount = clamp01f((float)evaluate_animation((Animation*)&selector->min_amount, time) / 100.0f);
    max_amount = clamp01f((float)evaluate_animation((Animation*)&selector->max_amount, time) / 100.0f);
    if (max_amount < min_amount) {
        float temp = min_amount;
        min_amount = max_amount;
        max_amount = temp;
    }
    global_noise = noise11f((float)(time * wiggles_per_second + temporal_phase * 0.1f));
    local_noise = noise11f((float)(time * wiggles_per_second + temporal_phase * 0.1f +
                                   unit_index * (0.37f + spatial_phase * 0.01f)));
    mixed_noise = mixf(local_noise, global_noise, correlation);
    return clamp01f((min_amount + (max_amount - min_amount) * mixed_noise) * amount);
}

static float combine_selector_weight(float current, float weight, TextSelectorMode mode, bool* has_value) {
    if (!has_value) return clamp01f(weight);
    if (!*has_value) {
        *has_value = true;
        return clamp01f(weight);
    }
    switch (mode) {
        case TEXT_SELECTOR_MODE_SUBTRACT:
            return clamp01f(current - weight);
        case TEXT_SELECTOR_MODE_INTERSECT:
            return clamp01f(current * weight);
        case TEXT_SELECTOR_MODE_MIN:
            return fminf(current, weight);
        case TEXT_SELECTOR_MODE_MAX:
            return fmaxf(current, weight);
        case TEXT_SELECTOR_MODE_ADD:
        default:
            return clamp01f(current + weight);
    }
}

static float evaluate_animator_weight(VM* vm, const TextAnimator* animator, double time, const TextCharMeta* meta,
                                      int char_index, int char_count, int total_words, int total_lines) {
    float total = 0.0f;
    bool has_value = false;
    if (!animator) return 0.0f;
    if (animator->range_selector_count == 0 &&
        animator->expression_selector_count == 0 &&
        animator->wiggly_selector_count == 0) return 1.0f;
    for (u32 i = 0; i < animator->range_selector_count; i++) {
        total = combine_selector_weight(total,
                                        evaluate_range_selector_weight(&animator->range_selectors[i], time, meta, char_index, char_count,
                                                                       total_words, total_lines),
                                        animator->range_selectors[i].mode, &has_value);
    }
    for (u32 i = 0; i < animator->expression_selector_count; i++) {
        total = combine_selector_weight(total,
                                        evaluate_expression_selector_weight(vm, &animator->expression_selectors[i], time, meta, char_index,
                                                                            char_count, total_words, total_lines),
                                        animator->expression_selectors[i].mode, &has_value);
    }
    for (u32 i = 0; i < animator->wiggly_selector_count; i++) {
        total = combine_selector_weight(total,
                                        evaluate_wiggly_selector_weight(&animator->wiggly_selectors[i], time, meta, char_index,
                                                                        char_count, total_words, total_lines),
                                        animator->wiggly_selectors[i].mode, &has_value);
    }
    return clamp01f(total);
}

void compute_text_char_style(const TimelineClip* tc, double anim_time, const TextCharMeta* meta,
                                    int total_words, int total_lines, int char_index, int char_count,
                                    TextCharStyle* out_style) {
    Clip* clip = tc->media;
    ObjClip* clip_obj = clip && clip->user_data ? (ObjClip*)clip->user_data : NULL;
    VM* vm = clip_obj ? (VM*)clip_obj->allocator.ctx : NULL;
    float base_fill_r = clip->text.color.r / 255.0f;
    float base_fill_g = clip->text.color.g / 255.0f;
    float base_fill_b = clip->text.color.b / 255.0f;
    float base_fill_a = clip->text.color.a / 255.0f;
    float base_stroke_r = clip->text.stroke_color.r / 255.0f;
    float base_stroke_g = clip->text.stroke_color.g / 255.0f;
    float base_stroke_b = clip->text.stroke_color.b / 255.0f;
    float base_stroke_a = clip->text.stroke_color.a / 255.0f;
    memset(out_style, 0, sizeof(*out_style));
    out_style->scale_x = 1.0f;
    out_style->scale_y = 1.0f;
    out_style->opacity_factor = 1.0f;
    out_style->fill_opacity = 1.0f;
    out_style->stroke_opacity = 1.0f;
    out_style->stroke_width = clip->text.stroke_width;
    out_style->fill_r = base_fill_r;
    out_style->fill_g = base_fill_g;
    out_style->fill_b = base_fill_b;
    out_style->fill_a = base_fill_a;
    out_style->stroke_r = base_stroke_r;
    out_style->stroke_g = base_stroke_g;
    out_style->stroke_b = base_stroke_b;
    out_style->stroke_a = base_stroke_a;

    for (u32 i = 0; i < clip->text.animator_count; i++) {
        const TextAnimator* animator = &clip->text.animators[i];
        float weight = evaluate_animator_weight(vm, animator, anim_time, meta, char_index, char_count,
                                                total_words, total_lines);
        float fill_mix;
        float stroke_mix;
        if (weight <= 0.0f) continue;
        out_style->offset_x += (float)evaluate_animation((Animation*)&animator->x, anim_time) * weight;
        out_style->offset_y += (float)evaluate_animation((Animation*)&animator->y, anim_time) * weight;
        out_style->scale_x *= fmaxf(0.01f, 1.0f + ((float)evaluate_animation((Animation*)&animator->scale_x, anim_time) / 100.0f) * weight);
        out_style->scale_y *= fmaxf(0.01f, 1.0f + ((float)evaluate_animation((Animation*)&animator->scale_y, anim_time) / 100.0f) * weight);
        out_style->rotation += (float)evaluate_animation((Animation*)&animator->rotation, anim_time) * weight;
        out_style->opacity_factor *= clamp01f(1.0f + ((float)evaluate_animation((Animation*)&animator->opacity, anim_time) / 100.0f) * weight);
        out_style->tracking += (float)evaluate_animation((Animation*)&animator->tracking, anim_time) * weight;
        out_style->anchor_x += (float)evaluate_animation((Animation*)&animator->anchor_x, anim_time) * weight;
        out_style->anchor_y += (float)evaluate_animation((Animation*)&animator->anchor_y, anim_time) * weight;
        out_style->skew += (float)evaluate_animation((Animation*)&animator->skew, anim_time) * weight;
        out_style->skew_axis += (float)evaluate_animation((Animation*)&animator->skew_axis, anim_time) * weight;
        out_style->fill_opacity *= clamp01f(1.0f + ((float)evaluate_animation((Animation*)&animator->fill_opacity, anim_time) / 100.0f) * weight);
        out_style->stroke_opacity *= clamp01f(1.0f + ((float)evaluate_animation((Animation*)&animator->stroke_opacity, anim_time) / 100.0f) * weight);
        out_style->stroke_width = fmaxf(0.0f, out_style->stroke_width +
            (float)evaluate_animation((Animation*)&animator->stroke_width, anim_time) * weight);
        out_style->character_offset += (float)evaluate_animation((Animation*)&animator->character_offset, anim_time) * weight;
        out_style->character_value += (float)evaluate_animation((Animation*)&animator->character_value, anim_time) * weight;

        fill_mix = clamp01f((float)evaluate_animation((Animation*)&animator->fill_color[3], anim_time) * weight);
        stroke_mix = clamp01f((float)evaluate_animation((Animation*)&animator->stroke_color[3], anim_time) * weight);

        if (fill_mix > 0.0f) {
            out_style->fill_r = mixf(out_style->fill_r, (float)evaluate_animation((Animation*)&animator->fill_color[0], anim_time), fill_mix);
            out_style->fill_g = mixf(out_style->fill_g, (float)evaluate_animation((Animation*)&animator->fill_color[1], anim_time), fill_mix);
            out_style->fill_b = mixf(out_style->fill_b, (float)evaluate_animation((Animation*)&animator->fill_color[2], anim_time), fill_mix);
        }
        if (stroke_mix > 0.0f) {
            out_style->stroke_r = mixf(out_style->stroke_r, (float)evaluate_animation((Animation*)&animator->stroke_color[0], anim_time), stroke_mix);
            out_style->stroke_g = mixf(out_style->stroke_g, (float)evaluate_animation((Animation*)&animator->stroke_color[1], anim_time), stroke_mix);
            out_style->stroke_b = mixf(out_style->stroke_b, (float)evaluate_animation((Animation*)&animator->stroke_color[2], anim_time), stroke_mix);
            out_style->stroke_a = mixf(out_style->stroke_a, 1.0f, stroke_mix);
        }
        apply_hsv_adjustment(&out_style->fill_r, &out_style->fill_g, &out_style->fill_b,
                             (float)evaluate_animation((Animation*)&animator->fill_hue, anim_time) * weight,
                             (float)evaluate_animation((Animation*)&animator->fill_saturation, anim_time) * weight,
                             (float)evaluate_animation((Animation*)&animator->fill_brightness, anim_time) * weight);
        apply_hsv_adjustment(&out_style->stroke_r, &out_style->stroke_g, &out_style->stroke_b,
                             (float)evaluate_animation((Animation*)&animator->stroke_hue, anim_time) * weight,
                             (float)evaluate_animation((Animation*)&animator->stroke_saturation, anim_time) * weight,
                             (float)evaluate_animation((Animation*)&animator->stroke_brightness, anim_time) * weight);
    }
}

