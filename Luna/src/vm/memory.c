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
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            // 释放内部存储的 Value 数组
            FREE_ARRAY(vm, Value, list->items, list->capacity);
            // 释放对象本身
            (void)reallocate(vm, object, sizeof(ObjList), 0);
            break;
        }
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
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
                timeline_free(vm, obj->timeline);
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
        // grayStack 是纯指针数组，不通过 VM 统计分配（避免递归 GC），直接用 realloc
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
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            // 标记列表中的每一个元素，防止被 GC 回收
            for (u32 i = 0; i < list->count; i++) {
                markValue(vm, list->items[i]);
            }
            break;
        }
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
    // 1. 标记栈上的值
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(vm, *slot);
    }

    // 2. 标记调用栈帧中的 Context (这里是 ObjClip)
    // 根据你的 vm.h，CallFrame 包含 ObjClip* clip
    for (i32 i = 0; i < vm->frameCount; i++) {
        // 只有当 clip 不为空时才标记 (虽然通常会有值)
        if (vm->frames[i].clip) {
            markObject(vm, (Obj*)vm->frames[i].clip);
        }
    }

    // [已删除] openUpvalues 循环，因为 VM 结构体中没有定义它
    // for (Obj* upvalue = vm->openUpvalues; ... ) { ... } 

    // 3. 标记全局变量表
    markTable(vm, &vm->globals);

    // 4. 标记当前编译单元的常量 (防止 GC 回收字面量)
    if (vm->chunk) {
        markArray(vm, &vm->chunk->constants);
    }
    
    // 5. [新增关键修复] 标记当前激活的时间轴
    // 这是修复 Use-After-Free 崩溃的核心：
    // 只要时间轴正在被预览，它引用的所有素材(ObjClip)都必须存活
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