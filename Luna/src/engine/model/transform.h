// src/engine/model/transform.h

#pragma once

#include <stdint.h>

// 变换属性
// 采用 16 字节对齐，方便未来 SIMD 优化
// 总大小：32 字节
typedef struct __attribute__((aligned(16))) {
    float x;             // 0-4
    float y;             // 4-8
    float scale_x;       // 8-12
    float scale_y;       // 12-16 (128-bit boundary)
 
    float rotation;      // 16-20
    float opacity;       // 20-24
    int32_t z_index;     // 24-28
    uint32_t _padding;   // 28-32 (Padding for alignment)
} Transform;