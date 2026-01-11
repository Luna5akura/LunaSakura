// src/engine/timeline_export.c

#include <stdio.h>
#include <glad/glad.h>
#include "timeline.h"
#include "compositor.h"
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/codec/encoder.h" // 引入新模块

// 辅助：垂直翻转像素 (因为 OpenGL 坐标系原点在左下角，而视频在左上角)
static void flip_vertical(u8* pixels, int width, int height, int stride) {
    u8* row = malloc(stride);
    for (int y = 0; y < height / 2; y++) {
        u8* top = pixels + y * stride;
        u8* bot = pixels + (height - 1 - y) * stride;
        memcpy(row, top, stride);
        memcpy(top, bot, stride);
        memcpy(bot, row, stride);
    }
    free(row);
}

void export_timeline(VM* vm, Timeline* tl, const char* output_filename) {
    if (!tl) return;
    printf("[Export] Rendering to '%s'...\n", output_filename);

    // 1. 创建 Compositor (确保当前线程有 GL Context)
    Compositor* comp = compositor_create(vm, tl);
  
    // 2. 创建 Encoder
    // 码率传 0 使用默认质量
    Encoder* enc = encoder_create(output_filename, tl->width, tl->height, tl->fps, 0);
    if (!enc) {
        fprintf(stderr, "[Export] Failed to create encoder.\n");
        compositor_free(vm, comp);
        return;
    }

    i32 total_frames = (i32)(tl->duration * tl->fps);
    double step = 1.0 / tl->fps;
    
    // 准备 PBO 和 内存
    size_t buf_size = tl->width * tl->height * 4;
    u8* pixels = malloc(buf_size);
    
    // 3. 渲染循环
    for (i32 i = 0; i < total_frames; i++) {
        double t = i * step;
      
        // A. 渲染一帧
        compositor_render(comp, t);
      
        // B. 读取像素 (这里使用简单同步读取，为了性能可以使用 PBO，但为了代码清晰暂时用同步)
        glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
        glReadPixels(0, 0, tl->width, tl->height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // C. 翻转 (OpenGL -> Video)
        flip_vertical(pixels, tl->width, tl->height, tl->width * 4);
        
        // D. 编码
        encoder_encode_rgb(enc, pixels, tl->width * 4);
      
        if (i % 30 == 0) {
            printf("\r[Export] Frame %d / %d (%.1f%%)", i, total_frames, (double)i/total_frames*100.0);
            fflush(stdout);
        }
    }
  
    printf("\n[Export] Finishing...\n");
    
    // 4. 清理
    encoder_finish(enc);
    compositor_free(vm, comp);
    free(pixels);
    printf("[Export] Done.\n");
}