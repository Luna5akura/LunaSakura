// src/engine/compositor.h

#pragma once
#include "engine/timeline.h"
#include <glad/glad.h>
#include <EGL/egl.h>       // 新增: EGL for interop
#include <EGL/eglext.h>    // 新增: EGL 扩展（如 EGL_LINUX_DMA_BUF_EXT）
#include <va/va.h>         // 新增: VA-API
#include <va/va_glx.h>     // 新增: VA-GLX interop
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <drm/drm_fourcc.h> // 新增: DRM_FORMAT_NV12 等
#include <SDL2/SDL_thread.h>  // 新增: SDL_Thread 和 SDL_mutex

// Forward declare VM
typedef struct VM VM;
// 前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVBufferRef;  // 显式前向声明

// 全局 VA 显示（在 main.c 初始化）
extern VADisplay g_va_display;

// --- ClipDecoder ---
// [优化] 新增硬件解码支持

typedef struct {
    ObjClip* clip_ref;
    AVFormatContext* fmt_ctx;
    AVCodecContext* dec_ctx;
    i32 video_stream_idx;
    
    // CPU Decoding
    AVFrame* raw_frame;    // Native format (YUV)
    AVFrame* rgb_frame;    // Converted RGB
    u8* rgb_buffer;        // Raw buffer for RGB frame
    struct SwsContext* sws_ctx;

    // OpenGL
    GLuint texture;        // Stores the RGB frame
    
    double current_pts_sec;
    bool active_this_frame;
    
    // HW Accel (Deprecated/Removed for stability)
    bool hw_accel;
    AVBufferRef* hw_device_ctx;
    AVBufferRef* hw_frames_ctx;
    void* egl_image;       // EGLImageKHR
} ClipDecoder;

// --- Compositor ---
// [优化] 新增全局帧池和线程支持
typedef struct {
    VM* vm; // Added for allocations
    Timeline* timeline;
    // GL Resources
    GLuint shader_program;
    GLuint vao, vbo;
    GLuint fbo;
    GLuint output_texture;

    // CPU Readback
    u8* cpu_output_buffer;
    bool cpu_buffer_stale;
    // Decoders
    ClipDecoder** decoders;
    i32 decoder_count;
    i32 decoder_capacity;

    i64 frame_counter;

    // 新增: 全局帧缓存池（避免 per-clip 碎片）
    AVFrame** frame_pool;
    i32 pool_size;
    i32 pool_used;

    // 新增: 预取线程
    SDL_Thread* prefetch_thread;
    SDL_mutex* decoder_mutex; // 新增: 线程锁
    bool running;
} Compositor;

// --- API ---
// 注意：创建 Compositor 前必须先创建 OpenGL Context (在 main.c 中完成)
Compositor* compositor_create(VM* vm, Timeline* timeline);
void compositor_free(VM* vm, Compositor* comp);
// 核心渲染：绘制到内部 FBO
void compositor_render(Compositor* comp, double time);
// 辅助函数：将内部 FBO 的内容绘制到默认帧缓冲区（即屏幕窗口）
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height);
// 获取 CPU 缓冲区 (用于导出，包含 glReadPixels 和垂直翻转处理)
u8* compositor_get_cpu_buffer(Compositor* comp);