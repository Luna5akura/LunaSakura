// src/engine/codec/decoder.c

#include "decoder.h"
#include "engine/media/utils/ffmpeg_utils.h"
#include <libswresample/swresample.h>
#include <stdatomic.h>
#include <SDL2/SDL.h>

#define MAX_QUEUE_SIZE 8
#define AUDIO_RB_SIZE 131072 // 必须是 2 的幂，方便位运算优化（这里暂用取模）
#define MIX_SAMPLE_RATE 44100

// --- Internal Structures ---

typedef struct DecodedFrame {
    AVFrame* frame;
    double pts;
    struct DecodedFrame* next;
} DecodedFrame;

typedef struct {
    DecodedFrame* head;
    DecodedFrame* tail;
    int count;
} FrameQueue;

struct Decoder {
    Clip* clip_ref;
    char* file_path_copy;
    
    // Threading
    SDL_Thread* thread;
    SDL_mutex* mutex;
    SDL_cond* cond_can_produce;
    atomic_bool thread_running;
    
    // Seek State
    bool seek_requested;
    double seek_target_time;
    
    // Video State
    FrameQueue video_queue;
    AVFrame* current_frame_cpu; // 当前供渲染的帧
    double current_pts;
    int64_t start_pts;
    bool has_start_pts;
    
    // Audio Ring Buffer (Lock-Free SPSC)
    float* audio_ring_buffer;
    int32_t rb_capacity;
    atomic_int rb_head; // Write index
    atomic_int rb_tail; // Read index
    
    // FFmpeg Context
    MediaContext media; 
    SwrContext* swr_ctx;
    bool active_this_frame;
};

// --- Queue Helpers ---

static void fq_push(FrameQueue* q, AVFrame* frame, double pts) {
    DecodedFrame* node = (DecodedFrame*)malloc(sizeof(DecodedFrame));
    node->frame = frame;
    node->pts = pts;
    node->next = NULL;
    if (q->tail) q->tail->next = node;
    else q->head = node;
    q->tail = node;
    q->count++;
}

static AVFrame* fq_pop(FrameQueue* q, double* out_pts) {
    if (!q->head) return NULL;
    DecodedFrame* node = q->head;
    AVFrame* frame = node->frame;
    if (out_pts) *out_pts = node->pts;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    free(node);
    return frame;
}

static void fq_clear(FrameQueue* q) {
    while (q->head) {
        AVFrame* f = fq_pop(q, NULL);
        av_frame_free(&f);
    }
}

// --- Audio Ring Buffer Helper ---

// 计算可写入空间 (保留 1 个 slot 区分满/空)
static int rb_write_available(Decoder* dec, int head, int tail) {
    // Free = (Tail - Head + Capacity - 1) % Capacity
    return (tail - head + dec->rb_capacity - 1) % dec->rb_capacity;
}

// 计算可读取数量
static int rb_read_available(Decoder* dec, int head, int tail) {
    // Used = (Head - Tail + Capacity) % Capacity
    return (head - tail + dec->rb_capacity) % dec->rb_capacity;
}

// --- Thread Function ---

