// src/engine/service/exporter.c

#include "exporter.h"
#include "engine/media/codec/encoder.h"
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/engine.h"

void export_timeline(VM* vm, Timeline* tl, const char* output_filename) {
    if (!tl) return;
    printf("[Export] Rendering to '%s'...\n", output_filename);

    // 1. 创建环境
    Compositor* comp = compositor_create(vm, tl);
    
    // 码率传 0 使用默认质量 (CRF 23)
    Encoder* enc = encoder_create(output_filename, tl->width, tl->height, tl->fps, 0);
    if (!enc) {
        fprintf(stderr, "[Export] Failed to create encoder.\n");
        compositor_free(vm, comp);
        return;
    }

    i32 total_frames = (i32)(tl->duration * tl->fps);
    double step = 1.0 / tl->fps;
    
    // 3. 渲染循环
    for (i32 i = 0; i < total_frames; i++) {
        double t = i * step;
        u8* pixels;
      
        // A. 渲染到 FBO
        compositor_render(comp, t);
      
        // B. 直接复用未翻转的 GPU 读回缓冲，交给 encoder 使用负 stride 读取
        pixels = compositor_get_cpu_buffer_raw(comp);
        if (!pixels) break;
        
        // C. 编码 (GL 读回是底朝上，传入最后一行并使用负 stride 避免额外翻转)
        encoder_encode_rgb(enc,
                           pixels + (size_t)(tl->height - 1) * (size_t)(tl->width * 4),
                           -(tl->width * 4));
      
        if (i % 30 == 0) {
            printf("\r[Export] Frame %d / %d (%.1f%%)", i, total_frames, (double)i/total_frames*100.0);
            fflush(stdout);
        }
    }
  
    printf("\n[Export] Finishing...\n");
    
    // 4. 清理
    encoder_finish(enc);
    compositor_free(vm, comp);
    printf("[Export] Done.\n");
}
