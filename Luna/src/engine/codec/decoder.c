// src/engine/codec/decoder.c

#include "decoder.h"
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>

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
    // 标识
    ObjClip* clip_ref;
    char* file_path_copy;

    // 线程与同步
    SDL_Thread* thread;
    SDL_mutex* mutex;
    SDL_cond* cond_can_produce;
    bool thread_running;
    
    // Seek 控制
    bool seek_requested;
    double seek_target_time;

    // 队列
    FrameQueue video_queue;

    // 音频环形缓冲
    float* audio_ring_buffer;
    i32 rb_capacity;
    i32 rb_head; // Write
    i32 rb_tail; // Read
    i32 rb_count;

    // 视频状态 (Main Thread)
    GLuint tex_y, tex_u, tex_v;
    double current_pts;
    bool texture_ready;
    bool active_this_frame;
    
    // 基础 PTS 修正
    int64_t start_pts;
    bool has_start_pts;

    // FFmpeg Contexts
    AVFormatContext* fmt_ctx;
    AVCodecContext* vid_ctx;
    AVCodecContext* aud_ctx;
    SwrContext* swr_ctx;
    i32 video_stream_idx;
    i32 audio_stream_idx;
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
    
    dec->fmt_ctx = avformat_alloc_context();
    if (avformat_open_input(&dec->fmt_ctx, dec->file_path_copy, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(dec->fmt_ctx, NULL);

    // Video Setup
    dec->video_stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (dec->video_stream_idx >= 0) {
        AVStream* st = dec->fmt_ctx->streams[dec->video_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        dec->vid_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec->vid_ctx, st->codecpar);
        avcodec_open2(dec->vid_ctx, codec, NULL);
    }

    // Audio Setup
    dec->audio_stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (dec->audio_stream_idx >= 0) {
        AVStream* st = dec->fmt_ctx->streams[dec->audio_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        dec->aud_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec->aud_ctx, st->codecpar);
        if (avcodec_open2(dec->aud_ctx, codec, NULL) == 0) {
            AVChannelLayout out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
            swr_alloc_set_opts2(&dec->swr_ctx, &out_layout, AV_SAMPLE_FMT_FLT, MIX_SAMPLE_RATE,
                &st->codecpar->ch_layout, st->codecpar->format, st->codecpar->sample_rate, 0, NULL);
            swr_init(dec->swr_ctx);
        }
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
            dec->rb_head = dec->rb_tail = dec->rb_count = 0;
        }
        SDL_UnlockMutex(dec->mutex);

        if (seeking) {
            int64_t ts = (int64_t)(seek_tgt * AV_TIME_BASE);
            av_seek_frame(dec->fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
            if (dec->vid_ctx) avcodec_flush_buffers(dec->vid_ctx);
            if (dec->aud_ctx) avcodec_flush_buffers(dec->aud_ctx);
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
        int ret = av_read_frame(dec->fmt_ctx, pkt);
        if (ret < 0) {
            SDL_Delay(10); 
            continue;
        }

        // Process Video
        if (pkt->stream_index == dec->video_stream_idx && dec->vid_ctx) {
            if (avcodec_send_packet(dec->vid_ctx, pkt) == 0) {
                while (avcodec_receive_frame(dec->vid_ctx, frame) == 0) {
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
                        pts = (pts_val - dec->start_pts) * av_q2d(dec->fmt_ctx->streams[dec->video_stream_idx]->time_base);
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
        else if (pkt->stream_index == dec->audio_stream_idx && dec->aud_ctx && dec->swr_ctx) {
             if (avcodec_send_packet(dec->aud_ctx, pkt) == 0) {
                 while (avcodec_receive_frame(dec->aud_ctx, frame) == 0) {
                     uint8_t* out_data[2] = {0};
                     int out_samples = av_rescale_rnd(swr_get_delay(dec->swr_ctx, dec->aud_ctx->sample_rate) + frame->nb_samples,
                                                      MIX_SAMPLE_RATE, dec->aud_ctx->sample_rate, AV_ROUND_UP);
                     av_samples_alloc(out_data, NULL, 2, out_samples, AV_SAMPLE_FMT_FLT, 0);
                     int len = swr_convert(dec->swr_ctx, out_data, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
                     
                     if (len > 0) {
                         SDL_LockMutex(dec->mutex);
                         int floats_to_write = len * 2;
                         int available = dec->rb_capacity - dec->rb_count;
                         if (available >= floats_to_write) {
                             float* raw = (float*)out_data[0];
                             for (int i=0; i<floats_to_write; i++) {
                                 dec->audio_ring_buffer[dec->rb_head] = raw[i];
                                 dec->rb_head = (dec->rb_head + 1) % dec->rb_capacity;
                             }
                             dec->rb_count += floats_to_write;
                         }
                         SDL_UnlockMutex(dec->mutex);
                     }
                     av_freep(&out_data[0]);
                 }
             }
        }
        av_packet_unref(pkt);
    }
    
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (dec->vid_ctx) avcodec_free_context(&dec->vid_ctx);
    if (dec->aud_ctx) avcodec_free_context(&dec->aud_ctx);
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    avformat_close_input(&dec->fmt_ctx);
    return 0;
}

// --- Public API Implementation ---

Decoder* decoder_create(ObjClip* clip) {
    Decoder* dec = (Decoder*)malloc(sizeof(Decoder));
    memset(dec, 0, sizeof(Decoder));
    
    dec->clip_ref = clip;
    dec->file_path_copy = strdup(clip->path->chars); // 使用 strdup
    dec->mutex = SDL_CreateMutex();
    dec->cond_can_produce = SDL_CreateCond();
    
    dec->rb_capacity = AUDIO_RB_SIZE;
    dec->audio_ring_buffer = (float*)malloc(sizeof(float) * dec->rb_capacity);
    
    // GL Texture Init
    glGenTextures(1, &dec->tex_y);
    glGenTextures(1, &dec->tex_u);
    glGenTextures(1, &dec->tex_v);
    
    GLenum params[] = {dec->tex_y, dec->tex_u, dec->tex_v};
    for(int i=0; i<3; i++) {
        glBindTexture(GL_TEXTURE_2D, params[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

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
    
    glDeleteTextures(1, &dec->tex_y);
    glDeleteTextures(1, &dec->tex_u);
    glDeleteTextures(1, &dec->tex_v);
    
    fq_clear(&dec->video_queue);
    free(dec->audio_ring_buffer);
    free(dec->file_path_copy);
    free(dec);
}

bool decoder_update_video(Decoder* dec, double timeline_time) {
    SDL_LockMutex(dec->mutex);
    
    double diff = timeline_time - dec->current_pts;
    if (diff < -0.1 || diff > 1.0) {
        dec->seek_requested = true;
        dec->seek_target_time = timeline_time;
        dec->current_pts = timeline_time;
        SDL_CondSignal(dec->cond_can_produce);
        SDL_UnlockMutex(dec->mutex);
        return false;
    }
    
    AVFrame* best_frame = NULL;
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

    if (best_frame) {
        if (dec->clip_ref->width == 0) {
            dec->clip_ref->width = best_frame->width;
            dec->clip_ref->height = best_frame->height;
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, dec->tex_y);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, best_frame->linesize[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, best_frame->width, best_frame->height, 
                     0, GL_RED, GL_UNSIGNED_BYTE, best_frame->data[0]);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, dec->tex_u);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, best_frame->linesize[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, best_frame->width/2, best_frame->height/2, 
                     0, GL_RED, GL_UNSIGNED_BYTE, best_frame->data[1]);
        
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, dec->tex_v);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, best_frame->linesize[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, best_frame->width/2, best_frame->height/2, 
                     0, GL_RED, GL_UNSIGNED_BYTE, best_frame->data[2]);
        
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); 
        
        dec->texture_ready = true;
        av_frame_free(&best_frame);
    }
    return dec->texture_ready;
}

GLuint decoder_get_texture_y(Decoder* dec) { return dec->tex_y; }
GLuint decoder_get_texture_u(Decoder* dec) { return dec->tex_u; }
GLuint decoder_get_texture_v(Decoder* dec) { return dec->tex_v; }

ObjClip* decoder_get_clip_ref(Decoder* dec) { return dec->clip_ref; }

void decoder_set_active(Decoder* dec, bool active) {
    dec->active_this_frame = active;
}

void decoder_mix_audio(Decoder* dec, float* stream, int len_samples, float volume) {
    if (SDL_TryLockMutex(dec->mutex) == 0) {
        if (dec->rb_count > 0) {
            int read_amt = (dec->rb_count > len_samples) ? len_samples : dec->rb_count;
            for (int k = 0; k < read_amt; k++) {
                stream[k] += dec->audio_ring_buffer[dec->rb_tail] * volume;
                dec->rb_tail = (dec->rb_tail + 1) % dec->rb_capacity;
            }
            dec->rb_count -= read_amt;
        }
        SDL_UnlockMutex(dec->mutex);
    }
}