static int decoder_thread_func(void* data) {
    Decoder* dec = (Decoder*)data;
    
    if (!media_open(&dec->media, dec->file_path_copy, true, true)) {
        return -1;
    }

    // Audio Resampler Init
    if (dec->media.aud_ctx) {
        AVChannelLayout out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&dec->swr_ctx, &out_layout, AV_SAMPLE_FMT_FLT, MIX_SAMPLE_RATE,
            &dec->media.aud_ctx->ch_layout, dec->media.aud_ctx->sample_fmt, 
            dec->media.aud_ctx->sample_rate, 0, NULL);
        swr_init(dec->swr_ctx);
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (atomic_load(&dec->thread_running)) {
        // 1. Handle Seek
        SDL_LockMutex(dec->mutex);
        bool do_seek = dec->seek_requested;
        double seek_tgt = dec->seek_target_time;
        if (do_seek) {
            dec->seek_requested = false;
            fq_clear(&dec->video_queue);
            // 重置音频缓冲区
            atomic_store(&dec->rb_head, 0);
            atomic_store(&dec->rb_tail, 0);
        }
        SDL_UnlockMutex(dec->mutex);

        if (do_seek) {
            int64_t ts = (int64_t)(seek_tgt * AV_TIME_BASE);
            av_seek_frame(dec->media.fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
            if (dec->media.vid_ctx) avcodec_flush_buffers(dec->media.vid_ctx);
            if (dec->media.aud_ctx) avcodec_flush_buffers(dec->media.aud_ctx);
        }

        // 2. Control Queue Size (Backpressure)
        SDL_LockMutex(dec->mutex);
        if (dec->video_queue.count >= MAX_QUEUE_SIZE) {
            SDL_CondWaitTimeout(dec->cond_can_produce, dec->mutex, 10);
            SDL_UnlockMutex(dec->mutex);
            continue; 
        }
        SDL_UnlockMutex(dec->mutex);

        // 3. Read & Decode
        int ret = av_read_frame(dec->media.fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                SDL_Delay(100); // Wait for seek or exit
                continue;
            }
            break; // Error
        }

        // 3a. Process Video
        if (pkt->stream_index == dec->media.vid_stream_idx && dec->media.vid_ctx) {
            if (avcodec_send_packet(dec->media.vid_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec->media.vid_ctx, frame) == 0) {
                    AVFrame* cloned = av_frame_alloc();
                    av_frame_ref(cloned, frame);
                    
                    int64_t pts_val = frame->best_effort_timestamp;
                    
                    SDL_LockMutex(dec->mutex);
                    if (!dec->has_start_pts && pts_val != AV_NOPTS_VALUE) {
                        dec->start_pts = pts_val;
                        dec->has_start_pts = true;
                    }
                    
                    double pts = 0.0;
                    if (pts_val != AV_NOPTS_VALUE && dec->has_start_pts) {
                        pts = (pts_val - dec->start_pts) * av_q2d(dec->media.vid_stream->time_base);
                    } else if (dec->video_queue.tail) {
                        pts = dec->video_queue.tail->pts + 0.033; // Fallback
                    }
                    if (pts < 0) pts = 0;
                    
                    fq_push(&dec->video_queue, cloned, pts);
                    SDL_UnlockMutex(dec->mutex);
                }
            }
        }
        // 3b. Process Audio
        else if (pkt->stream_index == dec->media.aud_stream_idx && dec->media.aud_ctx && dec->swr_ctx) {
            if (avcodec_send_packet(dec->media.aud_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec->media.aud_ctx, frame) == 0) {
                    uint8_t* out_data[2] = {0};
                    int out_samples = av_rescale_rnd(
                        swr_get_delay(dec->swr_ctx, dec->media.aud_ctx->sample_rate) + frame->nb_samples,
                        MIX_SAMPLE_RATE, dec->media.aud_ctx->sample_rate, AV_ROUND_UP);
                        
                    av_samples_alloc(out_data, NULL, 2, out_samples, AV_SAMPLE_FMT_FLT, 0);
                    int len = swr_convert(dec->swr_ctx, out_data, out_samples, 
                                        (const uint8_t**)frame->data, frame->nb_samples);
                    
                    if (len > 0) {
                        int floats_to_write = len * 2; // Stereo
                        float* raw = (float*)out_data[0];
                        
                        // --- Lock-Free Write Start ---
                        int head = atomic_load_explicit(&dec->rb_head, memory_order_relaxed);
                        int tail = atomic_load_explicit(&dec->rb_tail, memory_order_acquire);
                        
                        int avail = rb_write_available(dec, head, tail);
                        
                        if (avail >= floats_to_write) {
                            int chunk1 = dec->rb_capacity - head;
                            if (chunk1 >= floats_to_write) {
                                memcpy(dec->audio_ring_buffer + head, raw, floats_to_write * sizeof(float));
                            } else {
                                memcpy(dec->audio_ring_buffer + head, raw, chunk1 * sizeof(float));
                                memcpy(dec->audio_ring_buffer, raw + chunk1, (floats_to_write - chunk1) * sizeof(float));
                            }
                            
                            // 确保数据写入内存后再更新 Head
                            atomic_store_explicit(&dec->rb_head, 
                                (head + floats_to_write) % dec->rb_capacity, 
                                memory_order_release);
                        }
                        // --- Lock-Free Write End ---
                    }
                    if (out_data[0]) av_freep(&out_data[0]);
                }
            }
        }
        
        av_packet_unref(pkt);
    }
    
    av_frame_free(&frame);
    av_packet_free(&pkt);
    
    media_close(&dec->media);
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    
    return 0;
}

// --- Public API ---

Decoder* decoder_create(Clip* clip) {
    Decoder* dec = (Decoder*)malloc(sizeof(Decoder));
    memset(dec, 0, sizeof(Decoder));
    
    dec->clip_ref = clip;
    dec->file_path_copy = strdup(clip->path); 
    dec->mutex = SDL_CreateMutex();
    dec->cond_can_produce = SDL_CreateCond();
    
    dec->rb_capacity = AUDIO_RB_SIZE;
    dec->audio_ring_buffer = (float*)malloc(sizeof(float) * dec->rb_capacity);
    atomic_init(&dec->rb_head, 0);
    atomic_init(&dec->rb_tail, 0);
    atomic_init(&dec->thread_running, true);
    
    dec->thread = SDL_CreateThread(decoder_thread_func, "DecoderThread", dec);
    return dec;
}

