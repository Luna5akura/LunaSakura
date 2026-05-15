#pragma once

#include "engine/effect/filter_base.h"
#include "engine/model/animation.h"

#include <glad/glad.h>
#include <math.h>
#include <string.h>

typedef struct {
    Animation amount;
    Animation color[4];
} ColorMixEffectData;

typedef struct {
    Animation brightness;
    Animation contrast;
} BrightnessContrastEffectData;

typedef struct {
    Animation radius;
} BlurEffectData;

typedef struct {
    Animation radius;
    Animation intensity;
    Animation threshold;
    Animation softness;
    Animation color[4];
} GlowEffectData;

typedef struct {
    Animation block_size;
    bool sharp_colors;
} MosaicEffectData;

typedef struct {
    Animation size_x;
    Animation size_y;
    Animation line_width;
    Animation opacity;
    Animation color[4];
} GridEffectData;

typedef struct {
    Animation start[2];
    Animation end[2];
    Animation start_color[4];
    Animation end_color[4];
    Animation blend;
} GradientRampEffectData;

typedef struct {
    Animation scale;
    Animation evolution;
    Animation contrast;
    Animation brightness;
    Animation octaves;
    Animation amount;
    Animation offset_x;
    Animation offset_y;
    bool invert;
} FractalNoiseEffectData;

typedef struct {
    Animation scale_x;
    Animation scale_y;
    Animation amount;
    Animation offset_x;
    Animation offset_y;
    Animation horizontal_channel;
    Animation vertical_channel;
    bool use_luma;
    u32 source_clip_id;
} DisplacementMapEffectData;

typedef struct {
    Animation levels;
    Animation amount;
} PosterizeEffectData;

typedef struct {
    Animation value;
} SingleNumberControlEffectData;

typedef struct {
    Animation value;
} CheckboxControlEffectData;

typedef struct {
    Animation x;
    Animation y;
} PointControlEffectData;

typedef struct {
    Animation color[4];
} ColorControlEffectData;

extern const EffectProcessor TINT_PROCESSOR;
extern const EffectProcessor FILL_PROCESSOR;
extern const EffectProcessor BRIGHTNESS_CONTRAST_PROCESSOR;
extern const EffectProcessor BLUR_PROCESSOR;
extern const EffectProcessor GLOW_PROCESSOR;
extern const EffectProcessor MOSAIC_PROCESSOR;
extern const EffectProcessor GRID_PROCESSOR;
extern const EffectProcessor GRADIENT_RAMP_PROCESSOR;
extern const EffectProcessor FRACTAL_NOISE_PROCESSOR;
extern const EffectProcessor DISPLACEMENT_MAP_PROCESSOR;
extern const EffectProcessor POSTERIZE_PROCESSOR;
extern const EffectProcessor SLIDER_CONTROL_PROCESSOR;
extern const EffectProcessor ANGLE_CONTROL_PROCESSOR;
extern const EffectProcessor CHECKBOX_CONTROL_PROCESSOR;
extern const EffectProcessor POINT_CONTROL_PROCESSOR;
extern const EffectProcessor COLOR_CONTROL_PROCESSOR;

Animation* color_mix_get_number_animation(void* instance, const char* key);
bool color_mix_get_color_animations_cb(void* instance, const char* key, Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a);
Animation* brightness_contrast_get_number_animation(void* instance, const char* key);
Animation* blur_get_number_animation(void* instance, const char* key);
Animation* glow_get_number_animation(void* instance, const char* key);
bool glow_get_color_animations_cb(void* instance, const char* key, Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a);
Animation* mosaic_get_number_animation(void* instance, const char* key);
Animation* grid_get_number_animation(void* instance, const char* key);
bool grid_get_color_animations_cb(void* instance, const char* key, Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a);
Animation* gradient_ramp_get_number_animation(void* instance, const char* key);
bool gradient_ramp_get_color_animations_cb(void* instance, const char* key, Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a);
Animation* fractal_noise_get_number_animation(void* instance, const char* key);
Animation* displacement_map_get_number_animation(void* instance, const char* key);
Animation* posterize_get_number_animation(void* instance, const char* key);
Animation* slider_control_get_number_animation(void* instance, const char* key);
Animation* checkbox_control_get_number_animation(void* instance, const char* key);
Animation* point_control_get_number_animation(void* instance, const char* key);
bool color_control_get_color_animations_cb(void* instance, const char* key, Animation** out_r, Animation** out_g, Animation** out_b, Animation** out_a);
