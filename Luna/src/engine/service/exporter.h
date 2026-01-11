// src/engine/service/exporter.h

#pragma once

#include "engine/model/timeline.h"

// 前向声明
typedef struct VM VM;

// 将时间轴渲染并导出为视频文件
// vm: 虚拟机实例 (用于内存管理)
// tl: 时间轴对象
// output_filename: 输出路径 (如 "output.mp4")
// 注意：这是一个阻塞操作
void export_timeline(VM* vm, Timeline* tl, const char* output_filename);