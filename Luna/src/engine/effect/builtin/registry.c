#include "internal.h"

void effect_register_builtin_processors(void) {
    effect_registry_register(&TINT_PROCESSOR);
    effect_registry_register(&FILL_PROCESSOR);
    effect_registry_register(&BRIGHTNESS_CONTRAST_PROCESSOR);
    effect_registry_register(&BLUR_PROCESSOR);
    effect_registry_register(&GLOW_PROCESSOR);
    effect_registry_register(&MOSAIC_PROCESSOR);
    effect_registry_register(&GRID_PROCESSOR);
    effect_registry_register(&GRADIENT_RAMP_PROCESSOR);
    effect_registry_register(&FRACTAL_NOISE_PROCESSOR);
    effect_registry_register(&DISPLACEMENT_MAP_PROCESSOR);
    effect_registry_register(&POSTERIZE_PROCESSOR);
    effect_registry_register(&SLIDER_CONTROL_PROCESSOR);
    effect_registry_register(&ANGLE_CONTROL_PROCESSOR);
    effect_registry_register(&CHECKBOX_CONTROL_PROCESSOR);
    effect_registry_register(&POINT_CONTROL_PROCESSOR);
    effect_registry_register(&COLOR_CONTROL_PROCESSOR);
}
