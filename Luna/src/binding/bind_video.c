// src/binding/bind_video.c

#include <stdio.h>
#include <string.h>
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/engine.h"
#include "engine/effect/registry.h"   // 新增
#include "engine/media/utils/image_loader.h"
// --- 宏定义：简化操作 ---
// 获取当前实例 (this)，在 Native Init 中，this 位于 args[-1]
#define GET_SELF (AS_INSTANCE(args[-1]))

static EngineContext* get_engine_ctx(VM* vm) {
    return vm ? (EngineContext*)vm->user_data : NULL;
}

static ObjString* get_cached_prop_key(VM* vm, int key_index, const char* key_name) {
    EngineContext* ctx = get_engine_ctx(vm);
    ObjString* key;

    if (ctx && key_index >= 0 && key_index < ENGINE_BINDING_PROP_CACHE_COUNT && ctx->prop_cache[key_index]) {
        return ctx->prop_cache[key_index];
    }
    key = copyString(vm, key_name, (int)strlen(key_name));
    if (ctx && key_index >= 0 && key_index < ENGINE_BINDING_PROP_CACHE_COUNT) {
        ctx->prop_cache[key_index] = key;
    }
    return key;
}

static void set_number_prop_cached(VM* vm, ObjInstance* obj, int key_index, const char* key_name, double value) {
    ObjString* key = get_cached_prop_key(vm, key_index, key_name);
    tableSet(vm, &obj->fields, OBJ_VAL(key), NUMBER_VAL(value));
}

static void set_bool_prop_cached(VM* vm, ObjInstance* obj, int key_index, const char* key_name, bool value) {
    ObjString* key = get_cached_prop_key(vm, key_index, key_name);
    tableSet(vm, &obj->fields, OBJ_VAL(key), BOOL_VAL(value));
}

#define SET_PROP(obj, key_enum, key_name, val) \
    set_number_prop_cached(vm, obj, key_enum, key_name, val)
#define SET_BOOL_PROP(obj, key_enum, key_name, val) \
    set_bool_prop_cached(vm, obj, key_enum, key_name, val)

static Value effectInit(VM* vm, i32 argCount, Value* args);
static Value animatedPropertyInit(VM* vm, i32 argCount, Value* args);
static Value textAnimatorInit(VM* vm, i32 argCount, Value* args);
static Value rangeSelectorInit(VM* vm, i32 argCount, Value* args);
static Value expressionSelectorInit(VM* vm, i32 argCount, Value* args);
static Value wigglySelectorInit(VM* vm, i32 argCount, Value* args);
static void registerEffectMethods(VM* vm, ObjClass* klass);
static void registerAnimatedPropertyMethods(VM* vm, ObjClass* klass);
static void registerTextAnimatorMethods(VM* vm, ObjClass* klass);
static void registerRangeSelectorMethods(VM* vm, ObjClass* klass);
static void registerExpressionSelectorMethods(VM* vm, ObjClass* klass);
static void registerWigglySelectorMethods(VM* vm, ObjClass* klass);

static ObjString* get_cached_handle_key(VM* vm) {
    EngineContext* ctx = get_engine_ctx(vm);
    ObjString* handleKey;

    if (ctx && ctx->handle_key) return ctx->handle_key;
    handleKey = copyString(vm, "_handle", 7);
    if (ctx) ctx->handle_key = handleKey;
    return handleKey;
}

static ObjString* get_existing_handle_key(VM* vm) {
    EngineContext* ctx = get_engine_ctx(vm);
    return ctx ? ctx->handle_key : NULL;
}

static ObjClass* get_cached_global_class(VM* vm, ObjClass** slot, const char* class_name, int class_name_length) {
    ObjString* name;
    Value classVal;

    if (slot && *slot) return *slot;
    name = copyString(vm, class_name, class_name_length);
    push(vm, OBJ_VAL(name));
    if (!tableGet(&vm->globals, OBJ_VAL(name), &classVal) || !IS_CLASS(classVal)) {
        pop(vm);
        return NULL;
    }
    pop(vm);
    if (slot) *slot = AS_CLASS(classVal);
    return AS_CLASS(classVal);
}

static bool parse_keyframe_type(const char* type_str, KeyframeType* out) {
    if (strcmp(type_str, "hold") == 0) *out = KEYFRAME_HOLD;
    else if (strcmp(type_str, "linear") == 0) *out = KEYFRAME_LINEAR;
    else if (strcmp(type_str, "bezier") == 0) *out = KEYFRAME_BEZIER;
    else return false;
    return true;
}

typedef enum {
    EFFECT_PARAM_NUMBER,
    EFFECT_PARAM_BOOL,
    EFFECT_PARAM_COLOR
} EffectParamKind;

typedef struct {
    const char* name;
    EffectParamKind kind;
} EffectParamSpec;

typedef struct {
    const char* class_name;
    const EffectParamSpec* params;
    i32 param_count;
} EffectClassSpec;

static const EffectParamSpec TINT_EFFECT_PARAMS[] = {
    {"amount", EFFECT_PARAM_NUMBER},
    {"color", EFFECT_PARAM_COLOR}
};

static const EffectParamSpec FILL_EFFECT_PARAMS[] = {
    {"amount", EFFECT_PARAM_NUMBER},
    {"color", EFFECT_PARAM_COLOR}
};

static const EffectParamSpec BRIGHTNESS_CONTRAST_EFFECT_PARAMS[] = {
    {"brightness", EFFECT_PARAM_NUMBER},
    {"contrast", EFFECT_PARAM_NUMBER}
};

static const EffectParamSpec BLUR_EFFECT_PARAMS[] = {
    {"radius", EFFECT_PARAM_NUMBER}
};

static const EffectParamSpec GLOW_EFFECT_PARAMS[] = {
    {"radius", EFFECT_PARAM_NUMBER},
    {"intensity", EFFECT_PARAM_NUMBER},
    {"threshold", EFFECT_PARAM_NUMBER},
    {"softness", EFFECT_PARAM_NUMBER},
    {"color", EFFECT_PARAM_COLOR}
};

static const EffectParamSpec MOSAIC_EFFECT_PARAMS[] = {
    {"blockSize", EFFECT_PARAM_NUMBER},
    {"sharpColors", EFFECT_PARAM_BOOL}
};

static const EffectParamSpec GRID_EFFECT_PARAMS[] = {
    {"sizeX", EFFECT_PARAM_NUMBER},
    {"sizeY", EFFECT_PARAM_NUMBER},
    {"lineWidth", EFFECT_PARAM_NUMBER},
    {"opacity", EFFECT_PARAM_NUMBER},
    {"color", EFFECT_PARAM_COLOR}
};

static const EffectParamSpec GRADIENT_RAMP_EFFECT_PARAMS[] = {
    {"startX", EFFECT_PARAM_NUMBER},
    {"startY", EFFECT_PARAM_NUMBER},
    {"endX", EFFECT_PARAM_NUMBER},
    {"endY", EFFECT_PARAM_NUMBER},
    {"blend", EFFECT_PARAM_NUMBER},
    {"startColor", EFFECT_PARAM_COLOR},
    {"endColor", EFFECT_PARAM_COLOR}
};

static const EffectParamSpec FRACTAL_NOISE_EFFECT_PARAMS[] = {
    {"scale", EFFECT_PARAM_NUMBER},
    {"evolution", EFFECT_PARAM_NUMBER},
    {"contrast", EFFECT_PARAM_NUMBER},
    {"brightness", EFFECT_PARAM_NUMBER},
    {"octaves", EFFECT_PARAM_NUMBER},
    {"amount", EFFECT_PARAM_NUMBER},
    {"offsetX", EFFECT_PARAM_NUMBER},
    {"offsetY", EFFECT_PARAM_NUMBER},
    {"invert", EFFECT_PARAM_BOOL}
};

static const EffectParamSpec DISPLACEMENT_MAP_EFFECT_PARAMS[] = {
    {"scaleX", EFFECT_PARAM_NUMBER},
    {"scaleY", EFFECT_PARAM_NUMBER},
    {"amount", EFFECT_PARAM_NUMBER},
    {"offsetX", EFFECT_PARAM_NUMBER},
    {"offsetY", EFFECT_PARAM_NUMBER},
    {"horizontalChannel", EFFECT_PARAM_NUMBER},
    {"verticalChannel", EFFECT_PARAM_NUMBER},
    {"useLuma", EFFECT_PARAM_BOOL}
};

static const EffectParamSpec POSTERIZE_EFFECT_PARAMS[] = {
    {"levels", EFFECT_PARAM_NUMBER},
    {"amount", EFFECT_PARAM_NUMBER}
};

static const EffectParamSpec SLIDER_CONTROL_EFFECT_PARAMS[] = {
    {"value", EFFECT_PARAM_NUMBER}
};

static const EffectParamSpec ANGLE_CONTROL_EFFECT_PARAMS[] = {
    {"angle", EFFECT_PARAM_NUMBER}
};

static const EffectParamSpec CHECKBOX_CONTROL_EFFECT_PARAMS[] = {
    {"value", EFFECT_PARAM_BOOL}
};

static const EffectParamSpec POINT_CONTROL_EFFECT_PARAMS[] = {
    {"x", EFFECT_PARAM_NUMBER},
    {"y", EFFECT_PARAM_NUMBER}
};

static const EffectParamSpec COLOR_CONTROL_EFFECT_PARAMS[] = {
    {"color", EFFECT_PARAM_COLOR}
};

static const EffectClassSpec EFFECT_CLASS_SPECS[] = {
    {"Tint", TINT_EFFECT_PARAMS, (i32)(sizeof(TINT_EFFECT_PARAMS) / sizeof(TINT_EFFECT_PARAMS[0]))},
    {"Fill", FILL_EFFECT_PARAMS, (i32)(sizeof(FILL_EFFECT_PARAMS) / sizeof(FILL_EFFECT_PARAMS[0]))},
    {"BrightnessContrast", BRIGHTNESS_CONTRAST_EFFECT_PARAMS, (i32)(sizeof(BRIGHTNESS_CONTRAST_EFFECT_PARAMS) / sizeof(BRIGHTNESS_CONTRAST_EFFECT_PARAMS[0]))},
    {"Blur", BLUR_EFFECT_PARAMS, (i32)(sizeof(BLUR_EFFECT_PARAMS) / sizeof(BLUR_EFFECT_PARAMS[0]))},
    {"Glow", GLOW_EFFECT_PARAMS, (i32)(sizeof(GLOW_EFFECT_PARAMS) / sizeof(GLOW_EFFECT_PARAMS[0]))},
    {"Mosaic", MOSAIC_EFFECT_PARAMS, (i32)(sizeof(MOSAIC_EFFECT_PARAMS) / sizeof(MOSAIC_EFFECT_PARAMS[0]))},
    {"Grid", GRID_EFFECT_PARAMS, (i32)(sizeof(GRID_EFFECT_PARAMS) / sizeof(GRID_EFFECT_PARAMS[0]))},
    {"GradientRamp", GRADIENT_RAMP_EFFECT_PARAMS, (i32)(sizeof(GRADIENT_RAMP_EFFECT_PARAMS) / sizeof(GRADIENT_RAMP_EFFECT_PARAMS[0]))},
    {"FractalNoise", FRACTAL_NOISE_EFFECT_PARAMS, (i32)(sizeof(FRACTAL_NOISE_EFFECT_PARAMS) / sizeof(FRACTAL_NOISE_EFFECT_PARAMS[0]))},
    {"DisplacementMap", DISPLACEMENT_MAP_EFFECT_PARAMS, (i32)(sizeof(DISPLACEMENT_MAP_EFFECT_PARAMS) / sizeof(DISPLACEMENT_MAP_EFFECT_PARAMS[0]))},
    {"Posterize", POSTERIZE_EFFECT_PARAMS, (i32)(sizeof(POSTERIZE_EFFECT_PARAMS) / sizeof(POSTERIZE_EFFECT_PARAMS[0]))},
    {"SliderControl", SLIDER_CONTROL_EFFECT_PARAMS, (i32)(sizeof(SLIDER_CONTROL_EFFECT_PARAMS) / sizeof(SLIDER_CONTROL_EFFECT_PARAMS[0]))},
    {"AngleControl", ANGLE_CONTROL_EFFECT_PARAMS, (i32)(sizeof(ANGLE_CONTROL_EFFECT_PARAMS) / sizeof(ANGLE_CONTROL_EFFECT_PARAMS[0]))},
    {"CheckboxControl", CHECKBOX_CONTROL_EFFECT_PARAMS, (i32)(sizeof(CHECKBOX_CONTROL_EFFECT_PARAMS) / sizeof(CHECKBOX_CONTROL_EFFECT_PARAMS[0]))},
    {"PointControl", POINT_CONTROL_EFFECT_PARAMS, (i32)(sizeof(POINT_CONTROL_EFFECT_PARAMS) / sizeof(POINT_CONTROL_EFFECT_PARAMS[0]))},
    {"ColorControl", COLOR_CONTROL_EFFECT_PARAMS, (i32)(sizeof(COLOR_CONTROL_EFFECT_PARAMS) / sizeof(COLOR_CONTROL_EFFECT_PARAMS[0]))}
};

typedef struct {
    const char* name;
    AnimatedPropertyValueKind kind;
} TextAnimatorParamSpec;

typedef struct {
    const char* name;
} TextSelectorParamSpec;

static const TextAnimatorParamSpec TEXT_ANIMATOR_PARAM_SPECS[] = {
    {"x", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"y", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"scaleX", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"scaleY", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"rotation", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"opacity", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"tracking", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"strokeWidth", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"anchorX", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"anchorY", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"skew", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"skewAxis", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"fillOpacity", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"strokeOpacity", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"fillHue", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"fillSaturation", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"fillBrightness", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"strokeHue", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"strokeSaturation", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"strokeBrightness", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"characterOffset", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"characterValue", ANIMATED_PROPERTY_VALUE_NUMBER},
    {"fillColor", ANIMATED_PROPERTY_VALUE_COLOR},
    {"strokeColor", ANIMATED_PROPERTY_VALUE_COLOR}
};

static const TextSelectorParamSpec TEXT_SELECTOR_PARAM_SPECS[] = {
    {"start"},
    {"end"},
    {"offset"},
    {"amount"},
    {"easeHigh"},
    {"easeLow"}
};

static const TextSelectorParamSpec TEXT_EXPRESSION_SELECTOR_PARAM_SPECS[] = {
    {"amount"}
};

static const TextSelectorParamSpec TEXT_WIGGLY_SELECTOR_PARAM_SPECS[] = {
    {"amount"},
    {"wigglesPerSecond"},
    {"correlation"},
    {"temporalPhase"},
    {"spatialPhase"},
    {"minAmount"},
    {"maxAmount"}
};

static const EffectClassSpec* find_effect_class_spec(const char* class_name) {
    size_t i;
    for (i = 0; i < sizeof(EFFECT_CLASS_SPECS) / sizeof(EFFECT_CLASS_SPECS[0]); i++) {
        if (strcmp(EFFECT_CLASS_SPECS[i].class_name, class_name) == 0) return &EFFECT_CLASS_SPECS[i];
    }
    return NULL;
}

static const TextAnimatorParamSpec* find_text_animator_param_spec(const char* name) {
    for (size_t i = 0; i < sizeof(TEXT_ANIMATOR_PARAM_SPECS) / sizeof(TEXT_ANIMATOR_PARAM_SPECS[0]); i++) {
        if (strcmp(TEXT_ANIMATOR_PARAM_SPECS[i].name, name) == 0) return &TEXT_ANIMATOR_PARAM_SPECS[i];
    }
    return NULL;
}

static bool is_text_selector_param(const char* name) {
    for (size_t i = 0; i < sizeof(TEXT_SELECTOR_PARAM_SPECS) / sizeof(TEXT_SELECTOR_PARAM_SPECS[0]); i++) {
        if (strcmp(TEXT_SELECTOR_PARAM_SPECS[i].name, name) == 0) return true;
    }
    return false;
}

static bool is_text_expression_selector_param(const char* name) {
    for (size_t i = 0; i < sizeof(TEXT_EXPRESSION_SELECTOR_PARAM_SPECS) / sizeof(TEXT_EXPRESSION_SELECTOR_PARAM_SPECS[0]); i++) {
        if (strcmp(TEXT_EXPRESSION_SELECTOR_PARAM_SPECS[i].name, name) == 0) return true;
    }
    return false;
}

static bool is_text_wiggly_selector_param(const char* name) {
    for (size_t i = 0; i < sizeof(TEXT_WIGGLY_SELECTOR_PARAM_SPECS) / sizeof(TEXT_WIGGLY_SELECTOR_PARAM_SPECS[0]); i++) {
        if (strcmp(TEXT_WIGGLY_SELECTOR_PARAM_SPECS[i].name, name) == 0) return true;
    }
    return false;
}

static bool parse_text_selector_shape(const char* name, TextSelectorShape* out_shape) {
    if (!name || !out_shape) return false;
    if (strcmp(name, "square") == 0) *out_shape = TEXT_SELECTOR_SHAPE_SQUARE;
    else if (strcmp(name, "ramp_up") == 0) *out_shape = TEXT_SELECTOR_SHAPE_RAMP_UP;
    else if (strcmp(name, "ramp_down") == 0) *out_shape = TEXT_SELECTOR_SHAPE_RAMP_DOWN;
    else if (strcmp(name, "triangle") == 0) *out_shape = TEXT_SELECTOR_SHAPE_TRIANGLE;
    else if (strcmp(name, "smooth") == 0) *out_shape = TEXT_SELECTOR_SHAPE_SMOOTH;
    else return false;
    return true;
}

