// src/vm/memory.c

#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "engine/timeline.h" 

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
            // [修改] 增加 (void) 转换以抑制 warn_unused_result 警告
            (void)reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);
            break;
        }
        case OBJ_NATIVE: {
            (void)reallocate(vm, object, sizeof(ObjNative), 0);
            break;
        }
        case OBJ_CLIP: {
            (void)reallocate(vm, object, sizeof(ObjClip), 0);
            break;
        }
        case OBJ_TIMELINE: {
            ObjTimeline* obj = (ObjTimeline*)object;
            if (obj->timeline) {
                timeline_free(obj->timeline);
                obj->timeline = NULL;
            }
            (void)reallocate(vm, object, sizeof(ObjTimeline), 0);
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
        // grayStack 只是指针数组，不通过 VM 统计分配（避免递归 GC），直接用 realloc
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
    // [修改] 将 int 改为 u32 以解决符号比较警告
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
        case OBJ_CLIP: {
            ObjClip* clip = (ObjClip*)object;
            if (clip->path) markObject(vm, (Obj*)clip->path);
            break;
        }
        // Native 和 String 没有引用其他对象
        default:
            break;
    }
}

static void markRoots(VM* vm) {
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(vm, *slot);
    }

    markTable(vm, &vm->globals);
    
    // [新增] 标记当前正在编译或执行的 Chunk 中的常量
    // 这解决了 markArray 未使用的警告，同时防止常量池被 GC
    if (vm->chunk) {
        markArray(vm, &vm->chunk->constants);
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
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm->bytesAllocated, before, vm->bytesAllocated, vm->nextGC);
#endif
}