// src/vm/memory.c

#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "engine/timeline.h" // for timeline_free

// 1. 优化：增加 VM* 上下文参数
void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
    // 2. 优化：启用 GC 统计与触发
    // 在分配内存前更新计数，这样 GC 可以在内存爆满前运行
    if (vm != NULL) {
        if (newSize > oldSize) {
            vm->bytesAllocated += (newSize - oldSize);
        } else {
            vm->bytesAllocated -= (oldSize - newSize);
        }
        // 策略：当分配新内存且总用量超过阈值时触发 GC
        // (DEBUG_STRESS_GC 宏用于调试，每次分配都强制 GC)
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
    // --- 核心分配逻辑 ---
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }
    void* result = realloc(pointer, newSize);
   
    // 致命错误处理
    if (UNLIKELY(result == NULL)) {
        fprintf(stderr, "Fatal Error: Out of memory.\n");
        exit(1);
    }
   
    return result;
}

void freeObject(VM* vm, Obj* object) {
#ifdef DEBUG_LOG_GC
    // 打印指针地址和类型，方便追踪内存泄漏
    printf("%p free type %d\n", (void*)object, object->type);
#endif
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            // FAM (柔性数组) 释放逻辑：
            // 必须释放 Header + chars + '\0'
            // 注意：reallocate 这里传入 vm 用于统计内存减少
            (void)reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);  // (void) 抑制警告
            break;
        }
       
        case OBJ_NATIVE: {
            (void)reallocate(vm, object, sizeof(ObjNative), 0);  // (void) 抑制警告
            break;
        }
       
        case OBJ_CLIP: {
            // Clip 内部持有的 path 是 ObjString*，由 GC 遍历处理，
            // 这里只需要释放结构体本身
            (void)reallocate(vm, object, sizeof(ObjClip), 0);  // (void) 抑制警告
            break;
        }
        case OBJ_TIMELINE: {
            ObjTimeline* obj = (ObjTimeline*)object;
            // 3. 安全性：防止 Double Free 或空指针解引用
            if (obj->timeline) {
                timeline_free(obj->timeline);
                obj->timeline = NULL;
            }
            (void)reallocate(vm, object, sizeof(ObjTimeline), 0);  // (void) 抑制警告
            break;
        }
       
        default:
            // 防御性编程：检测未处理的类型（通常意味着 switch 漏写了）
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
    // 4. 优化：清理 GC 灰色栈 (Gray Stack)
    // 这是我们在 vm.h 中新增的结构，VM 销毁时必须释放
    free(vm->grayStack);
    vm->grayStack = NULL;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
}

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;  // Avoid cycles or redundant marking
    
    object->isMarked = true;
    
#ifdef DEBUG_LOG_GC
    printf("[GC] Marking object %p (type %d)\n", (void*)object, object->type);
#endif
    
    // Recursively mark referenced objects based on type
    switch (object->type) {
        case OBJ_STRING:
            // Strings have no references
            break;
        case OBJ_NATIVE:
            // Natives have no references
            break;
        case OBJ_CLIP: {
            ObjClip* clip = (ObjClip*)object;
            if (clip->path != NULL) markObject((Obj*)clip->path);  // Mark referenced string
            break;
        }
        case OBJ_TIMELINE: {
            ObjTimeline* tl = (ObjTimeline*)object;
            // If timeline has references (e.g., clips), mark them here
            break;
        }
        default:
            break;
    }
}

void markValue(Value value) {
    if (IS_OBJ(value)) {
        markObject(AS_OBJ(value));
    }
    // Non-object values (e.g., numbers, bools) need no marking
}

void markTable(Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL) {
            markObject((Obj*)entry->key);
            markValue(entry->value);
        }
    }
}

void collectGarbage(VM* vm) {
    fprintf(stderr, "[GC] Starting GC. Allocated: %zu bytes, objects: %p\n", vm->bytesAllocated, (void*)vm->objects);

    // Mark roots (example: globals table, stack)
    markTable(&vm->globals);  // Assume markTable marks all values in table
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        markValue(*slot);
    }

    // Mark all reachable objects
    Obj* obj = vm->objects;
    while (obj != NULL) {
        if (!obj->isMarked) {
            fprintf(stderr, "[GC] Marking object %p (type %d)\n", (void*)obj, obj->type);
            markObject(obj);  // Recursively mark referenced objects
        }
        obj = obj->next;
    }

    // Sweep: Free unmarked objects (TODO: implement full sweep logic)
    // For now, reset marks
    obj = vm->objects;
    while (obj != NULL) {
        obj->isMarked = false;
        obj = obj->next;
    }

    fprintf(stderr, "[GC] GC completed.\n");
    vm->nextGC = vm->bytesAllocated * 2;
}