static bool parse_text_selector_mode(const char* name, TextSelectorMode* out_mode) {
    if (!name || !out_mode) return false;
    if (strcmp(name, "add") == 0) *out_mode = TEXT_SELECTOR_MODE_ADD;
    else if (strcmp(name, "subtract") == 0) *out_mode = TEXT_SELECTOR_MODE_SUBTRACT;
    else if (strcmp(name, "intersect") == 0) *out_mode = TEXT_SELECTOR_MODE_INTERSECT;
    else if (strcmp(name, "min") == 0) *out_mode = TEXT_SELECTOR_MODE_MIN;
    else if (strcmp(name, "max") == 0) *out_mode = TEXT_SELECTOR_MODE_MAX;
    else return false;
    return true;
}

static const char* text_selector_mode_name(TextSelectorMode mode) {
    switch (mode) {
        case TEXT_SELECTOR_MODE_SUBTRACT: return "subtract";
        case TEXT_SELECTOR_MODE_INTERSECT: return "intersect";
        case TEXT_SELECTOR_MODE_MIN: return "min";
        case TEXT_SELECTOR_MODE_MAX: return "max";
        case TEXT_SELECTOR_MODE_ADD:
        default: return "add";
    }
}

static const char* text_selector_shape_name(TextSelectorShape shape) {
    switch (shape) {
        case TEXT_SELECTOR_SHAPE_RAMP_UP: return "ramp_up";
        case TEXT_SELECTOR_SHAPE_RAMP_DOWN: return "ramp_down";
        case TEXT_SELECTOR_SHAPE_TRIANGLE: return "triangle";
        case TEXT_SELECTOR_SHAPE_SMOOTH: return "smooth";
        case TEXT_SELECTOR_SHAPE_SQUARE:
        default: return "square";
    }
}

static bool parse_text_selector_based_on(const char* name, TextSelectorBasedOn* out_based_on) {
    if (!name || !out_based_on) return false;
    if (strcmp(name, "characters") == 0) {
        *out_based_on = TEXT_SELECTOR_BASED_ON_CHARACTERS;
        return true;
    }
    if (strcmp(name, "words") == 0) {
        *out_based_on = TEXT_SELECTOR_BASED_ON_WORDS;
        return true;
    }
    if (strcmp(name, "lines") == 0) {
        *out_based_on = TEXT_SELECTOR_BASED_ON_LINES;
        return true;
    }
    return false;
}

static const char* text_selector_based_on_name(TextSelectorBasedOn based_on) {
    switch (based_on) {
        case TEXT_SELECTOR_BASED_ON_WORDS:
            return "words";
        case TEXT_SELECTOR_BASED_ON_LINES:
            return "lines";
        case TEXT_SELECTOR_BASED_ON_CHARACTERS:
        default: return "characters";
    }
}

static bool parse_effect_color_value(Value value, double* out_r, double* out_g, double* out_b, double* out_a) {
    ObjList* list;
    if (!IS_LIST(value)) return false;
    list = AS_LIST(value);
    if (list->count != 3 && list->count != 4) return false;
    for (u32 i = 0; i < list->count; i++) {
        if (!IS_NUMBER(list->items[i])) return false;
    }
    *out_r = AS_NUMBER(list->items[0]);
    *out_g = AS_NUMBER(list->items[1]);
    *out_b = AS_NUMBER(list->items[2]);
    *out_a = (list->count == 4) ? AS_NUMBER(list->items[3]) : 255.0;
    return true;
}

static bool apply_effect_constructor_value(EffectInstance* effect, const EffectParamSpec* spec, Value value) {
    double r, g, b, a;
    if (!effect || !spec || IS_UNDEFINED(value)) return true;
    switch (spec->kind) {
        case EFFECT_PARAM_NUMBER:
            if (!IS_NUMBER(value) || !effect->processor || !effect->processor->set_number) return false;
            return effect->processor->set_number(effect->data, spec->name, AS_NUMBER(value));
        case EFFECT_PARAM_BOOL:
            if (!IS_BOOL(value) || !effect->processor || !effect->processor->set_bool) return false;
            return effect->processor->set_bool(effect->data, spec->name, AS_BOOL(value));
        case EFFECT_PARAM_COLOR:
            if (!effect->processor || !effect->processor->set_color) return false;
            if (!parse_effect_color_value(value, &r, &g, &b, &a)) return false;
            return effect->processor->set_color(effect->data, spec->name, r, g, b, a);
    }
    return false;
}

static const EffectParamSpec* find_effect_param_spec(const EffectClassSpec* spec, const char* param_name) {
    if (!spec || !param_name) return NULL;
    for (i32 i = 0; i < spec->param_count; i++) {
        if (strcmp(spec->params[i].name, param_name) == 0) return &spec->params[i];
    }
    return NULL;
}

static Value make_effect_color_value(VM* vm, double r, double g, double b, double a) {
    ObjList* list = newList(vm);
    push(vm, OBJ_VAL(list));
    list->capacity = 4;
    list->count = 4;
    list->items = ALLOCATE(vm, Value, 4);
    list->items[0] = NUMBER_VAL(r);
    list->items[1] = NUMBER_VAL(g);
    list->items[2] = NUMBER_VAL(b);
    list->items[3] = NUMBER_VAL(a);
    pop(vm);
    return OBJ_VAL(list);
}

static bool get_effect_property_value(VM* vm, EffectInstance* effect, const EffectParamSpec* spec, Value* out_value) {
    double number_value;
    bool bool_value;
    Animation *r, *g, *b, *a;
    if (!vm || !effect || !spec || !out_value || !effect->processor) return false;
    switch (spec->kind) {
        case EFFECT_PARAM_NUMBER:
            if (!effect->processor->get_number ||
                !effect->processor->get_number(effect->data, spec->name, &number_value)) {
                return false;
            }
            *out_value = NUMBER_VAL(number_value);
            return true;
        case EFFECT_PARAM_BOOL:
            if (!effect->processor->get_bool ||
                !effect->processor->get_bool(effect->data, spec->name, &bool_value)) {
                return false;
            }
            *out_value = BOOL_VAL(bool_value);
            return true;
        case EFFECT_PARAM_COLOR:
            if (!effect->processor->get_color_animations ||
                !effect->processor->get_color_animations(effect->data, spec->name, &r, &g, &b, &a)) {
                return false;
            }
            *out_value = make_effect_color_value(vm,
                                                 evaluate_animation(r, 0.0) * 255.0,
                                                 evaluate_animation(g, 0.0) * 255.0,
                                                 evaluate_animation(b, 0.0) * 255.0,
                                                 evaluate_animation(a, 0.0) * 255.0);
            return true;
    }
    return false;
}

static bool sync_effect_property_field(VM* vm, ObjInstance* instance, EffectInstance* effect, const EffectParamSpec* spec) {
    ObjString* key;
    Value value;
    if (!vm || !instance || !effect || !spec) return false;
    if (!get_effect_property_value(vm, effect, spec, &value)) return false;
    key = copyString(vm, spec->name, (int)strlen(spec->name));
    tableSet(vm, &instance->fields, OBJ_VAL(key), value);
    return true;
}

static void populate_effect_instance_fields(VM* vm, ObjInstance* instance, EffectInstance* effect) {
    const char* class_name;
    const EffectClassSpec* spec;
    if (!vm || !instance || !instance->klass || !instance->klass->name || !effect) return;
    class_name = instance->klass->name->chars;
    spec = find_effect_class_spec(class_name);
    if (!spec) return;
    for (i32 i = 0; i < spec->param_count; i++) {
        sync_effect_property_field(vm, instance, effect, &spec->params[i]);
    }
}

static Allocator* resolve_effect_allocator(VM* vm, ObjEffectHandle* effectHandle, Allocator* temp_allocator) {
    if (effectHandle && effectHandle->allocator) return effectHandle->allocator;
    init_allocator(temp_allocator, vm);
    return temp_allocator;
}

static const char* keyframe_type_name(KeyframeType type) {
    switch (type) {
        case KEYFRAME_HOLD: return "hold";
        case KEYFRAME_LINEAR: return "linear";
        case KEYFRAME_BEZIER: return "bezier";
    }
    return "linear";
}

static bool parse_adjustment_blend_mode(const char* mode, u8* out_mode) {
    if (strcmp(mode, "normal") == 0) *out_mode = 0;
    else if (strcmp(mode, "add") == 0) *out_mode = 1;
    else if (strcmp(mode, "multiply") == 0) *out_mode = 2;
    else if (strcmp(mode, "screen") == 0) *out_mode = 3;
    else if (strcmp(mode, "overlay") == 0) *out_mode = 4;
    else return false;
    return true;
}

static const char* adjustment_blend_mode_name(u8 mode) {
    switch (mode) {
        case 1: return "add";
        case 2: return "multiply";
        case 3: return "screen";
        case 4: return "overlay";
        default: return "normal";
    }
}

static bool parse_adjustment_mask_mode(const char* mode, u8* out_mode) {
    if (strcmp(mode, "alpha") == 0) *out_mode = 0;
    else if (strcmp(mode, "luma") == 0) *out_mode = 1;
    else return false;
    return true;
}

static const char* adjustment_mask_mode_name(u8 mode) {
    return mode == 1 ? "luma" : "alpha";
}

static bool parse_position_mode(const char* mode, u8* out_mode) {
    if (!mode || !out_mode) return false;
    if (strcmp(mode, "position") == 0) *out_mode = TIMELINE_POSITION_MODE_POSITION;
    else if (strcmp(mode, "anchor") == 0) *out_mode = TIMELINE_POSITION_MODE_ANCHOR;
    else return false;
    return true;
}

static const char* position_mode_name(u8 mode) {
    return mode == TIMELINE_POSITION_MODE_ANCHOR ? "anchor" : "position";
}

static bool parse_timeline_anchor_point(const char* name, u8* out_anchor) {
    if (!name || !out_anchor) return false;
    if (strcmp(name, "top_left") == 0) *out_anchor = TIMELINE_ANCHOR_TOP_LEFT;
    else if (strcmp(name, "top_center") == 0) *out_anchor = TIMELINE_ANCHOR_TOP_CENTER;
    else if (strcmp(name, "top_right") == 0) *out_anchor = TIMELINE_ANCHOR_TOP_RIGHT;
    else if (strcmp(name, "center_left") == 0) *out_anchor = TIMELINE_ANCHOR_CENTER_LEFT;
    else if (strcmp(name, "center") == 0) *out_anchor = TIMELINE_ANCHOR_CENTER;
    else if (strcmp(name, "center_right") == 0) *out_anchor = TIMELINE_ANCHOR_CENTER_RIGHT;
    else if (strcmp(name, "bottom_left") == 0) *out_anchor = TIMELINE_ANCHOR_BOTTOM_LEFT;
    else if (strcmp(name, "bottom_center") == 0) *out_anchor = TIMELINE_ANCHOR_BOTTOM_CENTER;
    else if (strcmp(name, "bottom_right") == 0) *out_anchor = TIMELINE_ANCHOR_BOTTOM_RIGHT;
    else return false;
    return true;
}

static TimelineClip* resolve_timeline_clip(ObjTimelineClip* objTc, i32* out_track_index, i32* out_clip_index) {
    if (!objTc || !objTc->timeline || objTc->clip_id == 0) return NULL;
    return timeline_find_clip_by_id(objTc->timeline, objTc->clip_id, out_track_index, out_clip_index);
}

static Animation* resolve_clip_animation(ObjTimelineClip* objTc, const char* prop) {
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!clip) return NULL;
    if (strcmp(prop, "x") == 0) return &clip->anim.x;
    if (strcmp(prop, "y") == 0) return &clip->anim.y;
    if (strcmp(prop, "scale_x") == 0) return &clip->anim.scale_x;
    if (strcmp(prop, "scale_y") == 0) return &clip->anim.scale_y;
    if (strcmp(prop, "rotation") == 0) return &clip->anim.rotation;
    if (strcmp(prop, "opacity") == 0) return &clip->anim.opacity;
    if (strcmp(prop, "volume") == 0) return &clip->anim.volume;
    if (strcmp(prop, "font_size") == 0) return &clip->anim.font_size;
    return NULL;
}

static bool is_clip_animation_property(const char* prop) {
    return prop &&
           (strcmp(prop, "x") == 0 || strcmp(prop, "y") == 0 ||
            strcmp(prop, "scale_x") == 0 || strcmp(prop, "scale_y") == 0 ||
            strcmp(prop, "rotation") == 0 || strcmp(prop, "opacity") == 0 ||
            strcmp(prop, "volume") == 0 || strcmp(prop, "font_size") == 0);
}

static TextAnimator* resolve_text_animator_handle(ObjTextAnimatorHandle* handle) {
    if (!handle || !handle->clip_obj || !handle->clip_obj->clip) return NULL;
    return clip_text_get_animator(handle->clip_obj->clip, handle->animator_index);
}

static TextRangeSelector* resolve_text_range_selector_handle(ObjTextRangeSelectorHandle* handle) {
    TextAnimator* animator;
    if (!handle) return NULL;
    animator = resolve_text_animator_handle((ObjTextAnimatorHandle*)handle);
    return text_animator_get_range_selector(animator, handle->selector_index);
}

static TextExpressionSelector* resolve_text_expression_selector_handle(ObjTextExpressionSelectorHandle* handle) {
    TextAnimator* animator;
    if (!handle) return NULL;
    animator = resolve_text_animator_handle((ObjTextAnimatorHandle*)handle);
    return text_animator_get_expression_selector(animator, handle->selector_index);
}

static TextWigglySelector* resolve_text_wiggly_selector_handle(ObjTextWigglySelectorHandle* handle) {
    TextAnimator* animator;
    if (!handle) return NULL;
    animator = resolve_text_animator_handle((ObjTextAnimatorHandle*)handle);
    return text_animator_get_wiggly_selector(animator, handle->selector_index);
}

static Animation* resolve_text_animator_number_animation(TextAnimator* animator, const char* key) {
    if (!animator || !key) return NULL;
    if (strcmp(key, "x") == 0) return &animator->x;
    if (strcmp(key, "y") == 0) return &animator->y;
    if (strcmp(key, "scaleX") == 0) return &animator->scale_x;
    if (strcmp(key, "scaleY") == 0) return &animator->scale_y;
    if (strcmp(key, "rotation") == 0) return &animator->rotation;
    if (strcmp(key, "opacity") == 0) return &animator->opacity;
    if (strcmp(key, "tracking") == 0) return &animator->tracking;
    if (strcmp(key, "strokeWidth") == 0) return &animator->stroke_width;
    if (strcmp(key, "anchorX") == 0) return &animator->anchor_x;
    if (strcmp(key, "anchorY") == 0) return &animator->anchor_y;
    if (strcmp(key, "skew") == 0) return &animator->skew;
    if (strcmp(key, "skewAxis") == 0) return &animator->skew_axis;
    if (strcmp(key, "fillOpacity") == 0) return &animator->fill_opacity;
    if (strcmp(key, "strokeOpacity") == 0) return &animator->stroke_opacity;
    if (strcmp(key, "fillHue") == 0) return &animator->fill_hue;
    if (strcmp(key, "fillSaturation") == 0) return &animator->fill_saturation;
    if (strcmp(key, "fillBrightness") == 0) return &animator->fill_brightness;
    if (strcmp(key, "strokeHue") == 0) return &animator->stroke_hue;
    if (strcmp(key, "strokeSaturation") == 0) return &animator->stroke_saturation;
    if (strcmp(key, "strokeBrightness") == 0) return &animator->stroke_brightness;
    if (strcmp(key, "characterOffset") == 0) return &animator->character_offset;
    if (strcmp(key, "characterValue") == 0) return &animator->character_value;
    return NULL;
}

static bool resolve_text_animator_color_animations(TextAnimator* animator, const char* key,
                                                   Animation** out_r, Animation** out_g,
                                                   Animation** out_b, Animation** out_a) {
    if (!animator || !key) return false;
    if (strcmp(key, "fillColor") == 0) {
        if (out_r) *out_r = &animator->fill_color[0];
        if (out_g) *out_g = &animator->fill_color[1];
        if (out_b) *out_b = &animator->fill_color[2];
        if (out_a) *out_a = &animator->fill_color[3];
        return true;
    }
    if (strcmp(key, "strokeColor") == 0) {
        if (out_r) *out_r = &animator->stroke_color[0];
        if (out_g) *out_g = &animator->stroke_color[1];
        if (out_b) *out_b = &animator->stroke_color[2];
        if (out_a) *out_a = &animator->stroke_color[3];
        return true;
    }
    return false;
}

static Animation* resolve_text_selector_number_animation(TextRangeSelector* selector, const char* key) {
    if (!selector || !key) return NULL;
    if (strcmp(key, "start") == 0) return &selector->start;
    if (strcmp(key, "end") == 0) return &selector->end;
    if (strcmp(key, "offset") == 0) return &selector->offset;
    if (strcmp(key, "amount") == 0) return &selector->amount;
    if (strcmp(key, "easeHigh") == 0) return &selector->ease_high;
    if (strcmp(key, "easeLow") == 0) return &selector->ease_low;
    return NULL;
}

static Animation* resolve_text_expression_selector_number_animation(TextExpressionSelector* selector, const char* key) {
    if (!selector || !key) return NULL;
    if (strcmp(key, "amount") == 0) return &selector->amount;
    return NULL;
}

