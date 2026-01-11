// src/engine/service/transcoder.h

#pragma once

#include "engine/binding/object.h"

// 前向声明 VM，避免循环引用
typedef struct VM VM;

/**
 * 将单个视频素材进行转码导出。
 * 
 * 该函数使用纯 FFmpeg 管道 (Decode -> Encode) 进行处理，
 * 不经过 OpenGL 合成层。适用于不需要添加特效、只需剪辑或格式转换的场景。
 * 
 * @param vm 虚拟机实例 (用于内存统计和管理)
 * @param clip 包含源文件路径、入出点信息的素材对象
 * @param output_filename 输出文件的完整路径 (如 "export.mp4")
 */
void transcode_clip(VM* vm, ObjClip* clip, const char* output_filename);