// src/vm/table.c

#include <stdlib.h>
#include "memory.h"
#include "vm.h"
#define TABLE_MAX_LOAD 0.75
#ifndef INLINE
    #if defined(_MSC_VER)
        #define INLINE __forceinline
    #else
        #define INLINE __attribute__((always_inline)) inline
    #endif
#endif
static INLINE Entry* findEntry(Entry* entries, u32 capacity, Value key) {
    u32 hash = valueHash(key);
    u32 index = hash & (capacity - 1);
    Entry* tombstone = NULL;
    for (;;) {
        Entry* entry = &entries[index];
        if (IS_NIL(entry->key)) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (valuesEqual(entry->key, key)) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}
static void adjustCapacity(VM* vm, Table* table, u32 capacity) {
    Entry* entries = ALLOCATE(vm, Entry, capacity);
    for (u32 i = 0; i < capacity; i++) {
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }
    table->count = 0;
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_NIL(entry->key)) continue;
      
        Entry* dest = findEntry(entries, capacity, entry->key);
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
bool tableGet(Table* table, Value key, Value* value) {
    if (table->count == 0) return false;
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;
    *value = entry->value;
    return true;
}
bool tableSet(VM* vm, Table* table, Value key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        u32 capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(vm, table, capacity);
    }
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = IS_NIL(entry->key);
    if (isNewKey && IS_NIL(entry->value)) {
        table->count++;
    }
    entry->key = key;
    entry->value = value;
    return isNewKey;
}
bool tableDelete(Table* table, Value key) {
    if (table->count == 0) return false;
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;
    entry->key = NIL_VAL;
    entry->value = BOOL_VAL(true);
    return true;
}
void tableAddAll(VM* vm, Table* from, Table* to) {
    for (u32 i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (!IS_NIL(entry->key)) {
            tableSet(vm, to, entry->key, entry->value);
        }
    }
}
ObjString* tableFindString(Table* table, const char* chars, u32 length, u32 hash) {
    if (table->count == 0) return NULL;
    u32 index = hash & (table->capacity - 1);
    for (;;) {
        Entry* entry = &table->entries[index];
        if (!IS_NIL(entry->key) && IS_OBJ(entry->key) && OBJ_TYPE(entry->key) == OBJ_STRING) {
            ObjString* string = AS_STRING(entry->key);
            if (string->length == length && string->hash == hash &&
                memcmp(string->chars, chars, length) == 0) {
                return string;
            }
        }
        if (IS_NIL(entry->key) && IS_NIL(entry->value)) return NULL;
        index = (index + 1) & (table->capacity - 1);
    }
}
// [新增] GC 标记表中的所有对象
void markTable(VM* vm, Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        // 标记 Key 和 Value
        markValue(vm, entry->key);
        markValue(vm, entry->value);
    }
}
// [新增] 弱引用清理：移除未标记的字符串
// 用于字符串驻留池，当字符串不再被引用时，将其从池中移除
void tableRemoveWhite(Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (!IS_NIL(entry->key) && IS_OBJ(entry->key) && !AS_OBJ(entry->key)->isMarked) {
            tableDelete(table, entry->key);
        }
    }
}