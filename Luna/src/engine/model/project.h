// src/engine/model/project.h

#pragma once

#include "common.h"
#include "engine/model/timeline.h" // 需要 Timeline 定义

// Project 纯数据结构
// 负责持有 Timeline 指针以及全局项目配置
typedef struct Project {
    u32 width;
    u32 height;
    double fps;
    
    // 持有的 Timeline 实例
    Timeline* timeline;
    
    // 预览范围控制
    bool use_preview_range;
    double preview_start;
    double preview_end;
} Project;