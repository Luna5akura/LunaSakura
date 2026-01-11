// src/engine/compositor.h

#pragma once
#include <sys/stat.h>
#include <time.h>
#include <SDL2/SDL.h>

// [已删除] #include <va/va_glx.h> 
#include "core/memory.h"
#include "core/compiler/compiler.h"
#include "engine/timeline.h"
#include "engine/codec/decoder.h"
#include "engine/audio/audio_mixer.h" // [新增]

typedef struct VM VM;

// --- Compositor ---
typedef struct {
    VM* vm;
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
    Decoder** decoders;
    i32 decoder_count;
    i32 decoder_capacity;
    
    // Audio Module [新增]
    AudioMixer* mixer;
    
    // [已删除] SDL_AudioDeviceID audio_device;
    // [已删除] SDL_mutex* mix_mutex;
} Compositor;

// API
Compositor* compositor_create(VM* vm, Timeline* timeline);
void compositor_free(VM* vm, Compositor* comp);
void compositor_render(Compositor* comp, double time);
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height);
u8* compositor_get_cpu_buffer(Compositor* comp);