// src/core/vm/error.h
#pragma once

#include <stdarg.h>
#include <stdbool.h>

typedef struct VM VM;

bool runtimeError(VM* vm, const char* format, ...);