// src/engine/media/audio/mixer.c

#include "mixer.h"
#include <SDL2/SDL.h> // 仅用于 Thread 和 Mutex，不用于渲染
#define MAX_AUDIO_SOURCES 32
struct AudioMixer {
    SDL_AudioDeviceID device_id;
    SDL_mutex* mutex;
    struct {
        Decoder* decoder;
        float volume;
    } sources[MAX_AUDIO_SOURCES];
    int source_count;
    int sample_rate;
};
AudioMixer* mixer_create(int sample_rate) {
    AudioMixer* mixer = (AudioMixer*)malloc(sizeof(AudioMixer));
    if (!mixer) return NULL;
    memset(mixer, 0, sizeof(AudioMixer));
    mixer->sample_rate = sample_rate;
    mixer->mutex = SDL_CreateMutex();
    mixer->source_count = 0;
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "[Audio] SDL Audio Init failed: %s\n", SDL_GetError());
    } else {
        SDL_AudioSpec want = {0}, have;
        want.freq = sample_rate;
        want.format = AUDIO_F32;
        want.channels = 2;
        want.samples = 1024;
        want.callback = mixer_sdl_callback;
        want.userdata = mixer;
        mixer->device_id = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (mixer->device_id > 0) {
            SDL_PauseAudioDevice(mixer->device_id, 0);
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
    mixer->source_count = 0;
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
void mixer_sdl_callback(void* userdata, Uint8* stream, int len) {
    AudioMixer* mixer = (AudioMixer*)userdata;
    memset(stream, 0, len);
    if (SDL_LockMutex(mixer->mutex) == 0) {
        float* out_buffer = (float*)stream;
        int needed_samples = len / sizeof(float);
        for (int i = 0; i < mixer->source_count; i++) {
            Decoder* dec = mixer->sources[i].decoder;
            float vol = mixer->sources[i].volume;
            if (dec) {
                decoder_mix_audio(dec, out_buffer, needed_samples, vol);
            }
        }
        SDL_UnlockMutex(mixer->mutex);
    }
}