#include "registry.h"

#define MAX_EFFECTS 64

static const EffectProcessor* g_registeredEffects[MAX_EFFECTS];
static int g_effectCount = 0;

void effect_registry_init(VM* vm) {
    (void)vm;  // 暂时未使用
    g_effectCount = 0;
    memset(g_registeredEffects, 0, sizeof(g_registeredEffects));
    // 未来所有内置效果在此注册
}

void effect_registry_register(const EffectProcessor* proc) {
    if (g_effectCount >= MAX_EFFECTS) {
        fprintf(stderr, "Effect registry full!\n");
        return;
    }
    g_registeredEffects[g_effectCount++] = proc;
    printf("[Effect] Registered: %s\n", proc->name);
}

const EffectProcessor* effect_registry_get(const char* name) {
    for (int i = 0; i < g_effectCount; i++) {
        if (strcmp(g_registeredEffects[i]->name, name) == 0) {
            return g_registeredEffects[i];
        }
    }
    return NULL;
}