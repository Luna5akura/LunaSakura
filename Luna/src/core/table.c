// src/core/table.c

#include <stdlib.h>
#include "memory.h"
#include "vm/vm.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(VM* vm, Table* table) {
    FREE_ARRAY(vm, Entry, table->entries, table->capacity);
    initTable(table);
}

// --- Internal Helper: Find Entry ---
static INLINE Entry* findEntry(Entry* entries, u32 capacity, Value key) {
    u32 mask = capacity - 1;
    u32 index = valueHash(key) & mask;
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
        index = (index + 1) & mask;
    }
}

// --- Internal Helper: Resize ---
static void adjustCapacity(VM* vm, Table* table, u32 capacity) {
    Entry* entries = ALLOCATE(vm, Entry, capacity);
    for (u32 i = 0; i < capacity; i++) {
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    u32 mask = capacity - 1;

    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_NIL(entry->key)) continue;
        u32 index = valueHash(entry->key) & mask;
        while (!IS_NIL(entries[index].key)) {
            index = (index + 1) & mask;
        }

        entries[index].key = entry->key;
        entries[index].value = entry->value;
        table->count++;
    }

    FREE_ARRAY(vm, Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

// --- Operations ---

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
    entry->value = BOOL_VAL(true); // Tombstone 标记
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

// --- String Interning ---

ObjString* tableFindString(Table* table, const char* chars, u32 length, u32 hash) {
    if (table->count == 0) return NULL;

    u32 mask = table->capacity - 1;
    u32 index = hash & mask;

    for (;;) {
        Entry* entry = &table->entries[index];
        if (IS_NIL(entry->key)) {
            if (IS_NIL(entry->value)) return NULL;
        } else if (IS_OBJ(entry->key) && OBJ_TYPE(entry->key) == OBJ_STRING) {
            ObjString* string = AS_STRING(entry->key);
            if (string->hash == hash && 
                string->length == length &&
                (length == 0 || (string->chars[0] == chars[0] && memcmp(string->chars, chars, length) == 0))) {
                return string;
            }
        }

        index = (index + 1) & mask;
    }
}

// --- GC Helpers ---

void markTable(VM* vm, Table* table) {
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        markValue(vm, entry->key);
        markValue(vm, entry->value);
    }
}

void tableRemoveWhite(Table* table) {
    u32 capacity = table->capacity;
    Entry* entries = table->entries;
    
    for (u32 i = 0; i < capacity; i++) {
        Entry* entry = &entries[i];
        if (IS_NIL(entry->key)) continue;
        if (IS_OBJ(entry->key) && !AS_OBJ(entry->key)->isMarked) {
            entry->key = NIL_VAL;
            entry->value = BOOL_VAL(true);
        }
    }
}