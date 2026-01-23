// src/engine/codec/decoder.c

#include "decoder.h"
#include "engine/media/utils/ffmpeg_utils.h" // [新增]
#include <libswresample/swresample.h>
#include <stdatomic.h> // [新增]
#include <SDL2/SDL.h> // 仅用于 Thread 和 Mutex，不用于渲染

#define MAX_QUEUE_SIZE 8
#define AUDIO_RB_SIZE 131072
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
    // ... (video_queue, clip_ref 等保持不变) ...
    Clip* clip_ref;
    char* file_path_copy;
    SDL_Thread* thread;
    SDL_mutex* mutex;
    SDL_cond* cond_can_produce;
    bool thread_running;
    bool seek_requested;
    double seek_target_time;
    FrameQueue video_queue;
    
    // [修改] 视频帧缓存
    AVFrame* current_frame_cpu;
    
    // [修改] 音频环形缓冲 (SPSC Lock-Free)
    float* audio_ring_buffer;
    int32_t rb_capacity;
    atomic_int rb_head; // 原子类型: 写指针
    atomic_int rb_tail; // 原子类型: 读指针
    // 移除: int32_t rb_count; (不再需要，通过 head - tail 计算)

    // ... (其他状态保持不变) ...
    double current_pts;
    bool active_this_frame;
    int64_t start_pts;
    bool has_start_pts;
    MediaContext media; 
    SwrContext* swr_ctx;
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

// --- Thread Logic ---