static Animation* resolve_text_wiggly_selector_number_animation(TextWigglySelector* selector, const char* key) {
    if (!selector || !key) return NULL;
    if (strcmp(key, "amount") == 0) return &selector->amount;
    if (strcmp(key, "wigglesPerSecond") == 0) return &selector->wiggles_per_second;
    if (strcmp(key, "correlation") == 0) return &selector->correlation;
    if (strcmp(key, "temporalPhase") == 0) return &selector->temporal_phase;
    if (strcmp(key, "spatialPhase") == 0) return &selector->spatial_phase;
    if (strcmp(key, "minAmount") == 0) return &selector->min_amount;
    if (strcmp(key, "maxAmount") == 0) return &selector->max_amount;
    return NULL;
}
static void sync_common_props(VM* vm, ObjInstance* obj, Clip* inner) {
    SET_PROP(obj, ENGINE_BINDING_PROP_DEFAULT_SCALE_X, "default_scale_x", inner->default_scale_x);
    SET_PROP(obj, ENGINE_BINDING_PROP_DEFAULT_SCALE_Y, "default_scale_y", inner->default_scale_y);
    SET_PROP(obj, ENGINE_BINDING_PROP_DEFAULT_X, "default_x", inner->default_x);
    SET_PROP(obj, ENGINE_BINDING_PROP_DEFAULT_Y, "default_y", inner->default_y);
    SET_PROP(obj, ENGINE_BINDING_PROP_DEFAULT_ROTATION, "default_rotation", inner->default_rotation);
    SET_PROP(obj, ENGINE_BINDING_PROP_DEFAULT_OPACITY, "default_opacity", inner->default_opacity);
    SET_PROP(obj, ENGINE_BINDING_PROP_VOLUME, "volume", inner->volume);
    SET_PROP(obj, ENGINE_BINDING_PROP_IN_POINT, "in_point", inner->in_point);
    SET_PROP(obj, ENGINE_BINDING_PROP_DURATION, "duration", inner->duration);
}
// 供 main.c 调用
Project* get_active_project(VM* vm) {
    EngineContext* ctx = get_engine_ctx(vm);
    return ctx ? ctx->active_project : NULL;
}
void reset_active_project(VM* vm) {
    EngineContext* ctx = get_engine_ctx(vm);
    if (ctx) {
        ctx->active_project = NULL;
        ctx->active_project_obj = NULL;
    }
}
// --- 内部辅助函数 ---
// 获取 Handle 并校验类型
static Obj* getHandle(VM* vm, Value instanceVal, const ForeignClassMethods* expectedMethods) {
    if (!IS_INSTANCE(instanceVal)) return NULL;
    ObjInstance* instance = AS_INSTANCE(instanceVal);
    ObjString* handleKey = get_existing_handle_key(vm);
    Value handleVal;
    if (!handleKey) return NULL;
    bool found = tableGet(&instance->fields, OBJ_VAL(handleKey), &handleVal);

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
    ObjString* handleKey = get_cached_handle_key(vm);
    Value val = OBJ_VAL(internalObj);
    tableSet(vm, &instance->fields, OBJ_VAL(handleKey), val);
}

static Value create_clip_instance_value(VM* vm, ObjTimeline* tlObj, TimelineClip* tc) {
    EngineContext* ctx;
    ObjTimelineClip* objTc;
    ObjClass* klass;
    ObjInstance* instance;

    objTc = newTimelineClip(vm, tc, tlObj, tlObj->timeline, &tlObj->timeline->allocator);
    ctx = get_engine_ctx(vm);
    klass = get_cached_global_class(vm, ctx ? &ctx->clip_instance_class : NULL, "ClipInstance", 12);
    if (!klass) {
        fprintf(stderr, "Runtime Error: ClipInstance class not found.\n");
        return NIL_VAL;
    }
    instance = newInstance(vm, klass);
    setHandle(vm, instance, (Obj*)objTc);
    return OBJ_VAL(instance);
}

static Value create_timeline_value(VM* vm, ObjTimeline* tlObj) {
    EngineContext* ctx;
    ObjClass* klass;
    ObjInstance* instance;

    if (!tlObj) return NIL_VAL;
    ctx = get_engine_ctx(vm);
    klass = get_cached_global_class(vm, ctx ? &ctx->timeline_class : NULL, "Timeline", 8);
    if (!klass) {
        fprintf(stderr, "Runtime Error: Timeline class not found.\n");
        return NIL_VAL;
    }
    instance = newInstance(vm, klass);
    setHandle(vm, instance, (Obj*)tlObj);
    SET_PROP(instance, ENGINE_BINDING_PROP_WIDTH, "width", tlObj->timeline ? tlObj->timeline->width : 0.0);
    SET_PROP(instance, ENGINE_BINDING_PROP_HEIGHT, "height", tlObj->timeline ? tlObj->timeline->height : 0.0);
    SET_PROP(instance, ENGINE_BINDING_PROP_FPS, "fps", tlObj->timeline ? tlObj->timeline->fps : 0.0);
    SET_PROP(instance, ENGINE_BINDING_PROP_DURATION, "duration", tlObj->timeline ? tlObj->timeline->duration : 0.0);
    return OBJ_VAL(instance);
}

static void sync_nested_clip_meta(VM* vm, ObjInstance* thisObj, ObjClip* objClip) {
    Timeline* nested;
    if (!objClip || !objClip->clip || !objClip->timelineObj || !objClip->timelineObj->timeline) return;
    nested = objClip->timelineObj->timeline;
    objClip->clip->nested_timeline.timeline = nested;
    objClip->clip->width = nested->width;
    objClip->clip->height = nested->height;
    objClip->clip->fps = nested->fps;
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", nested->width);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", nested->height);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", nested->fps);
}

static EffectInstance* resolve_effect_handle(ObjEffectHandle* effectHandle) {
    TimelineClip* clip;
    EffectInstance* effect;

    if (!effectHandle || !effectHandle->effect) return NULL;
    if (!effectHandle->timeline || effectHandle->clip_id == 0) {
        return effectHandle->effect;
    }
    clip = timeline_find_clip_by_id(effectHandle->timeline, effectHandle->clip_id, NULL, NULL);
    if (!clip) return NULL;
    effect = clip->effectChain;
    while (effect) {
        if (effect == effectHandle->effect) return effect;
        effect = effect->next;
    }
    return NULL;
}

static Animation* resolve_effect_number_animation(EffectInstance* effect, const char* key) {
    if (!effect || !effect->processor || !effect->processor->get_number_animation) return NULL;
    return effect->processor->get_number_animation(effect->data, key);
}

static bool resolve_effect_color_animations(EffectInstance* effect, const char* key,
                                            Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a) {
    if (!effect || !effect->processor || !effect->processor->get_color_animations) return false;
    return effect->processor->get_color_animations(effect->data, key, out_r, out_g, out_b, out_a);
}

static Value create_effect_value(VM* vm, ObjTimeline* tlObj, TimelineClip* tc, EffectInstance* effect) {
    ObjEffectHandle* effectHandle;
    ObjClass* klass;
    ObjInstance* instance;
    const char* class_name;

    effectHandle = newEffectHandle(vm, tc, tlObj, tlObj->timeline, effect, &tlObj->timeline->allocator);
    class_name = (effect && effect->processor && effect->processor->name) ? effect->processor->name : NULL;
    if (!class_name) {
        fprintf(stderr, "Runtime Error: Effect processor name missing.\n");
        return NIL_VAL;
    }
    klass = get_cached_global_class(vm, NULL, class_name, (int)strlen(class_name));
    if (!klass) {
        fprintf(stderr, "Runtime Error: Effect class '%s' not found.\n", class_name);
        return NIL_VAL;
    }
    instance = newInstance(vm, klass);
    push(vm, OBJ_VAL(instance));
    setHandle(vm, instance, (Obj*)effectHandle);
    populate_effect_instance_fields(vm, instance, effect);
    pop(vm);
    return OBJ_VAL(instance);
}

static Value create_animated_property_value(VM* vm, Obj* owner_handle,
                                            AnimatedPropertySourceKind source_kind,
                                            AnimatedPropertyValueKind value_kind,
                                            const char* key) {
    ObjAnimatedPropertyHandle* prop_handle;
    ObjClass* klass;
    ObjInstance* instance;

    if (!vm || !owner_handle || !key) return NIL_VAL;
    klass = get_cached_global_class(vm, NULL, "AnimatedProperty", 16);
    if (!klass) {
        fprintf(stderr, "Runtime Error: AnimatedProperty class not found.\n");
        return NIL_VAL;
    }
    prop_handle = newAnimatedPropertyHandle(vm, owner_handle, source_kind, value_kind, key);
    instance = newInstance(vm, klass);
    push(vm, OBJ_VAL(instance));
    setHandle(vm, instance, (Obj*)prop_handle);
    pop(vm);
    return OBJ_VAL(instance);
}

static Value create_text_animator_value(VM* vm, ObjClip* clip_obj, u32 animator_index) {
    ObjTextAnimatorHandle* handle;
    ObjClass* klass;
    ObjInstance* instance;

    if (!vm || !clip_obj) return NIL_VAL;
    klass = get_cached_global_class(vm, NULL, "TextAnimator", 12);
    if (!klass) {
        fprintf(stderr, "Runtime Error: TextAnimator class not found.\n");
        return NIL_VAL;
    }
    handle = newTextAnimatorHandle(vm, clip_obj, animator_index);
    instance = newInstance(vm, klass);
    push(vm, OBJ_VAL(instance));
    setHandle(vm, instance, (Obj*)handle);
    pop(vm);
    return OBJ_VAL(instance);
}

static Value create_range_selector_value(VM* vm, ObjClip* clip_obj, u32 animator_index, u32 selector_index) {
    ObjTextRangeSelectorHandle* handle;
    ObjClass* klass;
    ObjInstance* instance;

    if (!vm || !clip_obj) return NIL_VAL;
    klass = get_cached_global_class(vm, NULL, "RangeSelector", 13);
    if (!klass) {
        fprintf(stderr, "Runtime Error: RangeSelector class not found.\n");
        return NIL_VAL;
    }
    handle = newTextRangeSelectorHandle(vm, clip_obj, animator_index, selector_index);
    instance = newInstance(vm, klass);
    push(vm, OBJ_VAL(instance));
    setHandle(vm, instance, (Obj*)handle);
    pop(vm);
    return OBJ_VAL(instance);
}

static Value create_expression_selector_value(VM* vm, ObjClip* clip_obj, u32 animator_index, u32 selector_index) {
    ObjTextExpressionSelectorHandle* handle;
    ObjClass* klass;
    ObjInstance* instance;

    if (!vm || !clip_obj) return NIL_VAL;
    klass = get_cached_global_class(vm, NULL, "ExpressionSelector", 18);
    if (!klass) {
        fprintf(stderr, "Runtime Error: ExpressionSelector class not found.\n");
        return NIL_VAL;
    }
    handle = newTextExpressionSelectorHandle(vm, clip_obj, animator_index, selector_index);
    instance = newInstance(vm, klass);
    push(vm, OBJ_VAL(instance));
    setHandle(vm, instance, (Obj*)handle);
    pop(vm);
    return OBJ_VAL(instance);
}

static Value create_wiggly_selector_value(VM* vm, ObjClip* clip_obj, u32 animator_index, u32 selector_index) {
    ObjTextWigglySelectorHandle* handle;
    ObjClass* klass;
    ObjInstance* instance;

    if (!vm || !clip_obj) return NIL_VAL;
    klass = get_cached_global_class(vm, NULL, "WigglySelector", 14);
    if (!klass) {
        fprintf(stderr, "Runtime Error: WigglySelector class not found.\n");
        return NIL_VAL;
    }
    handle = newTextWigglySelectorHandle(vm, clip_obj, animator_index, selector_index);
    instance = newInstance(vm, klass);
    push(vm, OBJ_VAL(instance));
    setHandle(vm, instance, (Obj*)handle);
    pop(vm);
    return OBJ_VAL(instance);
}

static Animation* resolve_animated_property_number_animation(ObjAnimatedPropertyHandle* handle) {
    ObjTimelineClip* clip_handle;
    ObjEffectHandle* effect_handle;
    ObjTextAnimatorHandle* text_animator_handle;
    ObjTextRangeSelectorHandle* text_selector_handle;
    ObjTextExpressionSelectorHandle* text_expression_selector_handle;
    ObjTextWigglySelectorHandle* text_wiggly_selector_handle;
    EffectInstance* effect;
    TextAnimator* animator;
    TextRangeSelector* selector;
    TextExpressionSelector* expression_selector;
    TextWigglySelector* wiggly_selector;

    if (!handle || handle->value_kind != ANIMATED_PROPERTY_VALUE_NUMBER) return NULL;
    if (handle->source_kind == ANIMATED_PROPERTY_SOURCE_CLIP) {
        clip_handle = (ObjTimelineClip*)handle->owner_handle;
        return resolve_clip_animation(clip_handle, handle->key);
    }
    if (handle->source_kind == ANIMATED_PROPERTY_SOURCE_TEXT_ANIMATOR) {
        text_animator_handle = (ObjTextAnimatorHandle*)handle->owner_handle;
        animator = resolve_text_animator_handle(text_animator_handle);
        return resolve_text_animator_number_animation(animator, handle->key);
    }
    if (handle->source_kind == ANIMATED_PROPERTY_SOURCE_TEXT_RANGE_SELECTOR) {
        text_selector_handle = (ObjTextRangeSelectorHandle*)handle->owner_handle;
        selector = resolve_text_range_selector_handle(text_selector_handle);
        return resolve_text_selector_number_animation(selector, handle->key);
    }
    if (handle->source_kind == ANIMATED_PROPERTY_SOURCE_TEXT_EXPRESSION_SELECTOR) {
        text_expression_selector_handle = (ObjTextExpressionSelectorHandle*)handle->owner_handle;
        expression_selector = resolve_text_expression_selector_handle(text_expression_selector_handle);
        return resolve_text_expression_selector_number_animation(expression_selector, handle->key);
    }
    if (handle->source_kind == ANIMATED_PROPERTY_SOURCE_TEXT_WIGGLY_SELECTOR) {
        text_wiggly_selector_handle = (ObjTextWigglySelectorHandle*)handle->owner_handle;
        wiggly_selector = resolve_text_wiggly_selector_handle(text_wiggly_selector_handle);
        return resolve_text_wiggly_selector_number_animation(wiggly_selector, handle->key);
    }
    effect_handle = (ObjEffectHandle*)handle->owner_handle;
    effect = resolve_effect_handle(effect_handle);
    return resolve_effect_number_animation(effect, handle->key);
}

static bool resolve_animated_property_color_animations(ObjAnimatedPropertyHandle* handle,
                                                       Animation** out_r, Animation** out_g,
                                                       Animation** out_b, Animation** out_a) {
    ObjTextAnimatorHandle* text_animator_handle;
    ObjEffectHandle* effect_handle;
    TextAnimator* animator;
    EffectInstance* effect;
    if (!handle || handle->value_kind != ANIMATED_PROPERTY_VALUE_COLOR) return false;
    if (handle->source_kind == ANIMATED_PROPERTY_SOURCE_TEXT_ANIMATOR) {
        text_animator_handle = (ObjTextAnimatorHandle*)handle->owner_handle;
        animator = resolve_text_animator_handle(text_animator_handle);
        return resolve_text_animator_color_animations(animator, handle->key, out_r, out_g, out_b, out_a);
    }
    if (handle->source_kind != ANIMATED_PROPERTY_SOURCE_EFFECT) return false;
    effect_handle = (ObjEffectHandle*)handle->owner_handle;
    effect = resolve_effect_handle(effect_handle);
    return resolve_effect_color_animations(effect, handle->key, out_r, out_g, out_b, out_a);
}

static Allocator* resolve_animated_property_allocator(VM* vm, ObjAnimatedPropertyHandle* handle, Allocator* temp_allocator) {
    if (!handle) return NULL;
    switch (handle->source_kind) {
        case ANIMATED_PROPERTY_SOURCE_CLIP: {
            ObjTimelineClip* clip_handle = (ObjTimelineClip*)handle->owner_handle;
            return clip_handle ? clip_handle->allocator : NULL;
        }
        case ANIMATED_PROPERTY_SOURCE_EFFECT: {
            ObjEffectHandle* effect_handle = (ObjEffectHandle*)handle->owner_handle;
            return resolve_effect_allocator(vm, effect_handle, temp_allocator);
        }
        case ANIMATED_PROPERTY_SOURCE_TEXT_ANIMATOR: {
            ObjTextAnimatorHandle* text_animator_handle = (ObjTextAnimatorHandle*)handle->owner_handle;
            return (text_animator_handle && text_animator_handle->clip_obj)
                ? &text_animator_handle->clip_obj->allocator
                : NULL;
        }
        case ANIMATED_PROPERTY_SOURCE_TEXT_RANGE_SELECTOR: {
            ObjTextRangeSelectorHandle* selector_handle = (ObjTextRangeSelectorHandle*)handle->owner_handle;
            return (selector_handle && selector_handle->clip_obj)
                ? &selector_handle->clip_obj->allocator
                : NULL;
        }
        case ANIMATED_PROPERTY_SOURCE_TEXT_EXPRESSION_SELECTOR: {
            ObjTextExpressionSelectorHandle* selector_handle = (ObjTextExpressionSelectorHandle*)handle->owner_handle;
            return (selector_handle && selector_handle->clip_obj)
                ? &selector_handle->clip_obj->allocator
                : NULL;
        }
        case ANIMATED_PROPERTY_SOURCE_TEXT_WIGGLY_SELECTOR: {
            ObjTextWigglySelectorHandle* selector_handle = (ObjTextWigglySelectorHandle*)handle->owner_handle;
            return (selector_handle && selector_handle->clip_obj)
                ? &selector_handle->clip_obj->allocator
                : NULL;
        }
        default:
            break;
    }
    return NULL;
}

static bool parse_animated_property_number_value(Value value, double* out_value) {
    if (IS_NUMBER(value)) {
        *out_value = AS_NUMBER(value);
        return true;
    }
    if (IS_BOOL(value)) {
        *out_value = AS_BOOL(value) ? 1.0 : 0.0;
        return true;
    }
    return false;
}

