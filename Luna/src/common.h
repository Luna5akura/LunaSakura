// include/common.h

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#if defined(__GNUC__) || defined(__clang__)
    #define LIKELY(x) __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LIKELY(x) (x)
    #define UNLIKELY(x) (x)
#endif

#if defined(_MSC_VER)
    #define INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define INLINE __attribute__((always_inline)) inline
#else
    #define INLINE inline
#endif

#define UNUSED(x) ((void)(x))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define U8_COUNT 256

// --- Bithack 辅助 ---
static inline u32 loadWord4(const void* ptr) {
    u32 result;
    memcpy(&result, ptr, sizeof(u32));
    return result;
}

// 开启调试打印的宏
#define DEBUG_PRINT_CODE
#define DEBUG_LOG_GC
// #define DEBUG_TRACE_EXECUTION