static int decoder_thread_func(void* data) {
    Decoder* dec = (Decoder*)data;
    
    // [修改] 使用工具函数统一打开视频和音频
    if (!media_open(&dec->media, dec->file_path_copy, true, true)) {
        fprintf(stderr, "[Decoder] Failed to open media: %s\n", dec->file_path_copy);
        return -1;
    }

    // [修改] 额外的音频 SWR 初始化逻辑 (仅在有音频流时执行)
    if (dec->media.aud_ctx) {
        AVCodecContext* aud_ctx = dec->media.aud_ctx;
        AVChannelLayout out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        
        swr_alloc_set_opts2(&dec->swr_ctx, &out_layout, AV_SAMPLE_FMT_FLT, MIX_SAMPLE_RATE,
            &aud_ctx->ch_layout, aud_ctx->sample_fmt, aud_ctx->sample_rate, 0, NULL);
        swr_init(dec->swr_ctx);
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (dec->thread_running) {
        // Handle Seek
        bool seeking = false;
        double seek_tgt = 0;
        
        SDL_LockMutex(dec->mutex);
        if (dec->seek_requested) {
            seeking = true;
            seek_tgt = dec->seek_target_time;
            dec->seek_requested = false;
            fq_clear(&dec->video_queue);
            
            // [修改] 重置原子索引
            atomic_store(&dec->rb_head, 0);
            atomic_store(&dec->rb_tail, 0);
        }
        SDL_UnlockMutex(dec->mutex);

        if (seeking) {
            int64_t ts = (int64_t)(seek_tgt * AV_TIME_BASE);
            // [修改] 使用 media.fmt_ctx
            av_seek_frame(dec->media.fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
            if (dec->media.vid_ctx) avcodec_flush_buffers(dec->media.vid_ctx);
            if (dec->media.aud_ctx) avcodec_flush_buffers(dec->media.aud_ctx);
        }

        // Check Queue Capacity
        SDL_LockMutex(dec->mutex);
        bool queue_full = (dec->video_queue.count >= MAX_QUEUE_SIZE);
        SDL_UnlockMutex(dec->mutex);

        if (queue_full) {
            SDL_LockMutex(dec->mutex);
            SDL_CondWaitTimeout(dec->cond_can_produce, dec->mutex, 20);
            SDL_UnlockMutex(dec->mutex);
            continue;
        }

        // Read Frame
        // [修改] 使用 media.fmt_ctx
        int ret = av_read_frame(dec->media.fmt_ctx, pkt);
        if (ret < 0) {
            SDL_Delay(10); 
            continue;
        }

        // Process Video
        // [修改] 使用 media 字段
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
                        // [修改] 使用 media.vid_stream
                        pts = (pts_val - dec->start_pts) * av_q2d(dec->media.vid_stream->time_base);
                    } else if (dec->video_queue.tail) {
                        pts = dec->video_queue.tail->pts + 0.033;
                    }
                    if (pts < 0) pts = 0;
                    
                    fq_push(&dec->video_queue, cloned, pts);
                    SDL_UnlockMutex(dec->mutex);
                }
            }
        }
        // Process Audio
        // [修改] 使用 media 字段
        else if (pkt->stream_index == dec->media.aud_stream_idx && dec->media.aud_ctx && dec->swr_ctx) {
             if (avcodec_send_packet(dec->media.aud_ctx, pkt) == 0) {
                 while (avcodec_receive_frame(dec->media.aud_ctx, frame) == 0) {
                     uint8_t* out_data[2] = {0};
                     int out_samples = av_rescale_rnd(swr_get_delay(dec->swr_ctx, dec->media.aud_ctx->sample_rate) + frame->nb_samples,
                                                      MIX_SAMPLE_RATE, dec->media.aud_ctx->sample_rate, AV_ROUND_UP);
                     av_samples_alloc(out_data, NULL, 2, out_samples, AV_SAMPLE_FMT_FLT, 0);
                     int len = swr_convert(dec->swr_ctx, out_data, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
                     
                     if (len > 0) {
                         // [修改] 无锁写入逻辑
                         // 不需要 SDL_LockMutex(dec->mutex)
                         
                         int floats_to_write = len * 2; // Stereo
                         float* raw = (float*)out_data[0];
                         
                         // 1. 获取当前索引快照
                         int head = atomic_load_explicit(&dec->rb_head, memory_order_relaxed);
                         int tail = atomic_load_explicit(&dec->rb_tail, memory_order_acquire);
                         
                         // 2. 计算剩余空间 (保留一个空位以区分满和空)
                         // Free = (Tail - Head + Cap - 1) % Cap
                         int free_space = (tail - head + dec->rb_capacity - 1) % dec->rb_capacity;
                         
                         if (free_space >= floats_to_write) {
                             // 3. 写入数据 (处理回绕)
                             int chunk1 = dec->rb_capacity - head;
                             if (chunk1 >= floats_to_write) {
                                 memcpy(dec->audio_ring_buffer + head, raw, floats_to_write * sizeof(float));
                             } else {
                                 memcpy(dec->audio_ring_buffer + head, raw, chunk1 * sizeof(float));
                                 memcpy(dec->audio_ring_buffer, raw + chunk1, (floats_to_write - chunk1) * sizeof(float));
                             }
                             
                             // 4. 更新 Head (发布数据)
                             int new_head = (head + floats_to_write) % dec->rb_capacity;
                             atomic_store_explicit(&dec->rb_head, new_head, memory_order_release);
                         } 
                         // else: 空间不足，丢弃数据 (Drop) - 这比阻塞音频线程要好
                     }
                     av_freep(&out_data[0]);
                 }
             }
        }
        av_packet_unref(pkt);
    }
    
    av_frame_free(&frame);
    av_packet_free(&pkt);
    
    // [修改] 统一释放 FFmpeg 上下文
    media_close(&dec->media);
    
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    return 0;
}
// --- Public API Implementation ---

Decoder* decoder_create(Clip* clip) {
    Decoder* dec = (Decoder*)malloc(sizeof(Decoder));
    memset(dec, 0, sizeof(Decoder));
    media_ctx_init(&dec->media);
    
    dec->clip_ref = clip;
    dec->file_path_copy = strdup(clip->path); 
    dec->mutex = SDL_CreateMutex();
    dec->cond_can_produce = SDL_CreateCond();
    
    dec->rb_capacity = AUDIO_RB_SIZE;
    dec->audio_ring_buffer = (float*)malloc(sizeof(float) * dec->rb_capacity);
    
    // [新增] 原子初始化
    atomic_init(&dec->rb_head, 0);
    atomic_init(&dec->rb_tail, 0);
    
    // [修改] 不再初始化 GL 纹理
    dec->current_frame_cpu = NULL;

    dec->thread_running = true;
    dec->thread = SDL_CreateThread(decoder_thread_func, "DecoderThread", dec);
    
    return dec;
}

void decoder_destroy(Decoder* dec) {
    if (!dec) return;
    
    dec->thread_running = false;
    SDL_CondSignal(dec->cond_can_produce);
    SDL_WaitThread(dec->thread, NULL);
    
    SDL_DestroyMutex(dec->mutex);
    SDL_DestroyCond(dec->cond_can_produce);
    
    // [修改] 释放 CPU 帧缓存
    if (dec->current_frame_cpu) av_frame_free(&dec->current_frame_cpu);
    // 移除: glDeleteTextures...
    
    fq_clear(&dec->video_queue);
    free(dec->audio_ring_buffer);
    free(dec->file_path_copy);
    free(dec);
}

