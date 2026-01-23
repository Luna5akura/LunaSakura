// src/engine/model/clip.h

#pragma once

#include "common.h"

// 纯数据结构，不依赖虚拟机
typedef struct Clip {
    // 用于 GC 的反向指针（Bridge 层赋值，Engine 层不使用但保留位置）
    void* user_data; 

    // 资源路径 (不再使用 ObjString，改用纯 char*)
    char* path;
    
    // 基础属性
    double duration;
    double start_time;
    double in_point;
    double out_point;
    double fps;

    // 媒体信息
    bool has_video;
    bool has_audio;
    i32 audio_channels;
    i32 audio_sample_rate;

    // 变换属性默认值
    double default_scale_x;
    double default_scale_y;
    double default_x;
    double default_y;
    double default_opacity;

    double volume; 
    u32 width;
    u32 height;
    i32 layer;
} Clip;

Clip* clip_create(const char* path);
void clip_free(Clip* clip);