static bool resolve_effect_property_value_kind(ObjInstance* instance, const char* key,
                                               AnimatedPropertyValueKind* out_kind) {
    const EffectClassSpec* class_spec;
    const EffectParamSpec* param_spec;
    if (!instance || !instance->klass || !instance->klass->name || !key || !out_kind) return false;
    class_spec = find_effect_class_spec(instance->klass->name->chars);
    if (!class_spec) return false;
    param_spec = find_effect_param_spec(class_spec, key);
    if (!param_spec) return false;
    *out_kind = (param_spec->kind == EFFECT_PARAM_COLOR)
        ? ANIMATED_PROPERTY_VALUE_COLOR
        : ANIMATED_PROPERTY_VALUE_NUMBER;
    return true;
}

bool resolveBoundInstancePropertyGet(VM* vm, ObjInstance* instance, ObjString* name, Value* out_value,
                                     bool* out_handled) {
    ObjTimelineClip* clip_handle;
    ObjEffectHandle* effect_handle;
    ObjTextAnimatorHandle* text_animator_handle;
    ObjTextRangeSelectorHandle* text_selector_handle;
    ObjTextExpressionSelectorHandle* text_expression_selector_handle;
    ObjTextWigglySelectorHandle* text_wiggly_selector_handle;
    AnimatedPropertyValueKind value_kind;
    const TextAnimatorParamSpec* text_animator_param_spec;

    if (out_handled) *out_handled = false;
    if (!vm || !instance || !name || !out_value) return true;

    clip_handle = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(instance), &TimelineClipMethods);
    if (clip_handle && is_clip_animation_property(name->chars)) {
        if (out_handled) *out_handled = true;
        push(vm, OBJ_VAL(instance));
        *out_value = create_animated_property_value(vm, (Obj*)clip_handle,
                                                    ANIMATED_PROPERTY_SOURCE_CLIP,
                                                    ANIMATED_PROPERTY_VALUE_NUMBER,
                                                    name->chars);
        pop(vm);
        return true;
    }

    text_animator_handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(instance), &TextAnimatorHandleMethods);
    text_animator_param_spec = text_animator_handle ? find_text_animator_param_spec(name->chars) : NULL;
    if (text_animator_handle && text_animator_param_spec) {
        if (out_handled) *out_handled = true;
        push(vm, OBJ_VAL(instance));
        *out_value = create_animated_property_value(vm, (Obj*)text_animator_handle,
                                                    ANIMATED_PROPERTY_SOURCE_TEXT_ANIMATOR,
                                                    text_animator_param_spec->kind,
                                                    name->chars);
        pop(vm);
        return true;
    }

    text_selector_handle = (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(instance), &TextRangeSelectorHandleMethods);
    if (text_selector_handle && is_text_selector_param(name->chars)) {
        if (out_handled) *out_handled = true;
        push(vm, OBJ_VAL(instance));
        *out_value = create_animated_property_value(vm, (Obj*)text_selector_handle,
                                                    ANIMATED_PROPERTY_SOURCE_TEXT_RANGE_SELECTOR,
                                                    ANIMATED_PROPERTY_VALUE_NUMBER,
                                                    name->chars);
        pop(vm);
        return true;
    }

    text_expression_selector_handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(instance), &TextExpressionSelectorHandleMethods);
    if (text_expression_selector_handle && is_text_expression_selector_param(name->chars)) {
        if (out_handled) *out_handled = true;
        push(vm, OBJ_VAL(instance));
        *out_value = create_animated_property_value(vm, (Obj*)text_expression_selector_handle,
                                                    ANIMATED_PROPERTY_SOURCE_TEXT_EXPRESSION_SELECTOR,
                                                    ANIMATED_PROPERTY_VALUE_NUMBER,
                                                    name->chars);
        pop(vm);
        return true;
    }

    text_wiggly_selector_handle =
        (ObjTextWigglySelectorHandle*)getHandle(vm, OBJ_VAL(instance), &TextWigglySelectorHandleMethods);
    if (text_wiggly_selector_handle && is_text_wiggly_selector_param(name->chars)) {
        if (out_handled) *out_handled = true;
        push(vm, OBJ_VAL(instance));
        *out_value = create_animated_property_value(vm, (Obj*)text_wiggly_selector_handle,
                                                    ANIMATED_PROPERTY_SOURCE_TEXT_WIGGLY_SELECTOR,
                                                    ANIMATED_PROPERTY_VALUE_NUMBER,
                                                    name->chars);
        pop(vm);
        return true;
    }

    effect_handle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(instance), &EffectHandleMethods);
    if (effect_handle && resolve_effect_property_value_kind(instance, name->chars, &value_kind)) {
        if (out_handled) *out_handled = true;
        push(vm, OBJ_VAL(instance));
        *out_value = create_animated_property_value(vm, (Obj*)effect_handle,
                                                    ANIMATED_PROPERTY_SOURCE_EFFECT,
                                                    value_kind,
                                                    name->chars);
        pop(vm);
        return true;
    }

    return true;
}

bool syncBoundInstancePropertySet(VM* vm, ObjInstance* instance, ObjString* name, Value value,
                                  bool* out_handled, const char** out_error) {
    const EffectClassSpec* class_spec;
    const EffectParamSpec* param_spec;
    const TextAnimatorParamSpec* text_animator_param_spec;
    ObjEffectHandle* effect_handle;
    ObjTextAnimatorHandle* text_animator_handle;
    ObjTextRangeSelectorHandle* text_selector_handle;
    ObjTextExpressionSelectorHandle* text_expression_selector_handle;
    ObjTextWigglySelectorHandle* text_wiggly_selector_handle;
    EffectInstance* effect;
    TextAnimator* animator;
    TextRangeSelector* selector;
    TextExpressionSelector* expression_selector;
    TextWigglySelector* wiggly_selector;
    double number_value;
    double r;
    double g;
    double b;
    double a;
    Animation *anim_r, *anim_g, *anim_b, *anim_a;

    if (out_handled) *out_handled = false;
    if (out_error) *out_error = NULL;
    if (!vm || !instance || !name || !instance->klass || !instance->klass->name) return true;

    text_animator_handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(instance), &TextAnimatorHandleMethods);
    text_animator_param_spec = text_animator_handle ? find_text_animator_param_spec(name->chars) : NULL;
    if (text_animator_handle && text_animator_param_spec) {
        if (out_handled) *out_handled = true;
        animator = resolve_text_animator_handle(text_animator_handle);
        if (!animator) {
            if (out_error) *out_error = "Text animator is unavailable.";
            return false;
        }
        if (text_animator_param_spec->kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
            Animation* anim = resolve_text_animator_number_animation(animator, name->chars);
            if (!anim || !parse_animated_property_number_value(value, &number_value)) {
                if (out_error) *out_error = "Invalid text animator property value.";
                return false;
            }
            anim->default_value = number_value;
        } else {
            if (!parse_effect_color_value(value, &r, &g, &b, &a) ||
                !resolve_text_animator_color_animations(animator, name->chars, &anim_r, &anim_g, &anim_b, &anim_a)) {
                if (out_error) *out_error = "Invalid text animator color value.";
                return false;
            }
            anim_r->default_value = r / 255.0;
            anim_g->default_value = g / 255.0;
            anim_b->default_value = b / 255.0;
            anim_a->default_value = a / 255.0;
        }
        return true;
    }

    text_selector_handle = (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(instance), &TextRangeSelectorHandleMethods);
    if (text_selector_handle && is_text_selector_param(name->chars)) {
        if (out_handled) *out_handled = true;
        selector = resolve_text_range_selector_handle(text_selector_handle);
        if (!selector) {
            if (out_error) *out_error = "Range selector is unavailable.";
            return false;
        }
        if (!parse_animated_property_number_value(value, &number_value)) {
            if (out_error) *out_error = "Invalid range selector property value.";
            return false;
        }
        {
            Animation* anim = resolve_text_selector_number_animation(selector, name->chars);
            if (!anim) {
                if (out_error) *out_error = "Unknown range selector property.";
                return false;
            }
            anim->default_value = number_value;
        }
        return true;
    }

    text_expression_selector_handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(instance), &TextExpressionSelectorHandleMethods);
    if (text_expression_selector_handle && is_text_expression_selector_param(name->chars)) {
        if (out_handled) *out_handled = true;
        expression_selector = resolve_text_expression_selector_handle(text_expression_selector_handle);
        if (!expression_selector) {
            if (out_error) *out_error = "Expression selector is unavailable.";
            return false;
        }
        if (!parse_animated_property_number_value(value, &number_value)) {
            if (out_error) *out_error = "Invalid expression selector property value.";
            return false;
        }
        {
            Animation* anim = resolve_text_expression_selector_number_animation(expression_selector, name->chars);
            if (!anim) {
                if (out_error) *out_error = "Unknown expression selector property.";
                return false;
            }
            anim->default_value = number_value;
        }
        return true;
    }

    text_wiggly_selector_handle =
        (ObjTextWigglySelectorHandle*)getHandle(vm, OBJ_VAL(instance), &TextWigglySelectorHandleMethods);
    if (text_wiggly_selector_handle && is_text_wiggly_selector_param(name->chars)) {
        if (out_handled) *out_handled = true;
        wiggly_selector = resolve_text_wiggly_selector_handle(text_wiggly_selector_handle);
        if (!wiggly_selector) {
            if (out_error) *out_error = "Wiggly selector is unavailable.";
            return false;
        }
        if (!parse_animated_property_number_value(value, &number_value)) {
            if (out_error) *out_error = "Invalid wiggly selector property value.";
            return false;
        }
        {
            Animation* anim = resolve_text_wiggly_selector_number_animation(wiggly_selector, name->chars);
            if (!anim) {
                if (out_error) *out_error = "Unknown wiggly selector property.";
                return false;
            }
            anim->default_value = number_value;
        }
        return true;
    }

    class_spec = find_effect_class_spec(instance->klass->name->chars);
    if (!class_spec) return true;

    if (out_handled) *out_handled = true;
    param_spec = find_effect_param_spec(class_spec, name->chars);
    if (!param_spec) {
        if (out_error) *out_error = "Unknown effect property.";
        return false;
    }

    effect_handle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(instance), &EffectHandleMethods);
    effect = resolve_effect_handle(effect_handle);
    if (!effect) {
        if (out_error) *out_error = "Effect instance is unavailable.";
        return false;
    }
    if (!apply_effect_constructor_value(effect, param_spec, value)) {
        if (out_error) *out_error = "Invalid effect property value.";
        return false;
    }
    if (!sync_effect_property_field(vm, instance, effect, param_spec)) {
        if (out_error) *out_error = "Failed to synchronize effect property.";
        return false;
    }
    return true;
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
    objClip->clip->user_data = objClip;
  
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
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", inner->width);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", inner->height);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", inner->fps);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_VIDEO, "has_video", 1);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_AUDIO, "has_audio", 1);
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
    objClip->clip = clip_create_text(content, "assets/fonts/arial.ttf", 32, 255, 255, 255);
    objClip->clip->user_data = objClip;
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
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", 0);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", 0);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", 0);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_VIDEO, "has_video", 0);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_AUDIO, "has_audio", 0);
    sync_common_props(vm, thisObj, inner);
    return OBJ_VAL(thisObj);
}
Value imageInit(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj;
    ObjString* path;
    int width = 0;
    int height = 0;
    ObjClip* objClip;
    Clip* inner;

    if (argCount != 1 || !IS_STRING(args[0])) {
        fprintf(stderr, "Usage: Image(path: String)\n");
        return NIL_VAL;
    }

    thisObj = GET_SELF;
    path = AS_STRING(args[0]);
    if (!image_probe_dimensions(path->chars, &width, &height)) {
        fprintf(stderr, "Runtime Error: Could not load image '%s'\n", path->chars);
        return OBJ_VAL(thisObj);
    }

    objClip = newClip(vm, path);
    if (objClip->clip) clip_free(objClip->clip);
    objClip->clip = clip_create_image(path->chars, (u32)width, (u32)height);
    objClip->clip->user_data = objClip;

    inner = objClip->clip;
    setHandle(vm, thisObj, (Obj*)objClip);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", inner->width);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", inner->height);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", inner->fps);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_VIDEO, "has_video", 1);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_AUDIO, "has_audio", 0);
    sync_common_props(vm, thisObj, inner);
    return OBJ_VAL(thisObj);
}
Value solidInit(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj;
    ObjClip* objClip;
    Clip* inner;
    u8 a = 255;

    if (argCount < 5 || argCount > 6) {
        fprintf(stderr, "Usage: Solid(width, height, r, g, b[, a])\n");
        return NIL_VAL;
    }

    thisObj = GET_SELF;
    if (argCount == 6) a = (u8)AS_NUMBER(args[5]);
    objClip = newClip(vm, NULL);
    if (objClip->clip) clip_free(objClip->clip);
    objClip->clip = clip_create_solid(
        (u32)AS_NUMBER(args[0]),
        (u32)AS_NUMBER(args[1]),
        (u8)AS_NUMBER(args[2]),
        (u8)AS_NUMBER(args[3]),
        (u8)AS_NUMBER(args[4]),
        a
    );
    objClip->clip->user_data = objClip;

    inner = objClip->clip;
    setHandle(vm, thisObj, (Obj*)objClip);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", inner->width);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", inner->height);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", inner->fps);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_VIDEO, "has_video", 1);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_AUDIO, "has_audio", 0);
    sync_common_props(vm, thisObj, inner);
    return OBJ_VAL(thisObj);
}
Value adjustmentInit(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj;
    ObjClip* objClip;
    Clip* inner;

    if (argCount != 2) {
        fprintf(stderr, "Usage: Adjustment(width, height)\n");
        return NIL_VAL;
    }

    thisObj = GET_SELF;
    objClip = newClip(vm, NULL);
    if (objClip->clip) clip_free(objClip->clip);
    objClip->clip = clip_create_adjustment(
        (u32)AS_NUMBER(args[0]),
        (u32)AS_NUMBER(args[1])
    );
    objClip->clip->user_data = objClip;

    inner = objClip->clip;
    setHandle(vm, thisObj, (Obj*)objClip);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", inner->width);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", inner->height);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", inner->fps);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_VIDEO, "has_video", 0);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_AUDIO, "has_audio", 0);
    SET_BOOL_PROP(thisObj, ENGINE_BINDING_PROP_AFFECTS_WHOLE_FRAME, "affects_whole_frame", inner->adjustment.affects_whole_frame);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FEATHER, "feather", inner->adjustment.feather);
    SET_BOOL_PROP(thisObj, ENGINE_BINDING_PROP_MASK_INVERT, "mask_invert", inner->adjustment.mask_invert);
    sync_common_props(vm, thisObj, inner);
    return OBJ_VAL(thisObj);
}

static Value init_nested_clip(VM* vm, Value* args, ClipType type) {
    ObjInstance* thisObj;
    ObjClip* objClip;
    Clip* inner;
    ObjTimeline* tlObj;

    if ((type != CLIP_TYPE_GROUP && type != CLIP_TYPE_PRECOMP) || !args) return NIL_VAL;
    thisObj = GET_SELF;
    objClip = newClip(vm, NULL);
    if (objClip->clip) clip_free(objClip->clip);
    objClip->clip = (type == CLIP_TYPE_GROUP)
        ? clip_create_group((u32)AS_NUMBER(args[0]), (u32)AS_NUMBER(args[1]), AS_NUMBER(args[2]))
        : clip_create_precomp((u32)AS_NUMBER(args[0]), (u32)AS_NUMBER(args[1]), AS_NUMBER(args[2]));
    objClip->clip->user_data = objClip;

    tlObj = newTimeline(vm, (u32)AS_NUMBER(args[0]), (u32)AS_NUMBER(args[1]), AS_NUMBER(args[2]));
    push(vm, OBJ_VAL(tlObj));
    objClip->timelineObj = tlObj;
    objClip->clip->nested_timeline.timeline = tlObj->timeline;

    inner = objClip->clip;
    setHandle(vm, thisObj, (Obj*)objClip);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", inner->width);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", inner->height);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", inner->fps);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_VIDEO, "has_video", 1);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HAS_AUDIO, "has_audio", 0);
    sync_common_props(vm, thisObj, inner);
    pop(vm);
    return OBJ_VAL(thisObj);
}

Value groupInit(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3) {
        fprintf(stderr, "Usage: Group(width, height, fps)\n");
        return NIL_VAL;
    }
    return init_nested_clip(vm, args, CLIP_TYPE_GROUP);
}

Value precompInit(VM* vm, i32 argCount, Value* args) {
    if (argCount != 3) {
        fprintf(stderr, "Usage: Precomp(width, height, fps)\n");
        return NIL_VAL;
    }
    return init_nested_clip(vm, args, CLIP_TYPE_PRECOMP);
}

Value nestedClipGetTimeline(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 0) return NIL_VAL;
    if (!objClip->clip || (objClip->clip->type != CLIP_TYPE_GROUP && objClip->clip->type != CLIP_TYPE_PRECOMP)) return NIL_VAL;
    return create_timeline_value(vm, objClip->timelineObj);
}