void decoder_destroy(Decoder* dec) {
    if (!dec) return;
    
    atomic_store(&dec->thread_running, false);
    SDL_CondSignal(dec->cond_can_produce);
    SDL_WaitThread(dec->thread, NULL);
    
    SDL_DestroyMutex(dec->mutex);
    SDL_DestroyCond(dec->cond_can_produce);
    
    if (dec->current_frame_cpu) av_frame_free(&dec->current_frame_cpu);
    
    fq_clear(&dec->video_queue);
    free(dec->audio_ring_buffer);
    free(dec->file_path_copy);
    free(dec);
}

bool decoder_update_video(Decoder* dec, double timeline_time) {
    SDL_LockMutex(dec->mutex);
    
    // Seek Check
    double diff = timeline_time - dec->current_pts;
    // 如果差异太大，触发 Seek
    if (diff < -0.1 || diff > 1.0) {
        dec->seek_requested = true;
        dec->seek_target_time = timeline_time;
        dec->current_pts = timeline_time; // 乐观更新，防止连续触发
        SDL_CondSignal(dec->cond_can_produce);
        SDL_UnlockMutex(dec->mutex);
        return false;
    }
    
    AVFrame* best_frame = NULL;
    
    // 寻找最佳帧：把时间戳小于 timeline_time 的帧都丢弃，取最近的一帧
    while (dec->video_queue.head) {
        double f_pts = dec->video_queue.head->pts;
        if (f_pts < timeline_time - 0.05) {
            // Frame is too old
            AVFrame* drop = fq_pop(&dec->video_queue, NULL);
            av_frame_free(&drop);
            SDL_CondSignal(dec->cond_can_produce);
        } else if (f_pts <= timeline_time + 0.05) {
            // Frame is good
            if (best_frame) av_frame_free(&best_frame); // Free previous best
            best_frame = fq_pop(&dec->video_queue, &dec->current_pts);
            SDL_CondSignal(dec->cond_can_produce);
        } else {
            // Frame is in future, stop
            break;
        }
    }
    SDL_UnlockMutex(dec->mutex);

    if (best_frame) {
        if (dec->clip_ref->width == 0) {
            dec->clip_ref->width = best_frame->width;
            dec->clip_ref->height = best_frame->height;
        }
        if (dec->current_frame_cpu) {
            av_frame_free(&dec->current_frame_cpu);
        }
        dec->current_frame_cpu = best_frame;
        return true;
    }
    
    return false;
}

bool decoder_get_video_data(Decoder* dec, uint8_t* data[3], int linesize[3], int* width, int* height) {
    if (!dec || !dec->current_frame_cpu) return false;
    AVFrame* f = dec->current_frame_cpu;
    for (int i=0; i<3; i++) {
        data[i] = f->data[i];
        linesize[i] = f->linesize[i];
    }
    if (width) *width = f->width;
    if (height) *height = f->height;
    return true;
}

Clip* decoder_get_clip_ref(Decoder* dec) { return dec->clip_ref; }

void decoder_set_active(Decoder* dec, bool active) {
    dec->active_this_frame = active;
}

// 供 Mixer 调用：消费音频数据
void decoder_mix_audio(Decoder* dec, float* stream, int len_samples, float volume) {
    if (!dec || !dec->audio_ring_buffer) return;

    // --- Lock-Free Read Start ---
    int tail = atomic_load_explicit(&dec->rb_tail, memory_order_relaxed);
    int head = atomic_load_explicit(&dec->rb_head, memory_order_acquire);
    
    int available = rb_read_available(dec, head, tail);
    
    if (available > 0) {
        // len_samples 是 stereo float 个数 (如 1024 frames * 2 channels = 2048)
        int to_read = (available > len_samples) ? len_samples : available;
        
        int idx = tail;
        // 简单循环混音，编译器通常会自动向量化(SIMD)
        for (int i = 0; i < to_read; i++) {
            stream[i] += dec->audio_ring_buffer[idx] * volume;
            idx++;
            if (idx == dec->rb_capacity) idx = 0;
        }
        
        // 更新 Tail，通知生产者有空位了
        atomic_store_explicit(&dec->rb_tail, idx, memory_order_release);
    }
    // --- Lock-Free Read End ---
}