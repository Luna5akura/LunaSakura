// src/engine/codec/decoder.h

#pragma once

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include "engine/binding/object.h" // 需要 ObjClip 定义

// 前向声明，外部无需知道具体结构
typedef struct Decoder Decoder;

// --- Lifecycle ---

// 为指定的 Clip 创建解码器
Decoder* decoder_create(ObjClip* clip);

// 销毁解码器，停止线程，释放资源
void decoder_destroy(Decoder* dec);

// --- Video API ---

// 根据时间轴时间更新解码器状态（寻找对应帧、上传纹理）
// 返回 true 表示纹理已准备好，false 表示无画面
bool decoder_update_video(Decoder* dec, double timeline_time);

// 获取当前的 OpenGL 纹理 ID (YUV)
GLuint decoder_get_texture_y(Decoder* dec);
GLuint decoder_get_texture_u(Decoder* dec);
GLuint decoder_get_texture_v(Decoder* dec);

// 获取解码器关联的 Clip 对象（用于缓存查找）
ObjClip* decoder_get_clip_ref(Decoder* dec);

// 标记当前帧是否活跃（用于音频混音策略）
void decoder_set_active(Decoder* dec, bool active);

// --- Audio API ---

// 将解码器的音频数据混音到输出流中
// stream: 目标缓冲区
// len_samples: 需要的样本数 (float count)
// volume: 音量 (0.0 - 1.0)
void decoder_mix_audio(Decoder* dec, float* stream, int len_samples, float volume);