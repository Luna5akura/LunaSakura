// src/vm/table.c
#include <stdlib.h>
#include "memory.h"
#include "vm.h"
// [宏定义与 initTable, freeTable, findEntry, adjustCapacity 保持之前的修改状态]
// ... (为了节省篇幅，这里假设之前的修改已生效，仅展示新增函数和上下文相关部分) ...
// 请确保 freeTable, adjustCapacity, tableSet, tableAddAll 已经包含 VM* vm 参数 (见之前的步骤)
#define TABLE_MAX_LOAD 0.75
// ... (此处省略重复的辅助函数 findEntry 等，请保留原文件内容) ...
// 为了完整性，这里重新提供完整的 table.c 代码，确保没有遗漏
#ifndef INLINE
    #if defined(_MSC_VER)
        #define INLINE __forceinline
    #else
        #define INLINE __attribute__((always_inline)) inline
    #endif
#endif
static INLINE Entry* findEntry(Entry* entries, u32 capacity, ObjString* key) {
    u32 index = key->hash & (capacity - 1);
    Entry* tombstone = NULL;
    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}
static void adjustCapacity(VM* vm, Table* table, u32 capacity) {
    Entry* entries = ALLOCATE(vm, Entry, capacity);
    for (u32 i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }
    table->count = 0;
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;
       
        u32 index = entry->key->hash & (capacity - 1);
        Entry* dest;
        for (;;) {
            dest = &entries[index];
            if (dest->key == NULL) break;
            index = (index + 1) & (capacity - 1);
        }
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    FREE_ARRAY(vm, Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}
void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}
void freeTable(VM* vm, Table* table) {
    FREE_ARRAY(vm, Entry, table->entries, table->capacity);
    initTable(table);
}
bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
    *value = entry->value;
    return true;
}
bool tableSet(VM* vm, Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        u32 capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(vm, table, capacity);
    }
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = (entry->key == NULL);
    if (isNewKey && IS_NIL(entry->value)) {
        table->count++;
    }
    entry->key = key;
    entry->value = value;
    return isNewKey;
}
bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}
void tableAddAll(VM* vm, Table* from, Table* to) {
    for (u32 i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(vm, to, entry->key, entry->value);
        }
    }
}
ObjString* tableFindString(Table* table, const char* chars, u32 length, u32 hash) {
    if (table->count == 0) return NULL;
    u32 index = hash & (table->capacity - 1);
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                   entry->key->hash == hash) {
            if (memcmp(entry->key->chars, chars, length) == 0) {
                return entry->key;
            }
        }
        index = (index + 1) & (table->capacity - 1);
    }
}
// [新增] GC 标记表中的所有对象
void markTable(VM* vm, Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        // 标记 Key (ObjString)
        markObject(vm, (Obj*)entry->key);
        // 标记 Value (可能是 Obj)
        markValue(vm, entry->value);
    }
}
// [新增] 弱引用清理：移除未标记的字符串
// 用于字符串驻留池，当字符串不再被引用时，将其从池中移除
void tableRemoveWhite(Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.isMarked) {
            tableDelete(table, entry->key);
        }
    }
}
