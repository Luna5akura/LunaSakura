// src/vm/table.h

#ifndef LUNA_TABLE_H
#define LUNA_TABLE_H

#include "common.h"
#include "value.h"

// --- Hash Table Entry ---
// Uses open addressing with tombstones for deletion.
// State encoding:
// 1. Empty: key == NULL, value == NIL_VAL
// 2. Tombstone: key == NULL, value == TRUE_VAL (used to keep probe chains intact)
// 3. Live: key != NULL
typedef struct {
    ObjString* key;
    Value value;
} Entry;

// --- Hash Table ---
typedef struct {
    u32 count; // Count of live entries
    u32 capacity; // Total slots (always a power of 2)
    Entry* entries;
} Table;

// --- Lifecycle ---
void initTable(Table* table);
void freeTable(Table* table);

// --- Core Operations ---
// Returns true if the key was found.
bool tableGet(Table* table, ObjString* key, Value* value);
// Returns true if a new key was added, false if an existing key was overwritten.
bool tableSet(Table* table, ObjString* key, Value value);
// Returns true if the key was found and deleted (replaced with tombstone).
bool tableDelete(Table* table, ObjString* key);
// Copies all valid entries from one table to another (used for inheritance/resizing).
void tableAddAll(Table* from, Table* to);

// --- String Interning ---
// Checks if a string with the given hash already exists in the table.
// Used to deduplicate strings (String Interning) without allocating a new ObjString first.
ObjString* tableFindString(Table* table, const char* chars, int length, u32 hash);

#endif