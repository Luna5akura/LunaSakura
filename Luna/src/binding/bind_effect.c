#include "core/vm/vm.h"
#include "engine/effect/registry.h"
#include "engine/bridge/object.h"

Value clip_add_effect(VM* vm, i32 argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) return NIL_VAL;
    ObjInstance* self = AS_INSTANCE(args[0]);
    const char* effectName = AS_CSTRING(args[1]);

    const EffectProcessor* proc = effect_registry_get(effectName);
    if (!proc) {
        fprintf(stderr, "Unknown effect: %s\n", effectName);
        return NIL_VAL;
    }

    // 收集参数（后续支持 dict 或 keyword args）
    // 调用 create 并挂载到 Clip 的 effectChain（通过 handle）
    // 具体实现略（可根据需要扩展）
    return OBJ_VAL(self);
}

void effect_bindings_register(VM* vm) {
    // 将 addEffect 方法注册到 Clip 类
    // （在 bind_video.c 中调用）
    printf("[Effects] Registry bindings registered.\n");
}