Value nestedClipSetTimeline(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    ObjTimeline* tlObj;
    if (!objClip || argCount != 1) return NIL_VAL;
    if (!objClip->clip || (objClip->clip->type != CLIP_TYPE_GROUP && objClip->clip->type != CLIP_TYPE_PRECOMP)) return NIL_VAL;
    tlObj = (ObjTimeline*)getHandle(vm, args[0], &TimelineMethods);
    if (!tlObj || !tlObj->timeline) return BOOL_VAL(false);
    objClip->timelineObj = tlObj;
    sync_nested_clip_meta(vm, thisObj, objClip);
    return BOOL_VAL(true);
}
Value adjustmentSetAffectsWholeFrame(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 1 || !IS_BOOL(args[0])) return NIL_VAL;
    objClip->clip->adjustment.affects_whole_frame = AS_BOOL(args[0]);
    SET_BOOL_PROP(thisObj, ENGINE_BINDING_PROP_AFFECTS_WHOLE_FRAME, "affects_whole_frame", objClip->clip->adjustment.affects_whole_frame);
    return BOOL_VAL(true);
}
Value adjustmentAffectsWholeFrame(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 0) return NIL_VAL;
    return BOOL_VAL(objClip->clip->adjustment.affects_whole_frame);
}
Value adjustmentSetFeather(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    double value;
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    value = AS_NUMBER(args[0]);
    if (value < 0.0) value = 0.0;
    objClip->clip->adjustment.feather = value;
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FEATHER, "feather", value);
    return BOOL_VAL(true);
}
Value adjustmentGetFeather(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 0) return NIL_VAL;
    return NUMBER_VAL(objClip->clip->adjustment.feather);
}
Value adjustmentSetBlendMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    u8 mode;
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    if (!parse_adjustment_blend_mode(AS_CSTRING(args[0]), &mode)) return BOOL_VAL(false);
    objClip->clip->adjustment.blend_mode = mode;
    return BOOL_VAL(true);
}
Value adjustmentGetBlendMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    const char* name;
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 0) return NIL_VAL;
    name = adjustment_blend_mode_name(objClip->clip->adjustment.blend_mode);
    return OBJ_VAL(copyString(vm, name, (i32)strlen(name)));
}
Value adjustmentSetMaskSource(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    ObjTimelineClip* srcHandle;
    TimelineClip* source_clip;
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 1) return NIL_VAL;
    if (IS_NIL(args[0])) {
        objClip->clip->adjustment.mask_source_clip_id = 0;
        return BOOL_VAL(true);
    }
    srcHandle = (ObjTimelineClip*)getHandle(vm, args[0], &TimelineClipMethods);
    if (!srcHandle) return BOOL_VAL(false);
    source_clip = resolve_timeline_clip(srcHandle, NULL, NULL);
    if (!source_clip) return BOOL_VAL(false);
    objClip->clip->adjustment.mask_source_clip_id = source_clip->id;
    return BOOL_VAL(true);
}
Value adjustmentGetMaskSourceClipId(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 0) return NIL_VAL;
    if (objClip->clip->adjustment.mask_source_clip_id == 0) return NIL_VAL;
    return NUMBER_VAL((double)objClip->clip->adjustment.mask_source_clip_id);
}
Value adjustmentSetMaskMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    u8 mode;
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    if (!parse_adjustment_mask_mode(AS_CSTRING(args[0]), &mode)) return BOOL_VAL(false);
    objClip->clip->adjustment.mask_mode = mode;
    return BOOL_VAL(true);
}
Value adjustmentGetMaskMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    const char* name;
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 0) return NIL_VAL;
    name = adjustment_mask_mode_name(objClip->clip->adjustment.mask_mode);
    return OBJ_VAL(copyString(vm, name, (i32)strlen(name)));
}
Value adjustmentSetMaskInvert(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 1 || !IS_BOOL(args[0])) return NIL_VAL;
    objClip->clip->adjustment.mask_invert = AS_BOOL(args[0]);
    SET_BOOL_PROP(thisObj, ENGINE_BINDING_PROP_MASK_INVERT, "mask_invert", objClip->clip->adjustment.mask_invert);
    return BOOL_VAL(true);
}
Value adjustmentMaskInverted(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || objClip->clip->type != CLIP_TYPE_ADJUSTMENT || argCount != 0) return NIL_VAL;
    return BOOL_VAL(objClip->clip->adjustment.mask_invert);
}
Value clipSetVolume(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1) return NIL_VAL;
  
    double val = AS_NUMBER(args[0]);
    if (val < 0.0) val = 0.0;
  
    // [修改] 写入内部结构
    objClip->clip->volume = val;
  
    SET_PROP(thisObj, ENGINE_BINDING_PROP_VOLUME, "volume", val);
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
    SET_PROP(thisObj, ENGINE_BINDING_PROP_IN_POINT, "in_point", start);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DURATION, "duration", duration);
  
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
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DEFAULT_SCALE_X, "default_scale_x", sx);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DEFAULT_SCALE_Y, "default_scale_y", sy);
    return OBJ_VAL(thisObj);
}
Value clipSetPos(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 2) return NIL_VAL;
  
    // [修改]
    objClip->clip->default_x = AS_NUMBER(args[0]);
    objClip->clip->default_y = AS_NUMBER(args[1]);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DEFAULT_X, "default_x", objClip->clip->default_x);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DEFAULT_Y, "default_y", objClip->clip->default_y);
    return OBJ_VAL(thisObj);
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
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DEFAULT_OPACITY, "default_opacity", val);
    return OBJ_VAL(thisObj);
}
Value clipSetRotation(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1) return NIL_VAL;
  
    double val = AS_NUMBER(args[0]);
  
    objClip->clip->default_rotation = val;
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DEFAULT_ROTATION, "default_rotation", val);
    return OBJ_VAL(thisObj);
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
    return OBJ_VAL(thisObj);
}
Value textSetSize(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
  
    if (objClip->clip->type == CLIP_TYPE_TEXT) {
        objClip->clip->text.font_size = (u32)AS_NUMBER(args[0]);
    }
    return OBJ_VAL(thisObj);
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
    return OBJ_VAL(thisObj);
}

Value textSetLetterSpacing(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || argCount != 1) return NIL_VAL;
    objClip->clip->text.letter_spacing = AS_NUMBER(args[0]);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_LETTER_SPACING, "letter_spacing", objClip->clip->text.letter_spacing);
    return OBJ_VAL(thisObj);
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

    SET_BOOL_PROP(thisObj, ENGINE_BINDING_PROP_STROKE_ENABLED, "stroke_enabled", objClip->clip->text.stroke_enabled);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_STROKE_WIDTH, "stroke_width", objClip->clip->text.stroke_width);
    return OBJ_VAL(thisObj);
}

Value textAddAnimator(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    TextAnimator* animator;
    if (!objClip || !objClip->clip || objClip->clip->type != CLIP_TYPE_TEXT || argCount != 0) return NIL_VAL;
    animator = clip_text_add_animator(objClip->clip);
    if (!animator) return NIL_VAL;
    return create_text_animator_value(vm, objClip, objClip->clip->text.animator_count - 1);
}

Value textGetAnimatorCount(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || !objClip->clip || objClip->clip->type != CLIP_TYPE_TEXT || argCount != 0) return NIL_VAL;
    return NUMBER_VAL((double)clip_text_get_animator_count(objClip->clip));
}

Value textGetAnimatorBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    u32 index;
    if (!objClip || !objClip->clip || objClip->clip->type != CLIP_TYPE_TEXT || argCount != 1 || !IS_NUMBER(args[0])) {
        return NIL_VAL;
    }
    index = (u32)AS_NUMBER(args[0]);
    if (!clip_text_get_animator(objClip->clip, index)) return NIL_VAL;
    return create_text_animator_value(vm, objClip, index);
}

static Value textAnimatorInit(VM* vm, i32 argCount, Value* args) {
    (void)vm;
    (void)argCount;
    (void)args;
    return NIL_VAL;
}

Value textAnimatorAddRangeSelector(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    if (!handle || argCount != 0) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator || !text_animator_add_range_selector(animator)) return NIL_VAL;
    return create_range_selector_value(vm, handle->clip_obj, handle->animator_index, animator->range_selector_count - 1);
}

Value textAnimatorGetRangeSelectorCount(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    if (!handle || argCount != 0) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator) return NIL_VAL;
    return NUMBER_VAL((double)text_animator_get_range_selector_count(animator));
}

Value textAnimatorGetRangeSelectorBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    u32 index;
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator) return NIL_VAL;
    index = (u32)AS_NUMBER(args[0]);
    if (!text_animator_get_range_selector(animator, index)) return NIL_VAL;
    return create_range_selector_value(vm, handle->clip_obj, handle->animator_index, index);
}

Value textAnimatorAddExpressionSelector(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    if (!handle || argCount != 0) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator || !text_animator_add_expression_selector(animator)) return NIL_VAL;
    return create_expression_selector_value(vm, handle->clip_obj, handle->animator_index,
                                            animator->expression_selector_count - 1);
}

Value textAnimatorGetExpressionSelectorCount(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    if (!handle || argCount != 0) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator) return NIL_VAL;
    return NUMBER_VAL((double)text_animator_get_expression_selector_count(animator));
}

Value textAnimatorGetExpressionSelectorBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    u32 index;
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator) return NIL_VAL;
    index = (u32)AS_NUMBER(args[0]);
    if (!text_animator_get_expression_selector(animator, index)) return NIL_VAL;
    return create_expression_selector_value(vm, handle->clip_obj, handle->animator_index, index);
}

Value textAnimatorAddWigglySelector(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    if (!handle || argCount != 0) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator || !text_animator_add_wiggly_selector(animator)) return NIL_VAL;
    return create_wiggly_selector_value(vm, handle->clip_obj, handle->animator_index,
                                        animator->wiggly_selector_count - 1);
}

Value textAnimatorGetWigglySelectorCount(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    if (!handle || argCount != 0) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator) return NIL_VAL;
    return NUMBER_VAL((double)text_animator_get_wiggly_selector_count(animator));
}

Value textAnimatorGetWigglySelectorBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextAnimatorHandle* handle = (ObjTextAnimatorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextAnimatorHandleMethods);
    TextAnimator* animator;
    u32 index;
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    animator = resolve_text_animator_handle(handle);
    if (!animator) return NIL_VAL;
    index = (u32)AS_NUMBER(args[0]);
    if (!text_animator_get_wiggly_selector(animator, index)) return NIL_VAL;
    return create_wiggly_selector_value(vm, handle->clip_obj, handle->animator_index, index);
}

static Value rangeSelectorInit(VM* vm, i32 argCount, Value* args) {
    (void)vm;
    (void)argCount;
    (void)args;
    return NIL_VAL;
}

Value rangeSelectorSetShape(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextRangeSelectorHandle* handle =
        (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextRangeSelectorHandleMethods);
    TextRangeSelector* selector;
    TextSelectorShape shape;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_range_selector_handle(handle);
    if (!selector || !parse_text_selector_shape(AS_CSTRING(args[0]), &shape)) return BOOL_VAL(false);
    selector->shape = shape;
    return OBJ_VAL(thisObj);
}

Value rangeSelectorGetShape(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextRangeSelectorHandle* handle =
        (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextRangeSelectorHandleMethods);
    TextRangeSelector* selector;
    const char* shape_name;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_range_selector_handle(handle);
    if (!selector) return NIL_VAL;
    shape_name = text_selector_shape_name(selector->shape);
    return OBJ_VAL(copyString(vm, shape_name, (i32)strlen(shape_name)));
}

Value rangeSelectorSetBasedOn(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextRangeSelectorHandle* handle =
        (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextRangeSelectorHandleMethods);
    TextRangeSelector* selector;
    TextSelectorBasedOn based_on;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_range_selector_handle(handle);
    if (!selector || !parse_text_selector_based_on(AS_CSTRING(args[0]), &based_on)) return BOOL_VAL(false);
    selector->based_on = based_on;
    return OBJ_VAL(thisObj);
}

Value rangeSelectorGetBasedOn(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextRangeSelectorHandle* handle =
        (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextRangeSelectorHandleMethods);
    TextRangeSelector* selector;
    const char* based_on_name;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_range_selector_handle(handle);
    if (!selector) return NIL_VAL;
    based_on_name = text_selector_based_on_name(selector->based_on);
    return OBJ_VAL(copyString(vm, based_on_name, (i32)strlen(based_on_name)));
}

Value rangeSelectorSetMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextRangeSelectorHandle* handle =
        (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextRangeSelectorHandleMethods);
    TextRangeSelector* selector;
    TextSelectorMode mode;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_range_selector_handle(handle);
    if (!selector || !parse_text_selector_mode(AS_CSTRING(args[0]), &mode)) return BOOL_VAL(false);
    selector->mode = mode;
    return OBJ_VAL(thisObj);
}

Value rangeSelectorGetMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextRangeSelectorHandle* handle =
        (ObjTextRangeSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextRangeSelectorHandleMethods);
    TextRangeSelector* selector;
    const char* mode_name;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_range_selector_handle(handle);
    if (!selector) return NIL_VAL;
    mode_name = text_selector_mode_name(selector->mode);
    return OBJ_VAL(copyString(vm, mode_name, (i32)strlen(mode_name)));
}

static Value expressionSelectorInit(VM* vm, i32 argCount, Value* args) {
    (void)vm;
    (void)argCount;
    (void)args;
    return NIL_VAL;
}

Value expressionSelectorSetExpression(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    char* new_expr;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector) return BOOL_VAL(false);
    new_expr = strdup(AS_CSTRING(args[0]));
    if (!new_expr) return BOOL_VAL(false);
    if (selector->expression) free(selector->expression);
    selector->expression = new_expr;
    return OBJ_VAL(thisObj);
}

Value expressionSelectorSetCallback(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    if (!handle || argCount != 1) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector) return BOOL_VAL(false);
    if (!IS_NIL(args[0]) && !IS_CLOSURE(args[0]) && !IS_BOUND_METHOD(args[0]) && !IS_NATIVE(args[0])) {
        return BOOL_VAL(false);
    }
    selector->callback = args[0];
    selector->has_callback = !IS_NIL(args[0]);
    return OBJ_VAL(thisObj);
}

Value expressionSelectorGetCallback(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    (void)vm;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector || !selector->has_callback) return NIL_VAL;
    return selector->callback;
}

Value expressionSelectorGetExpression(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector || !selector->expression) return NIL_VAL;
    return OBJ_VAL(copyString(vm, selector->expression, (i32)strlen(selector->expression)));
}

Value expressionSelectorSetBasedOn(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    TextSelectorBasedOn based_on;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector || !parse_text_selector_based_on(AS_CSTRING(args[0]), &based_on)) return BOOL_VAL(false);
    selector->based_on = based_on;
    return OBJ_VAL(thisObj);
}

Value expressionSelectorGetBasedOn(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    const char* based_on_name;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector) return NIL_VAL;
    based_on_name = text_selector_based_on_name(selector->based_on);
    return OBJ_VAL(copyString(vm, based_on_name, (i32)strlen(based_on_name)));
}

Value expressionSelectorSetMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    TextSelectorMode mode;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector || !parse_text_selector_mode(AS_CSTRING(args[0]), &mode)) return BOOL_VAL(false);
    selector->mode = mode;
    return OBJ_VAL(thisObj);
}

Value expressionSelectorGetMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextExpressionSelectorHandle* handle =
        (ObjTextExpressionSelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextExpressionSelectorHandleMethods);
    TextExpressionSelector* selector;
    const char* mode_name;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_expression_selector_handle(handle);
    if (!selector) return NIL_VAL;
    mode_name = text_selector_mode_name(selector->mode);
    return OBJ_VAL(copyString(vm, mode_name, (i32)strlen(mode_name)));
}

static Value wigglySelectorInit(VM* vm, i32 argCount, Value* args) {
    (void)vm;
    (void)argCount;
    (void)args;
    return NIL_VAL;
}

Value wigglySelectorSetBasedOn(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextWigglySelectorHandle* handle =
        (ObjTextWigglySelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextWigglySelectorHandleMethods);
    TextWigglySelector* selector;
    TextSelectorBasedOn based_on;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_wiggly_selector_handle(handle);
    if (!selector || !parse_text_selector_based_on(AS_CSTRING(args[0]), &based_on)) return BOOL_VAL(false);
    selector->based_on = based_on;
    return OBJ_VAL(thisObj);
}

Value wigglySelectorGetBasedOn(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextWigglySelectorHandle* handle =
        (ObjTextWigglySelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextWigglySelectorHandleMethods);
    TextWigglySelector* selector;
    const char* based_on_name;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_wiggly_selector_handle(handle);
    if (!selector) return NIL_VAL;
    based_on_name = text_selector_based_on_name(selector->based_on);
    return OBJ_VAL(copyString(vm, based_on_name, (i32)strlen(based_on_name)));
}

Value wigglySelectorSetMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextWigglySelectorHandle* handle =
        (ObjTextWigglySelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextWigglySelectorHandleMethods);
    TextWigglySelector* selector;
    TextSelectorMode mode;
    if (!handle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    selector = resolve_text_wiggly_selector_handle(handle);
    if (!selector || !parse_text_selector_mode(AS_CSTRING(args[0]), &mode)) return BOOL_VAL(false);
    selector->mode = mode;
    return OBJ_VAL(thisObj);
}

Value wigglySelectorGetMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTextWigglySelectorHandle* handle =
        (ObjTextWigglySelectorHandle*)getHandle(vm, OBJ_VAL(thisObj), &TextWigglySelectorHandleMethods);
    TextWigglySelector* selector;
    const char* mode_name;
    if (!handle || argCount != 0) return NIL_VAL;
    selector = resolve_text_wiggly_selector_handle(handle);
    if (!selector) return NIL_VAL;
    mode_name = text_selector_mode_name(selector->mode);
    return OBJ_VAL(copyString(vm, mode_name, (i32)strlen(mode_name)));
}

