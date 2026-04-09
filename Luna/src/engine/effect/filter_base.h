#pragma once
#include "common.h"
#include "allocator.h"
#include "core/vm/vm.h"

// === 效果实例（每个 Clip 可挂载多个效果）===
typedef struct EffectInstance {
    const char* name;
    void*       data;                    // 具体效果私有数据
    struct EffectInstance* next;
} EffectInstance;

// === 效果处理器接口（所有新效果必须实现）===
typedef struct {
    const char* name;                                      // "gaussian_blur"
    
    // 创建效果实例
    void*      (*create)(Allocator* a, Value* params, i32 paramCount);
    
    // 应用效果（renderContext 为将来 compositor 传递的上下文，目前可用 void* 兼容）
    void       (*apply)(void* instance, void* renderContext, double time);
    
    // 销毁效果实例
    void       (*destroy)(Allocator* a, void* instance);
    
    // GC 标记钩子（可选）
    void       (*mark)(VM* vm, void* instance);
} EffectProcessor;

// === 注册表 API ===
void effect_registry_init(VM* vm);
void effect_registry_register(const EffectProcessor* proc);
const EffectProcessor* effect_registry_get(const char* name);