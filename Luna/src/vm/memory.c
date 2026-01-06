// src/vm/memory.c

#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "engine/timeline.h"
#include "object.h" // 必须包含，用于访问 ObjFunction 等具体结构

void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
    if (vm != NULL) {
        if (newSize > oldSize) {
            vm->bytesAllocated += (newSize - oldSize);
        } else {
            vm->bytesAllocated -= (oldSize - newSize);
        }
        if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
            collectGarbage(vm);
#else
            if (vm->bytesAllocated > vm->nextGC) {
                collectGarbage(vm);
            }
#endif
        }
    }
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }
    void* result = realloc(pointer, newSize);
   
    if (UNLIKELY(result == NULL)) {
        fprintf(stderr, "Fatal Error: Out of memory.\n");
        exit(1);
    }
    return result;
}

void freeObject(VM* vm, Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            (void)reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            // 释放函数体内的字节码块
            freeChunk(vm, &function->chunk);
            (void)reallocate(vm, object, sizeof(ObjFunction), 0);
            break;
        }
        case OBJ_NATIVE: {
            (void)reallocate(vm, object, sizeof(ObjNative), 0);
            break;
        }
        case OBJ_CLIP: {
            // ObjClip 内部的 path 是 ObjString，由 GC 统一管理，不需要手动释放
            (void)reallocate(vm, object, sizeof(ObjClip), 0);
            break;
        }
        case OBJ_TIMELINE: {
            ObjTimeline* obj = (ObjTimeline*)object;
            if (obj->timeline) {
                timeline_free(vm, obj->timeline);
                obj->timeline = NULL;
            }
            (void)reallocate(vm, object, sizeof(ObjTimeline), 0);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            FREE_ARRAY(vm, Value, list->items, list->capacity);
            (void)reallocate(vm, object, sizeof(ObjList), 0);
            break;
        }
        default:
            fprintf(stderr, "Unknown object type %d in freeObject.\n", object->type);
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
    vm->objects = NULL;
    free(vm->grayStack);
    vm->grayStack = NULL;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
}

void markObject(VM* vm, Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;
   
    object->isMarked = true;
    if (vm->grayCapacity < vm->grayCount + 1) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
        vm->grayStack = (Obj**)realloc(vm->grayStack, sizeof(Obj*) * vm->grayCapacity);
        if (vm->grayStack == NULL) exit(1);
    }
    vm->grayStack[vm->grayCount++] = object;
}

void markValue(VM* vm, Value value) {
    if (IS_OBJ(value)) {
        markObject(vm, AS_OBJ(value));
    }
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
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            if (function->name) markObject(vm, (Obj*)function->name);
            // 标记函数常量池中的对象（如字符串字面量、其他函数等）
            markArray(vm, &function->chunk.constants);
            break;
        }
        case OBJ_CLIP: {
            ObjClip* clip = (ObjClip*)object;
            if (clip->path) markObject(vm, (Obj*)clip->path);
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            markArray(vm, (ValueArray*)list); // ObjList 布局兼容 ValueArray (count, capacity, items)
            // 或者更安全地手动遍历：
            /*
            for (u32 i = 0; i < list->count; i++) {
                markValue(vm, list->items[i]);
            }
            */
            break;
        }
        // Native, String, Timeline 没有需要递归标记的托管对象字段
        default:
            break;
    }
}

static void markRoots(VM* vm) {
    // 1. 标记栈上的值
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(vm, *slot);
    }

    // 2. 标记调用栈帧中的 Function 对象
    // (这替代了之前的 vm->chunk 逻辑，因为代码现在存在 function->chunk 中)
    for (i32 i = 0; i < vm->frameCount; i++) {
        if (vm->frames[i].function) {
            markObject(vm, (Obj*)vm->frames[i].function);
        }
    }

    // 3. 标记全局变量表
    markTable(vm, &vm->globals);

    // 4. [已删除] vm->chunk
    // 旧代码：if (vm->chunk) markArray(vm, &vm->chunk->constants);
    // 新架构下，当前正在执行的代码在 vm->frames 中，已在步骤 2 中被标记。

    // 5. 标记当前激活的时间轴 (防止预览时被回收)
    if (vm->active_timeline) {
        timeline_mark(vm, vm->active_timeline);
    }
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
    printf("-- gc begin\n");
    size_t before = vm->bytesAllocated;
#endif
    
    markRoots(vm);
    traceReferences(vm);
    tableRemoveWhite(&vm->strings);
    sweep(vm);
    
    vm->nextGC = vm->bytesAllocated * 2;
    
#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf(" collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm->bytesAllocated, before, vm->bytesAllocated, vm->nextGC);
#endif
}