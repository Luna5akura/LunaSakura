// src/core/memory.c

#include <stdlib.h>
#include "memory.h"
#include "vm/vm.h"
#include "compiler/compiler.h"
// --- Core Allocation ---
void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
    // 1. 统计内存并触发 GC
    if (vm != NULL) {
        // 使用有符号运算前先转换，防止 size_t 下溢风险 (虽然逻辑上不太可能)
        if (newSize > oldSize) {
            vm->bytesAllocated += (newSize - oldSize);
        } else {
            vm->bytesAllocated -= (oldSize - newSize);
        }
        if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
            collectGarbage(vm);
#else
            if (UNLIKELY(vm->bytesAllocated > vm->nextGC)) {
                collectGarbage(vm);
            }
#endif
        }
    }
    // 2. 释放逻辑
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }
    // 3. 分配/重分配逻辑
    void* result = realloc(pointer, newSize);
   
    // 内存耗尽处理
    if (UNLIKELY(result == NULL)) {
        // 尝试最后一次 GC 挽救
        if (vm != NULL) {
             collectGarbage(vm);
             result = realloc(pointer, newSize);
             if (result != NULL) return result;
        }
       
        fprintf(stderr, "Fatal: Out of memory.\n");
        exit(1);
    }
   
    return result;
}
// --- Object Freer ---
static void freeObject(VM* vm, Obj* object) {
    switch (object->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(vm, ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(vm, ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE: {
            FREE(vm, ObjUpvalue, object);
            break;
        }
        case OBJ_DICT: {
            ObjDict* dict = (ObjDict*)object;
            freeTable(vm, &dict->items);
            FREE(vm, ObjDict, object);
            break;
        }
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            // String 使用 Flexible Array Member 模式分配
            reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(vm, &function->chunk);
            // [新增] 释放参数名数组
            if (function->paramNames != NULL) {
                // 注意：这里释放的是指针数组本身，字符串对象由 GC 管理
                FREE_ARRAY(vm, ObjString*, function->paramNames, function->arity);
            }
            FREE(vm, ObjFunction, object);
            break;
        }
        case OBJ_NATIVE: {
            FREE(vm, ObjNative, object);
            break;
        }
        case OBJ_CLIP: {
            // 如果 ObjClip 内部有动态分配的资源，需先释放
            FREE(vm, ObjClip, object);
            break;
        }
        case OBJ_TIMELINE: {
            ObjTimeline* obj = (ObjTimeline*)object;
            if (obj->timeline) {
                timeline_free(vm, obj->timeline);
                obj->timeline = NULL;
            }
            FREE(vm, ObjTimeline, object);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            FREE_ARRAY(vm, Value, list->items, list->capacity);
            FREE(vm, ObjList, object);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            freeTable(vm, &klass->methods);
            FREE(vm, ObjClass, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(vm, &instance->fields);
            FREE(vm, ObjInstance, object);
            break;
        }
        case OBJ_BOUND_METHOD: {
            FREE(vm, ObjBoundMethod, object);
            break;
        }
        default:
            // 未知类型，防止内存泄漏最好报错
            fprintf(stderr, "Unknown object type %d in freeObject\n", object->type);
            break;
    }
}
void freeObjects(VM* vm) {
    Obj* object = vm->objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(vm, object);
        object = next;
    }
   
    free(vm->grayStack);
    vm->grayStack = NULL;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
   
    // [重要] 重置 allocation，防止 VM 重启/销毁时状态错误
    vm->bytesAllocated = 0;
}
// --- Garbage Collector ---
// 真正的标记逻辑 (Slow Path)
void markObjectDo(VM* vm, Obj* object) {
    object->isMarked = true;
   
    if (UNLIKELY(vm->grayCapacity < vm->grayCount + 1)) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
        // 注意：这里使用系统 realloc 这是一个独立的缓冲区，不计入 VM 托管的 bytesAllocated
        // 或者也可以使用 reallocate(vm, ...) 纳入管理，取决于设计策略。
        // 为了避免 GC 过程中触发 GC (递归灾难)，通常使用 raw realloc 或确保 reallocate 能处理 recursion。
        // 这里使用 raw realloc 安全。
        vm->grayStack = (Obj**)realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
       
        if (vm->grayStack == NULL) {
            fprintf(stderr, "Fatal: Out of memory during GC.\n");
            exit(1);
        }
    }
   
    vm->grayStack[vm->grayCount++] = object;
}
static void markArray(VM* vm, ValueArray* array) {
    for (u32 i = 0; i < array->count; i++) {
        markValue(vm, array->values[i]);
    }
}
static void blackenObject(VM* vm, Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    switch (object->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject(vm, (Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject(vm, (Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_UPVALUE: {
            markValue(vm, ((ObjUpvalue*)object)->closed);
            break;
        }
        case OBJ_DICT: {
            ObjDict* dict = (ObjDict*)object;
            markTable(vm, &dict->items);
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            markValue(vm, bound->receiver);
            markValue(vm, bound->method);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            markObject(vm, (Obj*)klass->name);
            markTable(vm, &klass->methods);
            // [新增] 只有当 superclass 存在时才标记
            if (klass->superclass) markObject(vm, (Obj*)klass->superclass);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            markObject(vm, (Obj*)instance->klass);
            markTable(vm, &instance->fields);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            if (function->name) markObject(vm, (Obj*)function->name);
            markArray(vm, &function->chunk.constants);
           
            // [重要修复] 必须标记参数名，否则字符串会被 GC 回收
            if (function->paramNames != NULL) {
                for (i32 i = 0; i < function->arity; i++) {
                    markObject(vm, (Obj*)function->paramNames[i]);
                }
            }
            break;
        }
        case OBJ_CLIP: {
            ObjClip* clip = (ObjClip*)object;
            if (clip->path) markObject(vm, (Obj*)clip->path);
            break;
        }
        case OBJ_LIST: {
            // [修复] 不能强转为 ValueArray，因为 ObjList 包含 Obj 头部
            // 必须显式访问 ObjList 的成员
            ObjList* list = (ObjList*)object;
            for (u32 i = 0; i < list->count; i++) {
                markValue(vm, list->items[i]);
            }
            break;
        }
        case OBJ_TIMELINE:
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}
static void markRoots(VM* vm) {
    // 1. Stack
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(vm, *slot);
    }
   
    // 2. Call Frames (Closures)
    for (i32 i = 0; i < vm->frameCount; i++) {
        markObject(vm, (Obj*)vm->frames[i].closure);
    }
   
    // 3. Open Upvalues
    for (ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject(vm, (Obj*)upvalue);
    }
   
    // 4. Globals
    markTable(vm, &vm->globals);
   
    // 5. Special Roots
    if (vm->active_timeline) timeline_mark(vm, vm->active_timeline);
    markObject(vm, (Obj*)vm->initString);
   
    // 6. Compiler Roots (如果在编译期间触发 GC)
    markCompilerRoots(vm);
}
static void traceReferences(VM* vm) {
    while (vm->grayCount > 0) {
        Obj* object = vm->grayStack[--vm->grayCount];
        blackenObject(vm, object);
    }
}
static void sweep(VM* vm) {
    Obj* previous = NULL;
    Obj* object = vm->objects;
   
    while (object != NULL) {
        if (object->isMarked) {
            // 重置标记，为下一次 GC 做准备
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
           
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm->objects = object;
            }
           
            freeObject(vm, unreached);
        }
    }
}
void collectGarbage(VM* vm) {
#ifdef DEBUG_LOG_GC
    size_t before = vm->bytesAllocated;
    printf("-- GC begin: %zu bytes allocated\n", before);
#endif
    markRoots(vm);
    traceReferences(vm);
   
    // 移除弱引用字符串 (Interned Strings)
    // 注意：tableRemoveWhite 需要访问 ObjString->obj.isMarked
    tableRemoveWhite(&vm->strings);
   
    sweep(vm);
    // [调优] GC 阈值动态调整
    size_t after = vm->bytesAllocated;
   
    // 防止除零错误 (极罕见)
    if (vm->nextGC == 0) vm->nextGC = 1024 * 1024;
    double usageRatio = (double)after / (double)vm->nextGC;
   
    // 如果内存使用率很高 (>70%)，说明当前堆太小，或者分配速率很高
    // 增长倍率降低 (1.5倍) 会导致更频繁的 GC，从而保持内存紧凑
    // 标准做法通常是直接 * 2，但为了平滑性能，根据压力动态调整是合理的
    if (usageRatio > 0.7) {
        vm->nextGC = (size_t)(after * 1.5);
    } else {
        vm->nextGC = (size_t)(after * 2.0);
    }
   
    // 设定下限，防止空闲时频繁微小 GC
    if (vm->nextGC < 1024 * 1024) {
        vm->nextGC = 1024 * 1024;
    }
#ifdef DEBUG_LOG_GC
    printf("-- GC end: %zu bytes allocated (freed %zu)\n", after, before - after);
    printf("-- Next GC threshold: %zu\n", vm->nextGC);
#endif
}
