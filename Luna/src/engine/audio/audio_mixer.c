// src/engine/audio/audio_mixer.c

#include "audio_mixer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_AUDIO_SOURCES 32

struct AudioMixer {
    SDL_AudioDeviceID device_id;
    SDL_mutex* mutex;
    
    // 活跃音源列表 (每一帧渲染时更新)
    struct {
        Decoder* decoder;
        float volume;
    } sources[MAX_AUDIO_SOURCES];
    
    int source_count;
    int sample_rate;
};

// SDL 音频回调运行在独立线程
void mixer_sdl_callback(void* userdata, Uint8* stream, int len) {
    AudioMixer* mixer = (AudioMixer*)userdata;
    
    // 1. 清空缓冲区 (静音)
    memset(stream, 0, len); 
    
    // 2. 尝试加锁
    // 如果主线程正在更新列表 (mixer_begin_frame)，我们稍微等待或跳过
    if (SDL_LockMutex(mixer->mutex) == 0) {
        float* out_buffer = (float*)stream;
        int needed_samples = len / sizeof(float);
        
        // 3. 遍历所有活跃源进行混音
        for (int i = 0; i < mixer->source_count; i++) {
            Decoder* dec = mixer->sources[i].decoder;
            float vol = mixer->sources[i].volume;
            
            if (dec) {
                // 调用 decoder.h 中的 API 填充数据
                // 注意：decoder_mix_audio 应该是累加 (+=) 到 buffer
                decoder_mix_audio(dec, out_buffer, needed_samples, vol);
            }
        }
        
        SDL_UnlockMutex(mixer->mutex);
    }
}

AudioMixer* mixer_create(int sample_rate) {
    AudioMixer* mixer = (AudioMixer*)malloc(sizeof(AudioMixer));
    if (!mixer) return NULL;
    memset(mixer, 0, sizeof(AudioMixer));
    
    mixer->sample_rate = sample_rate;
    mixer->mutex = SDL_CreateMutex();
    mixer->source_count = 0;
    
    // 初始化 SDL 音频
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "[Audio] SDL Audio Init failed: %s\n", SDL_GetError());
        // 不返回 NULL，允许在无音频设备下运行
    } else {
        SDL_AudioSpec want = {0}, have;
        want.freq = sample_rate;
        want.format = AUDIO_F32; // 32位浮点音频
        want.channels = 2;       // 立体声
        want.samples = 1024;     // 缓冲区大小
        want.callback = mixer_sdl_callback;
        want.userdata = mixer;
        
        mixer->device_id = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (mixer->device_id > 0) {
            SDL_PauseAudioDevice(mixer->device_id, 0); // 开始播放
        } else {
            fprintf(stderr, "[Audio] Failed to open device: %s\n", SDL_GetError());
        }
    }
    
    return mixer;
}

void mixer_free(AudioMixer* mixer) {
    if (!mixer) return;
    
    if (mixer->device_id > 0) {
        SDL_CloseAudioDevice(mixer->device_id);
    }
    SDL_DestroyMutex(mixer->mutex);
    free(mixer);
}

void mixer_begin_frame(AudioMixer* mixer) {
    SDL_LockMutex(mixer->mutex);
    mixer->source_count = 0; // 重置列表
}

void mixer_add_source(AudioMixer* mixer, Decoder* decoder, float volume) {
    if (mixer->source_count < MAX_AUDIO_SOURCES) {
        mixer->sources[mixer->source_count].decoder = decoder;
        mixer->sources[mixer->source_count].volume = volume;
        mixer->source_count++;
    }
}

void mixer_end_frame(AudioMixer* mixer) {
    SDL_UnlockMutex(mixer->mutex);
}