bool decoder_update_video(Decoder* dec, double timeline_time) {
    SDL_LockMutex(dec->mutex);
    
    // Seek 检查逻辑 (保持不变)
    double diff = timeline_time - dec->current_pts;
    if (diff < -0.1 || diff > 1.0) {
        dec->seek_requested = true;
        dec->seek_target_time = timeline_time;
        dec->current_pts = timeline_time;
        SDL_CondSignal(dec->cond_can_produce);
        SDL_UnlockMutex(dec->mutex);
        return false; // Seek 发生，画面未就绪
    }
    
    AVFrame* best_frame = NULL;
    // 队列消耗逻辑 (保持不变)
    while (dec->video_queue.head) {
        double f_pts = dec->video_queue.head->pts;
        if (f_pts < timeline_time - 0.05) {
            AVFrame* drop = fq_pop(&dec->video_queue, NULL);
            av_frame_free(&drop);
            SDL_CondSignal(dec->cond_can_produce);
        } else if (f_pts <= timeline_time + 0.05) {
            if (best_frame) av_frame_free(&best_frame);
            best_frame = fq_pop(&dec->video_queue, &dec->current_pts);
            SDL_CondSignal(dec->cond_can_produce);
        } else {
            break;
        }
    }
    SDL_UnlockMutex(dec->mutex);

    // [修改] 核心变化：只更新 CPU 帧指针，不进行 GL 上传
    if (best_frame) {
        if (dec->clip_ref->width == 0) {
            dec->clip_ref->width = best_frame->width;
            dec->clip_ref->height = best_frame->height;
        }

        // 替换当前持有的帧
        if (dec->current_frame_cpu) {
            av_frame_free(&dec->current_frame_cpu);
        }
        dec->current_frame_cpu = best_frame;
        return true; // 告诉 Compositor：有新数据了
    }
    
    return false; // 无新数据，保持上一帧
}

// [新增] 仅暴露数据指针
bool decoder_get_video_data(Decoder* dec, uint8_t* data[3], int linesize[3], int* width, int* height) {
    if (!dec || !dec->current_frame_cpu) return false;
    
    AVFrame* f = dec->current_frame_cpu;
    for (int i=0; i<3; i++) {
        data[i] = f->data[i];
        linesize[i] = f->linesize[i];
    }
    
    // 输出宽高
    if (width) *width = f->width;
    if (height) *height = f->height;
    
    return true;
}

// GLuint decoder_get_texture_y(Decoder* dec) { return dec->tex_y; }
// GLuint decoder_get_texture_u(Decoder* dec) { return dec->tex_u; }
// GLuint decoder_get_texture_v(Decoder* dec) { return dec->tex_v; }

Clip* decoder_get_clip_ref(Decoder* dec) { return dec->clip_ref; }

void decoder_set_active(Decoder* dec, bool active) {
    dec->active_this_frame = active;
}

void decoder_mix_audio(Decoder* dec, float* stream, int len_samples, float volume) {
    if (!dec || !dec->audio_ring_buffer) return;

    // [修改] 无锁读取逻辑
    // 1. 获取快照
    int tail = atomic_load_explicit(&dec->rb_tail, memory_order_relaxed);
    int head = atomic_load_explicit(&dec->rb_head, memory_order_acquire);
    
    // 2. 计算可用数据
    // Available = (Head - Tail + Cap) % Cap
    int available = (head - tail + dec->rb_capacity) % dec->rb_capacity;
    
    if (available > 0) {
        int read_amt = (available > len_samples) ? len_samples : available;
        
        // 3. 读取并混合
        // 由于需要 += 混音，且涉及回绕，这里用循环简单处理，
        // 或者分两段处理 (如果性能敏感，可以用 SIMD 优化这里)
        int idx = tail;
        for (int k = 0; k < read_amt; k++) {
            stream[k] += dec->audio_ring_buffer[idx] * volume;
            idx++;
            if (idx == dec->rb_capacity) idx = 0;
        }
        
        // 4. 更新 Tail (消费数据)
        atomic_store_explicit(&dec->rb_tail, idx, memory_order_release);
    }
}