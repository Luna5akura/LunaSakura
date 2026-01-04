// src/vm/table.c

#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"  // 新增：访问全局 vm（如果存在）或定义 VM

extern VM vm;  // 假设全局 vm 定义在 vm.c 中，需要 extern

#define TABLE_MAX_LOAD 0.75

// Force inline for hot-path helper functions
#ifndef INLINE
    #if defined(_MSC_VER)
        #define INLINE __forceinline
    #else
        #define INLINE __attribute__((always_inline)) inline
    #endif
#endif

void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(&vm, Entry, table->entries, table->capacity);  // 传入 &vm
    initTable(table);
}

// --- Core Lookup Algorithm ---
// Linear probing with tombstone support.
// capacity must be a power of 2.
static INLINE Entry* findEntry(Entry* entries, u32 capacity, ObjString* key) {
    // Optimization: Use bitwise AND instead of modulo (%) for power-of-2 capacity.
    u32 index = key->hash & (capacity - 1);
   
    Entry* tombstone = NULL;
    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty slot found.
                // If we passed a tombstone, recycle it; otherwise use the empty slot.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // Tombstone found.
                // Record the first one we encounter to reuse it later if needed.
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            // Key match found.
            // Pointer equality works because all strings are interned.
            return entry;
        }
        // Linear probing: wrap around using bitmask
        index = (index + 1) & (capacity - 1);
    }
}

// --- Resizing & Rehash ---
static void adjustCapacity(Table* table, u32 capacity) {
    // 1. Allocate new array using VM memory manager
    Entry* entries = ALLOCATE(&vm, Entry, capacity);  // 传入 &vm
   
    // 2. Initialize slots.
    // Cannot use memset because NIL_VAL (NaN Boxing) is not 0 bytes.
    for (u32 i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }
    // 3. Re-hash live entries.
    // Reset count because tombstones are discarded during resize.
    table->count = 0;
   
    for (u32 i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
       
        // Skip empty slots and tombstones (tombstones have NULL keys)
        if (entry->key == NULL) continue;
        // Find destination in the new array.
        // We can skip the full findEntry() logic because:
        // a) We know keys are unique (no duplicates to find).
        // b) The new array has no tombstones yet.
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
    // 4. Free old array
    FREE_ARRAY(&vm, Entry, table->entries, table->capacity);  // 传入 &vm
   
    table->entries = entries;
    table->capacity = capacity;
}

bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
    *value = entry->value;
    return true;
}

bool tableSet(Table* table, ObjString* key, Value value) {
    // Load factor check: Grow if usage exceeds 75%.
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        u32 capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = (entry->key == NULL);
   
    // Increment count only if we are using a truly empty slot (not a tombstone).
    // Tombstones are treated as "occupied" for load factor but don't increase count.
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
    // Place a tombstone.
    // Key = NULL, Value = TRUE (Bool).
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
   
    // Note: We do not decrement table->count here.
    // The slot is still physically occupied until the next rehash.
    return true;
}

void tableAddAll(Table* from, Table* to) {
    for (u32 i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

// --- String Interning Helper ---
ObjString* tableFindString(Table* table, const char* chars, int length, u32 hash) {
    if (table->count == 0) return NULL;
    u32 index = hash & (table->capacity - 1);
   
    for (;;) {
        Entry* entry = &table->entries[index];
       
        if (entry->key == NULL) {
            // Stop if we hit a non-tombstone empty slot
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == (u32)length &&
                   entry->key->hash == hash) {
            // Fast path passed (Hash & Length match).
            // Do full memory comparison.
            if (memcmp(entry->key->chars, chars, length) == 0) {
                return entry->key;
            }
        }
        index = (index + 1) & (table->capacity - 1);
    }
}