Value solidSetColor(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjClip* objClip = (ObjClip*)getHandle(vm, OBJ_VAL(thisObj), &ClipMethods);
    if (!objClip || objClip->clip->type != CLIP_TYPE_SOLID || argCount < 3 || argCount > 4) return NIL_VAL;
    objClip->clip->solid.color.r = (u8)AS_NUMBER(args[0]);
    objClip->clip->solid.color.g = (u8)AS_NUMBER(args[1]);
    objClip->clip->solid.color.b = (u8)AS_NUMBER(args[2]);
    objClip->clip->solid.color.a = (u8)((argCount == 4) ? AS_NUMBER(args[3]) : 255);
    return OBJ_VAL(thisObj);
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
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DURATION, "duration", 0);
  
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

    // 更新时长
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DURATION, "duration", tlObj->timeline->duration);
    return create_clip_instance_value(vm, tlObj, tc);
}
Value timelineAddTrack(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    i32 index;
    if (!tlObj || argCount != 0) return NIL_VAL;
    index = timeline_add_track(tlObj->timeline);
    return NUMBER_VAL((double)index);
}
Value timelineRemoveTrackBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    if (!tlObj || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    timeline_remove_track(tlObj->timeline, (i32)AS_NUMBER(args[0]));
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DURATION, "duration", tlObj->timeline->duration);
    return NIL_VAL;
}
Value timelineGetTrackCount(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    if (!tlObj || argCount != 0) return NIL_VAL;
    return NUMBER_VAL((double)tlObj->timeline->track_count);
}
Value timelineGetClipCountBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    if (!tlObj || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    return NUMBER_VAL((double)timeline_get_clip_count(tlObj->timeline, (i32)AS_NUMBER(args[0])));
}
Value timelineGetClipBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    TimelineClip* tc;
    if (!tlObj || argCount != 2 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    tc = timeline_get_clip(tlObj->timeline, (i32)AS_NUMBER(args[0]), (i32)AS_NUMBER(args[1]));
    if (!tc) return NIL_VAL;
    return create_clip_instance_value(vm, tlObj, tc);
}
Value timelineRemoveClipBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    bool removed = false;
    if (!tlObj || argCount != 2 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    if (timeline_get_clip(tlObj->timeline, (i32)AS_NUMBER(args[0]), (i32)AS_NUMBER(args[1]))) {
        timeline_remove_clip(tlObj->timeline, (i32)AS_NUMBER(args[0]), (i32)AS_NUMBER(args[1]));
        removed = true;
    }
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DURATION, "duration", tlObj->timeline->duration);
    return BOOL_VAL(removed);
}
Value timelineSetTrackName(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    Track* track;
    if (!tlObj || argCount != 2 || !IS_NUMBER(args[0]) || !IS_STRING(args[1])) return NIL_VAL;
    if ((i32)AS_NUMBER(args[0]) < 0 || (u32)AS_NUMBER(args[0]) >= tlObj->timeline->track_count) return NIL_VAL;
    track = &tlObj->timeline->tracks[(i32)AS_NUMBER(args[0])];
    snprintf(track->name, sizeof(track->name), "%s", AS_CSTRING(args[1]));
    return NIL_VAL;
}
Value timelineGetTrackName(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    Track* track;
    if (!tlObj || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if ((i32)AS_NUMBER(args[0]) < 0 || (u32)AS_NUMBER(args[0]) >= tlObj->timeline->track_count) return NIL_VAL;
    track = &tlObj->timeline->tracks[(i32)AS_NUMBER(args[0])];
    return OBJ_VAL(copyString(vm, track->name, (i32)strlen(track->name)));
}
Value timelineSetTrackVisible(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    Track* track;
    if (!tlObj || argCount != 2 || !IS_NUMBER(args[0]) || !IS_BOOL(args[1])) return NIL_VAL;
    if ((i32)AS_NUMBER(args[0]) < 0 || (u32)AS_NUMBER(args[0]) >= tlObj->timeline->track_count) return NIL_VAL;
    track = &tlObj->timeline->tracks[(i32)AS_NUMBER(args[0])];
    if (AS_BOOL(args[1])) track->flags |= 1;
    else track->flags &= (u8)~1;
    return NIL_VAL;
}
Value timelineIsTrackVisible(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    Track* track;
    if (!tlObj || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if ((i32)AS_NUMBER(args[0]) < 0 || (u32)AS_NUMBER(args[0]) >= tlObj->timeline->track_count) return NIL_VAL;
    track = &tlObj->timeline->tracks[(i32)AS_NUMBER(args[0])];
    return BOOL_VAL((track->flags & 1) != 0);
}
Value timelineSetTrackLocked(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    Track* track;
    if (!tlObj || argCount != 2 || !IS_NUMBER(args[0]) || !IS_BOOL(args[1])) return NIL_VAL;
    if ((i32)AS_NUMBER(args[0]) < 0 || (u32)AS_NUMBER(args[0]) >= tlObj->timeline->track_count) return NIL_VAL;
    track = &tlObj->timeline->tracks[(i32)AS_NUMBER(args[0])];
    if (AS_BOOL(args[1])) track->flags |= 2;
    else track->flags &= (u8)~2;
    return NIL_VAL;
}
Value timelineIsTrackLocked(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    Track* track;
    if (!tlObj || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if ((i32)AS_NUMBER(args[0]) < 0 || (u32)AS_NUMBER(args[0]) >= tlObj->timeline->track_count) return NIL_VAL;
    track = &tlObj->timeline->tracks[(i32)AS_NUMBER(args[0])];
    return BOOL_VAL((track->flags & 2) != 0);
}
Value timelineSetBackgroundColor(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    if (!tlObj || argCount < 3 || argCount > 4) return NIL_VAL;
    tlObj->timeline->background_color.r = (u8)AS_NUMBER(args[0]);
    tlObj->timeline->background_color.g = (u8)AS_NUMBER(args[1]);
    tlObj->timeline->background_color.b = (u8)AS_NUMBER(args[2]);
    tlObj->timeline->background_color.a = (u8)((argCount == 4) ? AS_NUMBER(args[3]) : 255);
    return NIL_VAL;
}
Value timelineGetDurationBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimeline* tlObj = (ObjTimeline*)getHandle(vm, OBJ_VAL(thisObj), &TimelineMethods);
    if (!tlObj || argCount != 0) return NIL_VAL;
    return NUMBER_VAL(tlObj->timeline->duration);
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
    SET_PROP(thisObj, ENGINE_BINDING_PROP_WIDTH, "width", w);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_HEIGHT, "height", h);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_FPS, "fps", fps);
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DURATION, "duration", 0);
 
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
    proj->timelineObj = tlObj;
    SET_PROP(thisObj, ENGINE_BINDING_PROP_DURATION, "duration", tlObj->timeline->duration);
  
    return OBJ_VAL(thisObj);
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
        ctx->active_project_obj = proj;
    }
    return OBJ_VAL(thisObj);
}
Value projectSetPreviewRange(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), &ProjectMethods);
    if (!proj || argCount != 2 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NIL_VAL;
    if (AS_NUMBER(args[1]) <= AS_NUMBER(args[0])) return NIL_VAL;
    proj->project->use_preview_range = true;
    proj->project->preview_start = AS_NUMBER(args[0]);
    proj->project->preview_end = AS_NUMBER(args[1]);
    return NIL_VAL;
}
Value projectClearPreviewRange(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), &ProjectMethods);
    if (!proj || argCount != 0) return NIL_VAL;
    proj->project->use_preview_range = false;
    proj->project->preview_start = 0.0;
    proj->project->preview_end = 0.0;
    return NIL_VAL;
}
Value projectSetBackgroundColor(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), &ProjectMethods);
    Timeline* timeline;
    if (!proj || argCount < 3 || argCount > 4) return NIL_VAL;
    timeline = proj->project->timeline;
    if (!timeline) {
        fprintf(stderr, "Runtime Error: Project has no timeline.\n");
        return NIL_VAL;
    }
    timeline->background_color.r = (u8)AS_NUMBER(args[0]);
    timeline->background_color.g = (u8)AS_NUMBER(args[1]);
    timeline->background_color.b = (u8)AS_NUMBER(args[2]);
    timeline->background_color.a = (u8)((argCount == 4) ? AS_NUMBER(args[3]) : 255);
    return NIL_VAL;
}
Value projectGetDurationBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjProject* proj = (ObjProject*)getHandle(vm, OBJ_VAL(thisObj), &ProjectMethods);
    if (!proj || argCount != 0) return NIL_VAL;
    if (!proj->project->timeline) return NUMBER_VAL(0);
    return NUMBER_VAL(proj->project->timeline->duration);
}
// --- 新增: ClipInstance 方法 ---
// ClipInstance 无 init (dummy，如果需要)
Value clipInstanceInit(VM* vm, i32 argCount, Value* args) {
    return NIL_VAL;  // 无需初始化，用户不直接 new
}
static Value effectInit(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    const char* class_name = thisObj->klass && thisObj->klass->name ? thisObj->klass->name->chars : NULL;
    const EffectClassSpec* spec = class_name ? find_effect_class_spec(class_name) : NULL;
    const EffectProcessor* proc;
    Allocator allocator;
    EffectInstance* effect;
    ObjEffectHandle* effectHandle;

    if (!spec || !class_name) {
        fprintf(stderr, "Runtime Error: Unsupported effect class.\n");
        return NIL_VAL;
    }
    proc = effect_registry_get(class_name);
    if (!proc) {
        fprintf(stderr, "Runtime Error: Unknown effect processor '%s'.\n", class_name);
        return NIL_VAL;
    }

    init_allocator(&allocator, vm);
    effect = effect_instance_create(&allocator, proc, NULL, 0);
    if (!effect) return NIL_VAL;

    for (i32 i = 0; i < argCount && i < spec->param_count; i++) {
        if (!apply_effect_constructor_value(effect, &spec->params[i], args[i])) {
            fprintf(stderr, "Runtime Error: Invalid constructor value for %s.%s.\n", class_name, spec->params[i].name);
            effect_instance_destroy(&allocator, effect);
            return NIL_VAL;
        }
    }

    effectHandle = newStandaloneEffectHandle(vm, effect);
    setHandle(vm, thisObj, (Obj*)effectHandle);
    populate_effect_instance_fields(vm, thisObj, effect);
    return OBJ_VAL(thisObj);
}

Value animatedPropertyInit(VM* vm, i32 argCount, Value* args) {
    (void)vm;
    (void)argCount;
    (void)args;
    return NIL_VAL;
}

static bool parse_batch_keyframe_entry(Value value, double* out_time, Value* out_payload,
                                       KeyframeType* out_type, double* out_weight) {
    ObjList* entry;
    if (!IS_LIST(value)) return false;
    entry = AS_LIST(value);
    if (entry->count < 2 || entry->count > 4) return false;
    if (!IS_NUMBER(entry->items[0])) return false;
    *out_time = AS_NUMBER(entry->items[0]);
    *out_payload = entry->items[1];
    *out_type = KEYFRAME_LINEAR;
    *out_weight = 0.0;
    if (entry->count >= 3) {
        if (!IS_STRING(entry->items[2]) || !parse_keyframe_type(AS_CSTRING(entry->items[2]), out_type)) return false;
    }
    if (entry->count == 4) {
        if (!IS_NUMBER(entry->items[3])) return false;
        *out_weight = AS_NUMBER(entry->items[3]);
    }
    return true;
}

static Value animatedPropertyKeyframesBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    ObjList* frames;
    if (!handle || argCount != 1 || !IS_LIST(args[0])) return NIL_VAL;
    frames = AS_LIST(args[0]);
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        Allocator temp_allocator;
        Allocator* allocator;
        if (!anim) return BOOL_VAL(false);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        clear_keyframes(anim);
        for (u32 i = 0; i < frames->count; i++) {
            double time, payload_num, weight;
            Value payload;
            KeyframeType type;
            if (!parse_batch_keyframe_entry(frames->items[i], &time, &payload, &type, &weight) ||
                !parse_animated_property_number_value(payload, &payload_num)) {
                return BOOL_VAL(false);
            }
            add_keyframe(anim, allocator, time, payload_num, type, weight);
        }
        return BOOL_VAL(true);
    } else {
        Animation *r, *g, *b, *a;
        Allocator temp_allocator;
        Allocator* allocator;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return BOOL_VAL(false);
        clear_keyframes(r);
        clear_keyframes(g);
        clear_keyframes(b);
        clear_keyframes(a);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        for (u32 i = 0; i < frames->count; i++) {
            double time, cr, cg, cb, ca, weight;
            Value payload;
            KeyframeType type;
            if (!parse_batch_keyframe_entry(frames->items[i], &time, &payload, &type, &weight) ||
                !parse_effect_color_value(payload, &cr, &cg, &cb, &ca)) {
                return BOOL_VAL(false);
            }
            add_keyframe(r, allocator, time, cr / 255.0, type, weight);
            add_keyframe(g, allocator, time, cg / 255.0, type, weight);
            add_keyframe(b, allocator, time, cb / 255.0, type, weight);
            add_keyframe(a, allocator, time, ca / 255.0, type, weight);
        }
        return BOOL_VAL(true);
    }
}

static Value animatedPropertyAddBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    KeyframeType type = KEYFRAME_LINEAR;
    double weight = 0.0;
    if (!handle || argCount < 2 || argCount > 4 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (argCount >= 3) {
        if (!IS_STRING(args[2]) || !parse_keyframe_type(AS_CSTRING(args[2]), &type)) return BOOL_VAL(false);
    }
    if (argCount == 4) {
        if (!IS_NUMBER(args[3])) return BOOL_VAL(false);
        weight = AS_NUMBER(args[3]);
    }
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        double payload;
        Animation* anim = resolve_animated_property_number_animation(handle);
        Allocator temp_allocator;
        Allocator* allocator;
        if (!anim || !parse_animated_property_number_value(args[1], &payload)) return BOOL_VAL(false);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        add_keyframe(anim, allocator, AS_NUMBER(args[0]), payload, type, weight);
        return BOOL_VAL(true);
    } else {
        double cr, cg, cb, ca;
        Animation *r, *g, *b, *a;
        Allocator temp_allocator;
        Allocator* allocator;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a) ||
            !parse_effect_color_value(args[1], &cr, &cg, &cb, &ca)) return BOOL_VAL(false);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        add_keyframe(r, allocator, AS_NUMBER(args[0]), cr / 255.0, type, weight);
        add_keyframe(g, allocator, AS_NUMBER(args[0]), cg / 255.0, type, weight);
        add_keyframe(b, allocator, AS_NUMBER(args[0]), cb / 255.0, type, weight);
        add_keyframe(a, allocator, AS_NUMBER(args[0]), ca / 255.0, type, weight);
        return BOOL_VAL(true);
    }
}

static Value animatedPropertySetBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    KeyframeType type = KEYFRAME_LINEAR;
    double weight = 0.0;
    if (!handle || argCount < 2 || argCount > 4 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (argCount >= 3) {
        if (!IS_STRING(args[2]) || !parse_keyframe_type(AS_CSTRING(args[2]), &type)) return BOOL_VAL(false);
    }
    if (argCount == 4) {
        if (!IS_NUMBER(args[3])) return BOOL_VAL(false);
        weight = AS_NUMBER(args[3]);
    }
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        double payload;
        Animation* anim = resolve_animated_property_number_animation(handle);
        Allocator temp_allocator;
        Allocator* allocator;
        if (!anim || !parse_animated_property_number_value(args[1], &payload)) return BOOL_VAL(false);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        set_keyframe(anim, allocator, AS_NUMBER(args[0]), payload, type, weight);
        return BOOL_VAL(true);
    } else {
        double cr, cg, cb, ca;
        Animation *r, *g, *b, *a;
        Allocator temp_allocator;
        Allocator* allocator;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a) ||
            !parse_effect_color_value(args[1], &cr, &cg, &cb, &ca)) return BOOL_VAL(false);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        set_keyframe(r, allocator, AS_NUMBER(args[0]), cr / 255.0, type, weight);
        set_keyframe(g, allocator, AS_NUMBER(args[0]), cg / 255.0, type, weight);
        set_keyframe(b, allocator, AS_NUMBER(args[0]), cb / 255.0, type, weight);
        set_keyframe(a, allocator, AS_NUMBER(args[0]), ca / 255.0, type, weight);
        return BOOL_VAL(true);
    }
}

static Value animatedPropertyWithPresetBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    Animation* anim;
    double payload;
    if (!handle || handle->value_kind != ANIMATED_PROPERTY_VALUE_NUMBER ||
        argCount != 3 || !IS_NUMBER(args[0]) || !IS_STRING(args[2])) {
        return NIL_VAL;
    }
    anim = resolve_animated_property_number_animation(handle);
    if (!anim || !parse_animated_property_number_value(args[1], &payload)) return BOOL_VAL(false);
    {
        Allocator temp_allocator;
        Allocator* allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        add_keyframe_with_preset(anim, allocator, AS_NUMBER(args[0]), payload, AS_CSTRING(args[2]));
    }
    return BOOL_VAL(true);
}

static Value animatedPropertyRemoveBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        return BOOL_VAL(anim ? remove_keyframe(anim, AS_NUMBER(args[0])) : false);
    } else {
        Animation *r, *g, *b, *a;
        bool removed = false;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return BOOL_VAL(false);
        removed = remove_keyframe(r, AS_NUMBER(args[0]));
        removed = remove_keyframe(g, AS_NUMBER(args[0])) || removed;
        removed = remove_keyframe(b, AS_NUMBER(args[0])) || removed;
        removed = remove_keyframe(a, AS_NUMBER(args[0])) || removed;
        return BOOL_VAL(removed);
    }
}

