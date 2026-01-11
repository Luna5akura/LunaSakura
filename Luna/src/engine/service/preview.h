// src/engine/service/preview.h

#pragma once

#include "engine/binding/object.h"

// 前向声明
typedef struct VM VM;

// 弹出一个独立窗口预览单个素材 (阻塞式，用于调试查看素材内容)
// clip: 需要预览的素材对象
void play_video_clip_preview(VM* vm, ObjClip* clip);
