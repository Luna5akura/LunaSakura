// src/engine/compositor.h

#ifndef LUNA_ENGINE_COMPOSITOR_H
#define LUNA_ENGINE_COMPOSITOR_H
#include <stdint.h>
#include "engine/timeline.h"

// 前向声明 FFmpeg 结构体，避免在头文件中引入庞大的 libav 头文件
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

// 单个素材的解码上下文
// 映射关系：一个 ObjClip (文件) -> 一个 ClipDecoder
typedef struct {
    ObjClip* clip_ref; // 关联的资源对象 (Key)
   
    // FFmpeg Internals
    struct AVFormatContext* fmt_ctx;
    struct AVCodecContext* dec_ctx;
    struct AVFrame* raw_frame; // 解码后的原始帧 (YUV/RGB)
    int video_stream_idx;
    double current_pts_sec; // 当前解码器停留在文件的第几秒
   
    // Scaler: 用于将任意格式/尺寸的视频转为画布标准格式
    struct SwsContext* sws_ctx;
    // LRU / 缓存管理
    int last_used_frame_count; // 用于简单 LRU 淘汰
    bool active_this_frame; // 标记当前帧是否被用到
} ClipDecoder;

// 合成器主对象
typedef struct {
    Timeline* timeline;
   
    // 输出缓冲区 (Framebuffer)
    // 格式：RGBA8888 (4 bytes per pixel)
    // 大小：width * height * 4
    uint8_t* output_buffer;
    size_t buffer_size;
    // 解码器池
    // 简单的动态数组，数量通常不会太多 (同时播放的视频很少超过 10 个)
    ClipDecoder** decoders;
    int decoder_count;
    int decoder_capacity;
    // 统计信息 (用于性能监控)
    long frame_counter;
} Compositor;

// --- API ---
// 初始化合成器
Compositor* compositor_create(Timeline* timeline);
// 销毁合成器 (释放所有 FFmpeg 资源和缓冲区)
void compositor_free(Compositor* comp);
// 核心渲染函数
// 给定时间点，计算所有轨道，混合图像，结果写入 comp->output_buffer
void compositor_render(Compositor* comp, double time);
// 获取输出缓冲区指针 (给 SDL/OpenGL 上传用)
uint8_t* compositor_get_buffer(Compositor* comp);

#endif