static Value animatedPropertyClearBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 0) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        if (!anim) return BOOL_VAL(false);
        clear_keyframes(anim);
        return BOOL_VAL(true);
    } else {
        Animation *r, *g, *b, *a;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return BOOL_VAL(false);
        clear_keyframes(r);
        clear_keyframes(g);
        clear_keyframes(b);
        clear_keyframes(a);
        return BOOL_VAL(true);
    }
}

static Value animatedPropertyCountBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 0) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        return anim ? NUMBER_VAL((double)get_keyframe_count(anim)) : NIL_VAL;
    } else {
        Animation *r, *g, *b, *a;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return NIL_VAL;
        return NUMBER_VAL((double)get_keyframe_count(r));
    }
}

static Value animatedPropertyTimeBinding(VM* vm, i32 argCount, Value* args) {
    const Keyframe* keyframe;
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        if (!anim) return NIL_VAL;
        keyframe = get_keyframe_at(anim, (uint32_t)AS_NUMBER(args[0]));
    } else {
        Animation *r, *g, *b, *a;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return NIL_VAL;
        keyframe = get_keyframe_at(r, (uint32_t)AS_NUMBER(args[0]));
    }
    return keyframe ? NUMBER_VAL(keyframe->time) : NIL_VAL;
}

static Value animatedPropertyValueBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        const Keyframe* keyframe = anim ? get_keyframe_at(anim, (uint32_t)AS_NUMBER(args[0])) : NULL;
        return keyframe ? NUMBER_VAL(keyframe->value) : NIL_VAL;
    } else {
        Animation *r, *g, *b, *a;
        const Keyframe *kr, *kg, *kb, *ka;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return NIL_VAL;
        kr = get_keyframe_at(r, (uint32_t)AS_NUMBER(args[0]));
        kg = get_keyframe_at(g, (uint32_t)AS_NUMBER(args[0]));
        kb = get_keyframe_at(b, (uint32_t)AS_NUMBER(args[0]));
        ka = get_keyframe_at(a, (uint32_t)AS_NUMBER(args[0]));
        if (!kr || !kg || !kb || !ka) return NIL_VAL;
        return make_effect_color_value(vm, kr->value * 255.0, kg->value * 255.0, kb->value * 255.0, ka->value * 255.0);
    }
}

static Value animatedPropertyTypeBinding(VM* vm, i32 argCount, Value* args) {
    const Keyframe* keyframe;
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        if (!anim) return NIL_VAL;
        keyframe = get_keyframe_at(anim, (uint32_t)AS_NUMBER(args[0]));
    } else {
        Animation *r, *g, *b, *a;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return NIL_VAL;
        keyframe = get_keyframe_at(r, (uint32_t)AS_NUMBER(args[0]));
    }
    return keyframe ? OBJ_VAL(copyString(vm, keyframe_type_name(keyframe->type),
                                         (i32)strlen(keyframe_type_name(keyframe->type)))) : NIL_VAL;
}

static Value animatedPropertyWeightBinding(VM* vm, i32 argCount, Value* args) {
    const Keyframe* keyframe;
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        if (!anim) return NIL_VAL;
        keyframe = get_keyframe_at(anim, (uint32_t)AS_NUMBER(args[0]));
    } else {
        Animation *r, *g, *b, *a;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return NIL_VAL;
        keyframe = get_keyframe_at(r, (uint32_t)AS_NUMBER(args[0]));
    }
    return keyframe ? NUMBER_VAL(keyframe->bezier_weight) : NIL_VAL;
}

static Value animatedPropertyShiftBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        if (!anim) return BOOL_VAL(false);
        shift_keyframe_times(anim, AS_NUMBER(args[0]));
    } else {
        Animation *r, *g, *b, *a;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return BOOL_VAL(false);
        shift_keyframe_times(r, AS_NUMBER(args[0]));
        shift_keyframe_times(g, AS_NUMBER(args[0]));
        shift_keyframe_times(b, AS_NUMBER(args[0]));
        shift_keyframe_times(a, AS_NUMBER(args[0]));
    }
    return BOOL_VAL(true);
}

static Value animatedPropertyScaleTimesBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    if (!handle || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* anim = resolve_animated_property_number_animation(handle);
        if (!anim) return BOOL_VAL(false);
        scale_keyframe_times(anim, AS_NUMBER(args[0]));
    } else {
        Animation *r, *g, *b, *a;
        if (!resolve_animated_property_color_animations(handle, &r, &g, &b, &a)) return BOOL_VAL(false);
        scale_keyframe_times(r, AS_NUMBER(args[0]));
        scale_keyframe_times(g, AS_NUMBER(args[0]));
        scale_keyframe_times(b, AS_NUMBER(args[0]));
        scale_keyframe_times(a, AS_NUMBER(args[0]));
    }
    return BOOL_VAL(true);
}

static Value animatedPropertyCopyFromBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjAnimatedPropertyHandle* handle =
        (ObjAnimatedPropertyHandle*)getHandle(vm, OBJ_VAL(thisObj), &AnimatedPropertyHandleMethods);
    ObjAnimatedPropertyHandle* src_handle;
    if (!handle || argCount != 1) return NIL_VAL;
    src_handle = (ObjAnimatedPropertyHandle*)getHandle(vm, args[0], &AnimatedPropertyHandleMethods);
    if (!src_handle || src_handle->value_kind != handle->value_kind) return BOOL_VAL(false);
    if (handle->value_kind == ANIMATED_PROPERTY_VALUE_NUMBER) {
        Animation* dst = resolve_animated_property_number_animation(handle);
        Animation* src = resolve_animated_property_number_animation(src_handle);
        Allocator temp_allocator;
        Allocator* allocator;
        if (!dst || !src) return BOOL_VAL(false);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        copy_keyframes(dst, allocator, src);
    } else {
        Animation *dr, *dg, *db, *da, *sr, *sg, *sb, *sa;
        Allocator temp_allocator;
        Allocator* allocator;
        if (!resolve_animated_property_color_animations(handle, &dr, &dg, &db, &da) ||
            !resolve_animated_property_color_animations(src_handle, &sr, &sg, &sb, &sa)) return BOOL_VAL(false);
        allocator = resolve_animated_property_allocator(vm, handle, &temp_allocator);
        if (!allocator) return BOOL_VAL(false);
        copy_keyframes(dr, allocator, sr);
        copy_keyframes(dg, allocator, sg);
        copy_keyframes(db, allocator, sb);
        copy_keyframes(da, allocator, sa);
    }
    return BOOL_VAL(true);
}
Value clipInstanceSetStart(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    i32 track_index;
    if (!objTc || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, &track_index, NULL);
    if (!clip) return NIL_VAL;
    if (!timeline_move_clip_by_id(objTc->timeline, objTc->clip_id, track_index, AS_NUMBER(args[0]))) {
        return NIL_VAL;
    }
    return OBJ_VAL(thisObj);
}
Value clipInstanceGetStart(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip) return NIL_VAL;
    return NUMBER_VAL(clip->timeline_start);
}
Value clipInstanceSetDuration(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    double duration;
    if (!objTc || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (!clip) return NIL_VAL;
    duration = AS_NUMBER(args[0]);
    if (duration < 0.0) duration = 0.0;
    clip->timeline_duration = duration;
    timeline_update_duration(objTc->timeline);
    return OBJ_VAL(thisObj);
}
Value clipInstanceGetDuration(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip) return NIL_VAL;
    return NUMBER_VAL(clip->timeline_duration);
}
Value clipInstanceSetInPoint(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    double in_point;
    if (!objTc || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (!clip) return NIL_VAL;
    in_point = AS_NUMBER(args[0]);
    if (in_point < 0.0) in_point = 0.0;
    clip->source_in = in_point;
    return OBJ_VAL(thisObj);
}
Value clipInstanceGetInPoint(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip) return NIL_VAL;
    return NUMBER_VAL(clip->source_in);
}
Value clipInstanceSetZIndex(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    if (!clip) return NIL_VAL;
    clip->transform.z_index = (i32)AS_NUMBER(args[0]);
    return OBJ_VAL(thisObj);
}
Value clipInstanceGetZIndex(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip) return NIL_VAL;
    return NUMBER_VAL((double)clip->transform.z_index);
}
Value clipInstanceSetVisible(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 1 || !IS_BOOL(args[0])) return NIL_VAL;
    if (!clip) return NIL_VAL;
    if (AS_BOOL(args[0])) clip->flags |= 1;
    else clip->flags &= (u8)~1;
    return OBJ_VAL(thisObj);
}
Value clipInstanceSetPositionMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    u8 mode;
    if (!objTc || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    if (!clip || !parse_position_mode(AS_CSTRING(args[0]), &mode)) return NIL_VAL;
    clip->position_mode = mode;
    return OBJ_VAL(thisObj);
}
Value clipInstanceGetPositionMode(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    const char* mode_name;
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip) return NIL_VAL;
    mode_name = position_mode_name(clip->position_mode);
    return OBJ_VAL(copyString(vm, mode_name, (i32)strlen(mode_name)));
}
Value clipInstanceAlignToBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    ObjTimelineClip* targetHandle;
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    TimelineClip* target;
    u8 self_anchor;
    u8 target_anchor;
    if (!objTc || argCount != 3 || !IS_STRING(args[1]) || !IS_STRING(args[2])) return NIL_VAL;
    if (!clip) return NIL_VAL;
    targetHandle = (ObjTimelineClip*)getHandle(vm, args[0], &TimelineClipMethods);
    if (!targetHandle) return NIL_VAL;
    target = resolve_timeline_clip(targetHandle, NULL, NULL);
    if (!target || target == clip) return NIL_VAL;
    if (!parse_timeline_anchor_point(AS_CSTRING(args[1]), &self_anchor) ||
        !parse_timeline_anchor_point(AS_CSTRING(args[2]), &target_anchor)) {
        return NIL_VAL;
    }
    clip->position_mode = TIMELINE_POSITION_MODE_ANCHOR;
    clip->alignment_target_type = TIMELINE_ALIGNMENT_TARGET_CLIP;
    clip->self_anchor = self_anchor;
    clip->target_anchor = target_anchor;
    clip->alignment_target_clip_id = target->id;
    return OBJ_VAL(thisObj);
}
Value clipInstanceAlignToCompositionBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    u8 self_anchor;
    u8 target_anchor;
    if (!objTc || argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) return NIL_VAL;
    if (!clip) return NIL_VAL;
    if (!parse_timeline_anchor_point(AS_CSTRING(args[0]), &self_anchor) ||
        !parse_timeline_anchor_point(AS_CSTRING(args[1]), &target_anchor)) {
        return NIL_VAL;
    }
    clip->position_mode = TIMELINE_POSITION_MODE_ANCHOR;
    clip->alignment_target_type = TIMELINE_ALIGNMENT_TARGET_COMPOSITION;
    clip->self_anchor = self_anchor;
    clip->target_anchor = target_anchor;
    clip->alignment_target_clip_id = 0;
    return OBJ_VAL(thisObj);
}
Value clipInstanceClearAlignmentTargetBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip) return NIL_VAL;
    clip->alignment_target_type = TIMELINE_ALIGNMENT_TARGET_COMPOSITION;
    clip->alignment_target_clip_id = 0;
    return OBJ_VAL(thisObj);
}
Value clipInstanceGetAlignmentTargetClipId(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip || clip->alignment_target_type != TIMELINE_ALIGNMENT_TARGET_CLIP ||
        clip->alignment_target_clip_id == 0) {
        return NIL_VAL;
    }
    return NUMBER_VAL((double)clip->alignment_target_clip_id);
}
Value clipInstanceIsVisible(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!objTc || argCount != 0) return NIL_VAL;
    if (!clip) return NIL_VAL;
    return BOOL_VAL((clip->flags & 1) != 0);
}
Value clipInstanceRemoveBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    if (!objTc || argCount != 0) return NIL_VAL;
    return BOOL_VAL(timeline_remove_clip_by_id(objTc->timeline, objTc->clip_id));
}
Value clipInstanceMoveToTrack(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    double new_start;
    if (!objTc || argCount < 1 || argCount > 2 || !IS_NUMBER(args[0])) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!clip) return NIL_VAL;
    new_start = (argCount == 2 && IS_NUMBER(args[1])) ? AS_NUMBER(args[1]) : clip->timeline_start;
    return BOOL_VAL(timeline_move_clip_by_id(objTc->timeline, objTc->clip_id, (i32)AS_NUMBER(args[0]), new_start));
}
Value clipInstanceDuplicate(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    u32 new_id;
    i32 track_index;
    i32 clip_index;
    if (!objTc || argCount < 1 || argCount > 2 || !IS_NUMBER(args[0])) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, &track_index, &clip_index);
    if (!clip) return NIL_VAL;
    new_id = timeline_duplicate_clip_by_id(
        objTc->timeline,
        objTc->clip_id,
        (i32)AS_NUMBER(args[0]),
        (argCount == 2 && IS_NUMBER(args[1])) ? AS_NUMBER(args[1]) : clip->timeline_start
    );
    if (new_id == 0) return NIL_VAL;
    clip = timeline_find_clip_by_id(objTc->timeline, new_id, NULL, NULL);
    if (!clip) return NIL_VAL;
    return create_clip_instance_value(vm, objTc->timelineObj, clip);
}
Value clipInstanceAddEffectBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    ObjEffectHandle* effectHandle;
    if (!objTc || argCount != 1) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!clip) return NIL_VAL;
    effectHandle = (ObjEffectHandle*)getHandle(vm, args[0], &EffectHandleMethods);
    if (!effectHandle || !effectHandle->effect) {
        fprintf(stderr, "Runtime Error: addEffect expects an effect instance.\n");
        return NIL_VAL;
    }
    if (!effectHandle->owns_effect || effectHandle->timeline || effectHandle->clip_id != 0) {
        fprintf(stderr, "Runtime Error: Effect '%s' is already attached to a ClipInstance.\n",
                effectHandle->effect->name ? effectHandle->effect->name : "<unknown>");
        return NIL_VAL;
    }
    if (!effect_chain_append_existing(&clip->effectChain, effectHandle->effect)) return NIL_VAL;
    effectHandle->clip_id = clip->id;
    effectHandle->timeline = objTc->timeline;
    effectHandle->timelineObj = objTc->timelineObj;
    effectHandle->allocator = objTc->allocator;
    effectHandle->owns_effect = false;
    return args[0];
}
Value clipInstanceGetEffectCountBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    if (!objTc || argCount != 0) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!clip) return NIL_VAL;
    return NUMBER_VAL((double)effect_chain_count(clip->effectChain));
}
Value clipInstanceGetEffectBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    EffectInstance* effect;
    if (!objTc || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!clip) return NIL_VAL;
    effect = effect_chain_get(clip->effectChain, (i32)AS_NUMBER(args[0]));
    if (!effect) return NIL_VAL;
    return create_effect_value(vm, objTc->timelineObj, clip, effect);
}
Value clipInstanceRemoveEffectBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    if (!objTc || argCount != 1 || !IS_NUMBER(args[0])) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!clip) return NIL_VAL;
    return BOOL_VAL(effect_chain_remove_at(objTc->allocator, &clip->effectChain, (i32)AS_NUMBER(args[0])));
}
Value clipInstanceClearEffectsBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjTimelineClip* objTc = (ObjTimelineClip*)getHandle(vm, OBJ_VAL(thisObj), &TimelineClipMethods);
    TimelineClip* clip;
    if (!objTc || argCount != 0) return NIL_VAL;
    clip = resolve_timeline_clip(objTc, NULL, NULL);
    if (!clip) return NIL_VAL;
    effect_chain_clear(objTc->allocator, &clip->effectChain);
    return NIL_VAL;
}
Value effectGetNameBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjEffectHandle* effectHandle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(thisObj), &EffectHandleMethods);
    EffectInstance* effect;
    if (!effectHandle || argCount != 0) return NIL_VAL;
    effect = resolve_effect_handle(effectHandle);
    if (!effect) return NIL_VAL;
    return OBJ_VAL(copyString(vm, effect->name, (i32)strlen(effect->name)));
}
Value effectSetSourceBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjEffectHandle* effectHandle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(thisObj), &EffectHandleMethods);
    EffectInstance* effect;
    ObjTimelineClip* srcHandle;
    TimelineClip* source_clip;
    u32 clip_id = 0;
    if (!effectHandle || argCount != 1) return NIL_VAL;
    effect = resolve_effect_handle(effectHandle);
    if (!effect || !effect->processor || !effect->processor->set_source_clip) return BOOL_VAL(false);
    if (!IS_NIL(args[0])) {
        srcHandle = (ObjTimelineClip*)getHandle(vm, args[0], &TimelineClipMethods);
        if (!srcHandle) return BOOL_VAL(false);
        source_clip = resolve_timeline_clip(srcHandle, NULL, NULL);
        if (!source_clip) return BOOL_VAL(false);
        clip_id = source_clip->id;
    }
    return BOOL_VAL(effect->processor->set_source_clip(effect->data, clip_id));
}
Value effectGetSourceClipIdBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjEffectHandle* effectHandle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(thisObj), &EffectHandleMethods);
    EffectInstance* effect;
    u32 clip_id;
    if (!effectHandle || argCount != 0) return NIL_VAL;
    effect = resolve_effect_handle(effectHandle);
    if (!effect || !effect->processor || !effect->processor->get_source_clip) return NIL_VAL;
    clip_id = effect->processor->get_source_clip(effect->data);
    if (clip_id == 0) return NIL_VAL;
    return NUMBER_VAL((double)clip_id);
}
Value effectLinkNumberBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjEffectHandle* effectHandle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(thisObj), &EffectHandleMethods);
    ObjEffectHandle* srcHandle;
    EffectInstance* effect;
    EffectInstance* srcEffect;
    Allocator temp_allocator;
    Allocator* allocator;
    double scale = 1.0;
    double offset = 0.0;
    if (!effectHandle || argCount < 3 || argCount > 5 || !IS_STRING(args[0]) || !IS_STRING(args[2])) return NIL_VAL;
    srcHandle = (ObjEffectHandle*)getHandle(vm, args[1], &EffectHandleMethods);
    if (!srcHandle) return BOOL_VAL(false);
    effect = resolve_effect_handle(effectHandle);
    srcEffect = resolve_effect_handle(srcHandle);
    if (!effect || !srcEffect || srcHandle->clip_id == 0) return BOOL_VAL(false);
    if (argCount >= 4 && IS_NUMBER(args[3])) scale = AS_NUMBER(args[3]);
    if (argCount == 5 && IS_NUMBER(args[4])) offset = AS_NUMBER(args[4]);
    allocator = resolve_effect_allocator(vm, effectHandle, &temp_allocator);
    return BOOL_VAL(effect_link_number(allocator, effect, AS_CSTRING(args[0]),
                                       srcHandle->clip_id, srcEffect, AS_CSTRING(args[2]), scale, offset));
}
Value effectLinkColorBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjEffectHandle* effectHandle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(thisObj), &EffectHandleMethods);
    ObjEffectHandle* srcHandle;
    EffectInstance* effect;
    EffectInstance* srcEffect;
    Allocator temp_allocator;
    Allocator* allocator;
    if (!effectHandle || argCount != 3 || !IS_STRING(args[0]) || !IS_STRING(args[2])) return NIL_VAL;
    srcHandle = (ObjEffectHandle*)getHandle(vm, args[1], &EffectHandleMethods);
    if (!srcHandle) return BOOL_VAL(false);
    effect = resolve_effect_handle(effectHandle);
    srcEffect = resolve_effect_handle(srcHandle);
    if (!effect || !srcEffect || srcHandle->clip_id == 0) return BOOL_VAL(false);
    allocator = resolve_effect_allocator(vm, effectHandle, &temp_allocator);
    return BOOL_VAL(effect_link_color(allocator, effect, AS_CSTRING(args[0]),
                                      srcHandle->clip_id, srcEffect, AS_CSTRING(args[2])));
}
Value effectUnlinkNumberBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjEffectHandle* effectHandle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(thisObj), &EffectHandleMethods);
    EffectInstance* effect;
    Allocator temp_allocator;
    Allocator* allocator;
    if (!effectHandle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    effect = resolve_effect_handle(effectHandle);
    if (!effect) return BOOL_VAL(false);
    allocator = resolve_effect_allocator(vm, effectHandle, &temp_allocator);
    return BOOL_VAL(effect_unlink(allocator, effect, AS_CSTRING(args[0]), EFFECT_LINK_NUMBER));
}
Value effectUnlinkColorBinding(VM* vm, i32 argCount, Value* args) {
    ObjInstance* thisObj = GET_SELF;
    ObjEffectHandle* effectHandle = (ObjEffectHandle*)getHandle(vm, OBJ_VAL(thisObj), &EffectHandleMethods);
    EffectInstance* effect;
    Allocator temp_allocator;
    Allocator* allocator;
    if (!effectHandle || argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;
    effect = resolve_effect_handle(effectHandle);
    if (!effect) return BOOL_VAL(false);
    allocator = resolve_effect_allocator(vm, effectHandle, &temp_allocator);
    return BOOL_VAL(effect_unlink(allocator, effect, AS_CSTRING(args[0]), EFFECT_LINK_COLOR));
}
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

static void defineNativeMethodWithSignature(VM* vm, ObjClass* klass, const char* name, NativeFn func,
                                            i32 arity, i32 minArity, const EffectParamSpec* params) {
    ObjNative* native;
    ObjString* methodName;
    const char** paramNames = NULL;

    if (arity > 0) {
        paramNames = ALLOCATE(vm, const char*, arity);
        for (i32 i = 0; i < arity; i++) paramNames[i] = params[i].name;
    }
    native = newNativeWithSignature(vm, func, arity, minArity, paramNames);
    if (paramNames) FREE_ARRAY(vm, const char*, paramNames, arity);
    push(vm, OBJ_VAL(native));
    methodName = copyString(vm, name, (int)strlen(name));
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

static void defineEffectClass(VM* vm, const EffectClassSpec* spec) {
    ObjString* className;
    ObjClass* klass;

    className = copyString(vm, spec->class_name, (int)strlen(spec->class_name));
    push(vm, OBJ_VAL(className));
    klass = newClass(vm, className);
    push(vm, OBJ_VAL(klass));

    defineNativeMethodWithSignature(vm, klass, "init", effectInit, spec->param_count, 0, spec->params);
    registerEffectMethods(vm, klass);

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
    defineNativeMethod(vm, klass, "volume", clipSetVolume);
    effect_bindings_register(vm);
}
static void registerImageMethods(VM* vm, ObjClass* klass) {
    registerCommonMethods(vm, klass);
}
// Text 独有
static void registerTextMethods(VM* vm, ObjClass* klass) {
    registerCommonMethods(vm, klass);
    defineNativeMethod(vm, klass, "setFont", textSetFont);
    defineNativeMethod(vm, klass, "setSize", textSetSize);
    defineNativeMethod(vm, klass, "setColor", textSetColor);
    defineNativeMethod(vm, klass, "setLetterSpacing", textSetLetterSpacing);
    defineNativeMethod(vm, klass, "setStroke", textSetStroke);
    defineNativeMethod(vm, klass, "addAnimator", textAddAnimator);
    defineNativeMethod(vm, klass, "getAnimatorCount", textGetAnimatorCount);
    defineNativeMethod(vm, klass, "getAnimator", textGetAnimatorBinding);
}
static void registerSolidMethods(VM* vm, ObjClass* klass) {
    registerCommonMethods(vm, klass);
    defineNativeMethod(vm, klass, "setColor", solidSetColor);
}
static void registerAdjustmentMethods(VM* vm, ObjClass* klass) {
    registerCommonMethods(vm, klass);
    defineNativeMethod(vm, klass, "setAffectsWholeFrame", adjustmentSetAffectsWholeFrame);
    defineNativeMethod(vm, klass, "affectsWholeFrame", adjustmentAffectsWholeFrame);
    defineNativeMethod(vm, klass, "setFeather", adjustmentSetFeather);
    defineNativeMethod(vm, klass, "getFeather", adjustmentGetFeather);
    defineNativeMethod(vm, klass, "setBlendMode", adjustmentSetBlendMode);
    defineNativeMethod(vm, klass, "getBlendMode", adjustmentGetBlendMode);
    defineNativeMethod(vm, klass, "setMaskSource", adjustmentSetMaskSource);
    defineNativeMethod(vm, klass, "getMaskSourceClipId", adjustmentGetMaskSourceClipId);
    defineNativeMethod(vm, klass, "setMaskMode", adjustmentSetMaskMode);
    defineNativeMethod(vm, klass, "getMaskMode", adjustmentGetMaskMode);
    defineNativeMethod(vm, klass, "setMaskInvert", adjustmentSetMaskInvert);
    defineNativeMethod(vm, klass, "maskInverted", adjustmentMaskInverted);
}
static void registerNestedTimelineClipMethods(VM* vm, ObjClass* klass) {
    registerCommonMethods(vm, klass);
    defineNativeMethod(vm, klass, "getTimeline", nestedClipGetTimeline);
    defineNativeMethod(vm, klass, "setTimeline", nestedClipSetTimeline);
}
static void registerTimelineMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "add", timelineAdd);
    defineNativeMethod(vm, klass, "addTrack", timelineAddTrack);
    defineNativeMethod(vm, klass, "removeTrack", timelineRemoveTrackBinding);
    defineNativeMethod(vm, klass, "getTrackCount", timelineGetTrackCount);
    defineNativeMethod(vm, klass, "getClipCount", timelineGetClipCountBinding);
    defineNativeMethod(vm, klass, "getClip", timelineGetClipBinding);
    defineNativeMethod(vm, klass, "removeClip", timelineRemoveClipBinding);
    defineNativeMethod(vm, klass, "setTrackName", timelineSetTrackName);
    defineNativeMethod(vm, klass, "getTrackName", timelineGetTrackName);
    defineNativeMethod(vm, klass, "setTrackVisible", timelineSetTrackVisible);
    defineNativeMethod(vm, klass, "isTrackVisible", timelineIsTrackVisible);
    defineNativeMethod(vm, klass, "setTrackLocked", timelineSetTrackLocked);
    defineNativeMethod(vm, klass, "isTrackLocked", timelineIsTrackLocked);
    defineNativeMethod(vm, klass, "setBackgroundColor", timelineSetBackgroundColor);
    defineNativeMethod(vm, klass, "getDuration", timelineGetDurationBinding);
}
static void registerProjectMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setTimeline", projectSetTimeline);
    defineNativeMethod(vm, klass, "preview", projectPreview);
    defineNativeMethod(vm, klass, "setPreviewRange", projectSetPreviewRange);
    defineNativeMethod(vm, klass, "clearPreviewRange", projectClearPreviewRange);
    defineNativeMethod(vm, klass, "setBackgroundColor", projectSetBackgroundColor);
    defineNativeMethod(vm, klass, "getDuration", projectGetDurationBinding);
}
static void registerClipInstanceMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setStart", clipInstanceSetStart);
    defineNativeMethod(vm, klass, "getStart", clipInstanceGetStart);
    defineNativeMethod(vm, klass, "setDuration", clipInstanceSetDuration);
    defineNativeMethod(vm, klass, "getDuration", clipInstanceGetDuration);
    defineNativeMethod(vm, klass, "setInPoint", clipInstanceSetInPoint);
    defineNativeMethod(vm, klass, "getInPoint", clipInstanceGetInPoint);
    defineNativeMethod(vm, klass, "setZIndex", clipInstanceSetZIndex);
    defineNativeMethod(vm, klass, "getZIndex", clipInstanceGetZIndex);
    defineNativeMethod(vm, klass, "setVisible", clipInstanceSetVisible);
    defineNativeMethod(vm, klass, "isVisible", clipInstanceIsVisible);
    defineNativeMethod(vm, klass, "setPositionMode", clipInstanceSetPositionMode);
    defineNativeMethod(vm, klass, "getPositionMode", clipInstanceGetPositionMode);
    defineNativeMethod(vm, klass, "alignTo", clipInstanceAlignToBinding);
    defineNativeMethod(vm, klass, "alignToComposition", clipInstanceAlignToCompositionBinding);
    defineNativeMethod(vm, klass, "clearAlignmentTarget", clipInstanceClearAlignmentTargetBinding);
    defineNativeMethod(vm, klass, "getAlignmentTargetClipId", clipInstanceGetAlignmentTargetClipId);
    defineNativeMethod(vm, klass, "remove", clipInstanceRemoveBinding);
    defineNativeMethod(vm, klass, "moveToTrack", clipInstanceMoveToTrack);
    defineNativeMethod(vm, klass, "duplicate", clipInstanceDuplicate);
    defineNativeMethod(vm, klass, "addEffect", clipInstanceAddEffectBinding);
    defineNativeMethod(vm, klass, "getEffectCount", clipInstanceGetEffectCountBinding);
    defineNativeMethod(vm, klass, "getEffect", clipInstanceGetEffectBinding);
    defineNativeMethod(vm, klass, "removeEffect", clipInstanceRemoveEffectBinding);
    defineNativeMethod(vm, klass, "clearEffects", clipInstanceClearEffectsBinding);
}
static void registerEffectMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "getName", effectGetNameBinding);
    defineNativeMethod(vm, klass, "setSource", effectSetSourceBinding);
    defineNativeMethod(vm, klass, "getSourceClipId", effectGetSourceClipIdBinding);
    defineNativeMethod(vm, klass, "linkNumber", effectLinkNumberBinding);
    defineNativeMethod(vm, klass, "linkColor", effectLinkColorBinding);
    defineNativeMethod(vm, klass, "unlinkNumber", effectUnlinkNumberBinding);
    defineNativeMethod(vm, klass, "unlinkColor", effectUnlinkColorBinding);
}
static void registerAnimatedPropertyMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "keyframes", animatedPropertyKeyframesBinding);
    defineNativeMethod(vm, klass, "add", animatedPropertyAddBinding);
    defineNativeMethod(vm, klass, "set", animatedPropertySetBinding);
    defineNativeMethod(vm, klass, "withPreset", animatedPropertyWithPresetBinding);
    defineNativeMethod(vm, klass, "remove", animatedPropertyRemoveBinding);
    defineNativeMethod(vm, klass, "clear", animatedPropertyClearBinding);
    defineNativeMethod(vm, klass, "count", animatedPropertyCountBinding);
    defineNativeMethod(vm, klass, "time", animatedPropertyTimeBinding);
    defineNativeMethod(vm, klass, "value", animatedPropertyValueBinding);
    defineNativeMethod(vm, klass, "type", animatedPropertyTypeBinding);
    defineNativeMethod(vm, klass, "weight", animatedPropertyWeightBinding);
    defineNativeMethod(vm, klass, "shift", animatedPropertyShiftBinding);
    defineNativeMethod(vm, klass, "scaleTimes", animatedPropertyScaleTimesBinding);
    defineNativeMethod(vm, klass, "copyFrom", animatedPropertyCopyFromBinding);
}

static void registerTextAnimatorMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "addRangeSelector", textAnimatorAddRangeSelector);
    defineNativeMethod(vm, klass, "getRangeSelectorCount", textAnimatorGetRangeSelectorCount);
    defineNativeMethod(vm, klass, "getRangeSelector", textAnimatorGetRangeSelectorBinding);
    defineNativeMethod(vm, klass, "addExpressionSelector", textAnimatorAddExpressionSelector);
    defineNativeMethod(vm, klass, "getExpressionSelectorCount", textAnimatorGetExpressionSelectorCount);
    defineNativeMethod(vm, klass, "getExpressionSelector", textAnimatorGetExpressionSelectorBinding);
    defineNativeMethod(vm, klass, "addWigglySelector", textAnimatorAddWigglySelector);
    defineNativeMethod(vm, klass, "getWigglySelectorCount", textAnimatorGetWigglySelectorCount);
    defineNativeMethod(vm, klass, "getWigglySelector", textAnimatorGetWigglySelectorBinding);
}

static void registerRangeSelectorMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setShape", rangeSelectorSetShape);
    defineNativeMethod(vm, klass, "getShape", rangeSelectorGetShape);
    defineNativeMethod(vm, klass, "setBasedOn", rangeSelectorSetBasedOn);
    defineNativeMethod(vm, klass, "getBasedOn", rangeSelectorGetBasedOn);
    defineNativeMethod(vm, klass, "setMode", rangeSelectorSetMode);
    defineNativeMethod(vm, klass, "getMode", rangeSelectorGetMode);
}

static void registerExpressionSelectorMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setExpression", expressionSelectorSetExpression);
    defineNativeMethod(vm, klass, "getExpression", expressionSelectorGetExpression);
    defineNativeMethod(vm, klass, "setCallback", expressionSelectorSetCallback);
    defineNativeMethod(vm, klass, "getCallback", expressionSelectorGetCallback);
    defineNativeMethod(vm, klass, "setBasedOn", expressionSelectorSetBasedOn);
    defineNativeMethod(vm, klass, "getBasedOn", expressionSelectorGetBasedOn);
    defineNativeMethod(vm, klass, "setMode", expressionSelectorSetMode);
    defineNativeMethod(vm, klass, "getMode", expressionSelectorGetMode);
}

static void registerWigglySelectorMethods(VM* vm, ObjClass* klass) {
    defineNativeMethod(vm, klass, "setBasedOn", wigglySelectorSetBasedOn);
    defineNativeMethod(vm, klass, "getBasedOn", wigglySelectorGetBasedOn);
    defineNativeMethod(vm, klass, "setMode", wigglySelectorSetMode);
    defineNativeMethod(vm, klass, "getMode", wigglySelectorGetMode);
}

void registerVideoBindings(VM* vm) {
    size_t i;
    effect_registry_init(vm);
    effect_register_builtin_processors();
    defineClass(vm, "Clip", videoInit, registerClipMethods);
    defineClass(vm, "Image", imageInit, registerImageMethods);
    defineClass(vm, "Text", textInit, registerTextMethods);
    defineClass(vm, "Solid", solidInit, registerSolidMethods);
    defineClass(vm, "Adjustment", adjustmentInit, registerAdjustmentMethods);
    defineClass(vm, "Group", groupInit, registerNestedTimelineClipMethods);
    defineClass(vm, "Precomp", precompInit, registerNestedTimelineClipMethods);
    defineClass(vm, "Timeline", timelineInit, registerTimelineMethods);
    defineClass(vm, "Project", projectInit, registerProjectMethods);
    // 新增 ClipInstance (无 init)
    defineClass(vm, "ClipInstance", clipInstanceInit, registerClipInstanceMethods);
    defineClass(vm, "AnimatedProperty", animatedPropertyInit, registerAnimatedPropertyMethods);
    defineClass(vm, "TextAnimator", textAnimatorInit, registerTextAnimatorMethods);
    defineClass(vm, "RangeSelector", rangeSelectorInit, registerRangeSelectorMethods);
    defineClass(vm, "ExpressionSelector", expressionSelectorInit, registerExpressionSelectorMethods);
    defineClass(vm, "WigglySelector", wigglySelectorInit, registerWigglySelectorMethods);
    for (i = 0; i < sizeof(EFFECT_CLASS_SPECS) / sizeof(EFFECT_CLASS_SPECS[0]); i++) {
        defineEffectClass(vm, &EFFECT_CLASS_SPECS[i]);
    }
    // 新增全局 addUserPreset
    defineNative(vm, "addUserPreset", globalAddUserPreset);
}
