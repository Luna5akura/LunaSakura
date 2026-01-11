// src/engine/media/audio/mixer.h

#pragma once

#include "engine/media/codec/decoder.h"

typedef struct AudioMixer AudioMixer;

// --- Lifecycle ---

// 创建混音器并打开音频设备
AudioMixer* mixer_create(int sample_rate);

// 销毁混音器并关闭设备
void mixer_free(AudioMixer* mixer);

// --- Per-Frame Logic ---

// 开始一帧的音频更新（加锁）
void mixer_begin_frame(AudioMixer* mixer);

// 添加当前帧需要播放的音源
// decoder: 解码器实例
// volume: 音量 (0.0 - 1.0)
void mixer_add_source(AudioMixer* mixer, Decoder* decoder, float volume);

// 提交一帧的更新（解锁）
void mixer_end_frame(AudioMixer* mixer);

// --- Internal (SDL Callback) ---
void mixer_sdl_callback(void* userdata, Uint8* stream, int len);