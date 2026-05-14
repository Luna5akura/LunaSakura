#pragma once
#include "filter_base.h"

void effect_registry_init(VM* vm);
void effect_registry_register(const EffectProcessor* proc);
const EffectProcessor* effect_registry_get(const char* name);
void effect_register_builtin_processors(void);
