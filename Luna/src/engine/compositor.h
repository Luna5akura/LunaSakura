// src/engine/compositor.h

#pragma once
#include <sys/stat.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <va/va_glx.h>
#include "core/memory.h"
#include "core/compiler/compiler.h"
#include "engine/timeline.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
// Forward declare VM
typedef struct VM VM;
// 前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwrContext;
// [新增] 线程安全的帧队列节点
typedef struct DecodedFrame {
    struct AVFrame* frame;
    double pts;
    struct DecodedFrame* next;
} DecodedFrame;
// [新增] 帧队列管理
typedef struct {
    DecodedFrame* head;
    DecodedFrame* tail;
    int count;
    int capacity; // 限制缓冲帧数，防止内存爆炸
} FrameQueue;
// --- ClipDecoder ---
typedef struct {
    // --- 标识与资源 ---
    // 注意：clip_ref 仅供主线程比较使用，解码线程不可访问
    ObjClip* clip_ref;
   
    // 线程专用的文件路径副本 (使用标准 malloc 分配)
    char* file_path_copy;
    // --- Threading ---
    SDL_Thread* thread;
    SDL_mutex* mutex; // 保护队列和状态
    SDL_cond* cond_can_produce; // 队列不满时唤醒解码线程
    bool thread_running;
    bool seek_requested;
    double seek_target_time;
    // --- Queues (Protected by mutex) ---
    FrameQueue video_queue;
   
    // --- Audio Buffer (Protected by mutex) ---
    // 环形缓冲区 (Stereo Interleaved Float)
    float* audio_ring_buffer; // 标准 malloc
    i32 rb_capacity;
    i32 rb_head; // Write
    i32 rb_tail; // Read
    i32 rb_count;
    // --- Video State (Main Thread Only) ---
    // OpenGL Textures (Y, U, V)
    GLuint tex_y, tex_u, tex_v;
    double current_pts;
    bool texture_ready; // 是否有有效画面
    // --- Properties ---
    float current_volume;
    bool active_this_frame;
    double last_render_time;
    int64_t start_pts; // 第一帧的原始 PTS
    bool has_start_pts; // 是否已捕获第一帧
    // --- Internal FFmpeg Contexts (Thread Local mostly) ---
    // 这些指针主要由解码线程管理，主线程只在销毁时触碰
    struct AVFormatContext* fmt_ctx;
    struct AVCodecContext* vid_ctx;
    struct AVCodecContext* aud_ctx;
    struct SwrContext* swr_ctx;
    i32 video_stream_idx;
    i32 audio_stream_idx;
} ClipDecoder;
// --- Compositor ---
typedef struct {
    VM* vm;
    Timeline* timeline;
    // GL Resources
    GLuint shader_program;
    GLuint vao, vbo;
    GLuint fbo;
    GLuint output_texture; // 渲染结果
    // CPU Readback
    u8* cpu_output_buffer;
    bool cpu_buffer_stale;
    // Decoders
    ClipDecoder** decoders;
    i32 decoder_count;
    i32 decoder_capacity;
    // Audio
    SDL_AudioDeviceID audio_device;
    SDL_mutex* mix_mutex; // 专门保护混音的锁
} Compositor;
// API
Compositor* compositor_create(VM* vm, Timeline* timeline);
void compositor_free(VM* vm, Compositor* comp);
void compositor_render(Compositor* comp, double time);
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height);
u8* compositor_get_cpu_buffer(Compositor* comp);