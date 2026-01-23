// src/engine/codec/decoder.h

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "engine/model/clip.h"

typedef struct Decoder Decoder;

// ... (其他函数保持不变) ...
Decoder* decoder_create(Clip* clip);
void decoder_destroy(Decoder* dec);
bool decoder_update_video(Decoder* dec, double timeline_time);

// [修改] 增加 width 和 height 参数
bool decoder_get_video_data(Decoder* dec, uint8_t* data[3], int linesize[3], int* width, int* height);

Clip* decoder_get_clip_ref(Decoder* dec);
void decoder_set_active(Decoder* dec, bool active);
void decoder_mix_audio(Decoder* dec, float* stream, int len_samples, float volume);