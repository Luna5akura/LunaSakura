// src/engine/codec/encoder.h

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct Encoder Encoder;

// --- Lifecycle ---

// 创建编码器
// filename: 输出路径 (如 "out.mp4")
// width, height: 视频分辨率
// fps: 帧率
// bitrate: 码率 (传 0 则使用默认 CRF 23 策略)
Encoder* encoder_create(const char* filename, int width, int height, double fps, int bitrate);

// 写入一帧 RGB 数据 (通常来自 glReadPixels)
// rgb_buffer: 必须是 width * height * 4 字节 (RGBA) 或 3 字节 (RGB)
// input_stride: 一行的字节数 (通常是 width * 4)
bool encoder_encode_rgb(Encoder* enc, uint8_t* rgb_buffer, int input_stride);

// 写入一帧 YUV 数据 (用于纯转码场景，可选)
// 这里的 frame 必须是 AVFrame* 类型，但在头文件中我们使用 void* 保持封装
bool encoder_encode_avframe(Encoder* enc, void* av_frame);

// 结束编码，写入文件尾，释放资源
void encoder_finish(Encoder* enc);