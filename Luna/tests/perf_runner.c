#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include "core/chunk.h"
#include "core/compiler/compiler.h"
#include "core/memory.h"
#include "core/object.h"
#include "core/value.h"
#include "core/vm/vm.h"
#include "engine/bridge/object.h"
#include "engine/effect/registry.h"
#include "engine/engine.h"
#include "engine/media/codec/decoder.h"
#include "engine/media/utils/image_loader.h"
#include "engine/model/animation.h"
#include "engine/model/clip.h"
#include "engine/model/timeline.h"
#include "engine/render/compositor.h"
#include "engine/service/exporter.h"
#include "engine/service/transcoder.h"

void registerStdBindings(VM* vm);
void registerVideoBindings(VM* vm);

typedef enum {
    BENCH_OK,
    BENCH_SKIP,
    BENCH_FAIL
} BenchStatus;

typedef struct {
    VM vm;
    EngineContext engine_ctx;
    Chunk chunk;
    bool vm_initialized;
} ScriptPerfSession;

typedef struct {
    SDL_Window* window;
    SDL_GLContext context;
    bool ready;
} GLBenchContext;

static int g_failures = 0;
static int g_skips = 0;

static const char* PERF_VIDEO_FIXTURE = "/tmp/luna_perf_fixture_input.mp4";
static const char* PERF_TRANSCODE_OUTPUT = "/tmp/luna_perf_transcoded.mp4";
static const char* PERF_EXPORT_OUTPUT = "/tmp/luna_perf_export.mp4";

static const char* PERF_STDLIB_SCRIPT =
    "var total = 0\n"
    "var i = 0\n"
    "while i < 200:\n"
    "    total = total + i\n"
    "    i = i + 1\n"
    "var doubled = List()\n"
    "i = 0\n"
    "while i < 200:\n"
    "    push(doubled, i * 2)\n"
    "    i = i + 1\n"
    "var d = Dict()\n"
    "dict_put(d, \"name\", \"luna\")\n"
    "dict_put(d, \"lang\", \"video\")\n"
    "var idx = 0\n"
    "while idx < len(doubled):\n"
    "    idx = idx + 1\n"
    "fun makeAdder(x):\n"
    "    fun add(y):\n"
    "        return x + y\n"
    "    return add\n"
    "var add7 = makeAdder(7)\n"
    "class Base:\n"
    "    fun greet():\n"
    "        return total\n"
    "class Child < Base:\n"
    "    fun greet():\n"
    "        return super.greet() + add7(3)\n"
    "var c = Child()\n"
    "var result = c.greet()\n"
    "try:\n"
    "    var safe = result + len(dict_keys(d))\n"
    "except:\n"
    "    var safe = -1\n";

static const char* PERF_VIDEO_SCRIPT =
    "var tl = Timeline(640, 360, 24)\n"
    "var image = Image(\"tests/assets/test_image.ppm\")\n"
    "var solid = Solid(320, 180, 10, 20, 30, 255)\n"
    "var adjust = Adjustment(640, 360)\n"
    "adjust.setAffectsWholeFrame(true)\n"
    "var group = Group(320, 180, 24)\n"
    "var groupTl = group.getTimeline()\n"
    "groupTl.add(0, Solid(320, 180, 80, 40, 20, 255), 0)\n"
    "var precomp = Precomp(160, 90, 24)\n"
    "var preTl = Timeline(160, 90, 24)\n"
    "preTl.add(0, Solid(160, 90, 20, 40, 80, 255), 0)\n"
    "precomp.setTimeline(preTl)\n"
    "var imgInst = tl.add(0, image, 0)\n"
    "var solidInst = tl.add(0, solid, 0.25)\n"
    "var groupInst = tl.add(1, group, 0.5)\n"
    "var preInst = tl.add(1, precomp, 0.75)\n"
    "var adjustInst = tl.add(2, adjust, 0)\n"
    "adjustInst.setDuration(2)\n"
    "var mosaic = Mosaic(blockSize=8, sharpColors=true)\n"
    "imgInst.addEffect(mosaic)\n"
    "var fill = Fill(amount=0.5, color=[255, 120, 30, 255])\n"
    "solidInst.addEffect(fill)\n"
    "var grade = BrightnessContrast(brightness=0.05, contrast=1.15)\n"
    "adjustInst.addEffect(grade)\n";

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool file_exists_nonempty(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

static bool ensure_real_video_fixture(void) {
    const char* cmd =
        "ffmpeg -loglevel error -y "
        "-f lavfi -i \"testsrc2=size=320x180:rate=24:duration=2\" "
        "-f lavfi -i \"sine=frequency=880:sample_rate=44100:duration=2\" "
        "-shortest -c:v mpeg4 -q:v 5 -pix_fmt yuv420p -c:a aac "
        "/tmp/luna_perf_fixture_input.mp4 >/dev/null 2>&1";

    if (file_exists_nonempty(PERF_VIDEO_FIXTURE)) {
        return true;
    }
    return system(cmd) == 0 && file_exists_nonempty(PERF_VIDEO_FIXTURE);
}

static void report_benchmark(const char* name, uint64_t total_ns, uint64_t iterations, uint64_t work_units, const char* unit_label) {
    double total_ms = (double)total_ns / 1000000.0;
    double iter_ms = iterations ? total_ms / (double)iterations : 0.0;
    double units_per_sec = total_ns ? ((double)work_units * 1000000000.0) / (double)total_ns : 0.0;
    printf("[PERF] %-34s total=%8.3f ms  iter=%8.4f ms  throughput=%10.2f %s/s\n",
           name, total_ms, iter_ms, units_per_sec, unit_label);
    fflush(stdout);
}

static void bench_fail(const char* name, const char* reason) {
    fprintf(stderr, "[FAIL] %s: %s\n", name, reason);
    g_failures++;
}

static void bench_skip(const char* name, const char* reason) {
    printf("[SKIP] %s: %s\n", name, reason);
    g_skips++;
}

static void* perf_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size) {
    (void)ctx;
    (void)old_size;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }

    void* result = realloc(ptr, new_size);
    if (!result) {
        fprintf(stderr, "perf realloc failed\n");
        exit(1);
    }
    return result;
}

static void perf_mark_roots(VM* vm) {
    EngineContext* ctx = (EngineContext*)vm->user_data;
    if (ctx && ctx->active_project_obj) {
        markObject(vm, (Obj*)ctx->active_project_obj);
    }
}

static void script_perf_session_init(ScriptPerfSession* session, bool include_video_bindings) {
    memset(session, 0, sizeof(*session));
    initChunk(&session->chunk);
    initVM(&session->vm);
    session->vm_initialized = true;
    session->vm.user_data = &session->engine_ctx;
    session->vm.host_mark_roots = perf_mark_roots;
    registerStdBindings(&session->vm);
    if (include_video_bindings) {
        registerVideoBindings(&session->vm);
    }
}

static bool script_perf_session_run(ScriptPerfSession* session, const char* source) {
    bool compiled = compile(&session->vm, source, &session->chunk);
    if (!compiled) return false;
    return interpret(&session->vm, &session->chunk) == INTERPRET_OK;
}

static void script_perf_session_dispose(ScriptPerfSession* session) {
    if (session->vm_initialized) {
        freeChunk(&session->vm, &session->chunk);
        freeVM(&session->vm);
    }
    memset(session, 0, sizeof(*session));
}

static BenchStatus benchmark_clip_construction(void) {
    const uint64_t iterations = 20000;
    uint64_t start = now_ns();

    for (uint64_t i = 0; i < iterations; i++) {
        Clip* media = clip_create_media("demo.mp4");
        Clip* image = clip_create_image("tests/assets/test_image.ppm", 2, 2);
        Clip* text = clip_create_text("Hello", "arial.ttf", 32, 255, 255, 255);
        Clip* solid = clip_create_solid(320, 180, 10, 20, 30, 255);
        Clip* adjustment = clip_create_adjustment(640, 360);
        Clip* group = clip_create_group(320, 180, 24.0);
        Clip* precomp = clip_create_precomp(160, 90, 12.0);
        clip_free(media);
        clip_free(image);
        clip_free(text);
        clip_free(solid);
        clip_free(adjustment);
        clip_free(group);
        clip_free(precomp);
    }

    report_benchmark("clip constructors/free", now_ns() - start, iterations, iterations * 7, "clips");
    return BENCH_OK;
}

static BenchStatus benchmark_image_loader_perf(void) {
    const uint64_t iterations = 300;
    uint64_t start = now_ns();

    for (uint64_t i = 0; i < iterations; i++) {
        uint8_t* pixels = NULL;
        int width = 0;
        int height = 0;
        if (!image_load_rgba("tests/assets/test_image.ppm", &pixels, &width, &height)) {
            bench_fail("image loader", "failed to load tests/assets/test_image.ppm");
            return BENCH_FAIL;
        }
        free(pixels);
    }

    report_benchmark("image loader", now_ns() - start, iterations, iterations, "loads");
    return BENCH_OK;
}

static BenchStatus benchmark_animation_eval_perf(void) {
    const uint64_t keyframes = 128;
    const uint64_t samples = 250000;
    Allocator allocator = { perf_realloc, NULL };
    Animation anim;
    volatile double sink = 0.0;

    init_animation(&anim, &allocator, 0.0);
    for (uint64_t i = 0; i < keyframes; i++) {
        add_keyframe(&anim, &allocator, (double)i * 0.1, sin((double)i * 0.17) * 100.0, KEYFRAME_BEZIER, 0.35);
    }

    uint64_t start = now_ns();
    for (uint64_t i = 0; i < samples; i++) {
        sink += evaluate_animation(&anim, ((double)(i % 512)) * 0.025);
    }
    report_benchmark("animation evaluate", now_ns() - start, samples, samples, "evals");
    free_animation(&anim, &allocator);
    (void)sink;
    return BENCH_OK;
}

static BenchStatus benchmark_timeline_ops_perf(void) {
    const uint64_t clip_count = 1500;
    const uint64_t query_count = 25000;
    VM vm;
    Allocator allocator;
    Timeline* tl;
    uint64_t start;
    volatile double sink = 0.0;

    initVM(&vm);
    init_allocator(&allocator, &vm);
    tl = timeline_create(&allocator, 1280, 720, 30.0);
    for (int i = 0; i < 6; i++) {
        timeline_add_track(tl);
    }

    start = now_ns();
    for (uint64_t i = 0; i < clip_count; i++) {
        Clip* solid = clip_create_solid(64, 64, (u8)(i % 255), (u8)((i * 2) % 255), (u8)((i * 3) % 255), 255);
        timeline_add_clip(tl, (i32)(i % 6), solid, (double)i * 0.05);
    }
    for (uint64_t i = 0; i < query_count; i++) {
        Track* track = &tl->tracks[i % tl->track_count];
        TimelineClip* clip = timeline_get_clip_at(track, ((double)(i % 500)) * 0.1);
        if (clip) sink += clip->timeline_start;
    }
    for (uint64_t i = 1; i < 200; i++) {
        timeline_duplicate_clip_by_id(tl, (u32)i, (i32)(i % 6), (double)i * 0.07);
        timeline_move_clip_by_id(tl, (u32)i, (i32)((i + 1) % 6), (double)i * 0.09);
    }
    for (uint64_t i = 1; i < 120; i++) {
        timeline_remove_clip_by_id(tl, (u32)i);
    }
    report_benchmark("timeline add/query/mutate", now_ns() - start, clip_count + query_count, clip_count + query_count + 320, "ops");

    timeline_free(tl);
    freeVM(&vm);
    (void)sink;
    return BENCH_OK;
}

static BenchStatus benchmark_compile_stdlib_perf(void) {
    const uint64_t iterations = 220;
    VM vm;
    uint64_t start;

    initVM(&vm);
    registerStdBindings(&vm);

    start = now_ns();
    for (uint64_t i = 0; i < iterations; i++) {
        Chunk chunk;
        initChunk(&chunk);
        if (!compile(&vm, PERF_STDLIB_SCRIPT, &chunk)) {
            freeChunk(&vm, &chunk);
            freeVM(&vm);
            bench_fail("compile stdlib script", "compile failed");
            return BENCH_FAIL;
        }
        freeChunk(&vm, &chunk);
    }
    report_benchmark("compile stdlib script", now_ns() - start, iterations, iterations, "compiles");
    freeVM(&vm);
    return BENCH_OK;
}

static BenchStatus benchmark_end_to_end_stdlib_perf(void) {
    const uint64_t iterations = 90;
    uint64_t start = now_ns();

    for (uint64_t i = 0; i < iterations; i++) {
        ScriptPerfSession session;
        script_perf_session_init(&session, false);
        if (!script_perf_session_run(&session, PERF_STDLIB_SCRIPT)) {
            script_perf_session_dispose(&session);
            bench_fail("stdlib script end-to-end", "compile or interpret failed");
            return BENCH_FAIL;
        }
        script_perf_session_dispose(&session);
    }
    report_benchmark("stdlib script end-to-end", now_ns() - start, iterations, iterations, "runs");
    return BENCH_OK;
}

static BenchStatus benchmark_video_bindings_perf(void) {
    const uint64_t iterations = 50;
    uint64_t start = now_ns();

    for (uint64_t i = 0; i < iterations; i++) {
        ScriptPerfSession session;
        script_perf_session_init(&session, true);
        if (!script_perf_session_run(&session, PERF_VIDEO_SCRIPT)) {
            script_perf_session_dispose(&session);
            bench_fail("video binding script", "compile or interpret failed");
            return BENCH_FAIL;
        }
        script_perf_session_dispose(&session);
    }

    report_benchmark("video binding script", now_ns() - start, iterations, iterations, "runs");
    return BENCH_OK;
}

static BenchStatus benchmark_effect_graph_perf(void) {
    const uint64_t iterations = 180000;
    VM vm;
    Allocator allocator;
    Timeline* tl;
    TimelineClip* target;
    TimelineClip* source;
    EffectInstance* slider;
    EffectInstance* post;
    EffectInstance* color_ctl;
    EffectInstance* fill;
    uint64_t start;

    initVM(&vm);
    init_allocator(&allocator, &vm);
    effect_registry_init(&vm);
    effect_register_builtin_processors();

    tl = timeline_create(&allocator, 640, 360, 30.0);
    timeline_add_track(tl);
    timeline_add_clip(tl, 0, clip_create_solid(320, 180, 10, 20, 30, 255), 0.0);
    timeline_add_clip(tl, 0, clip_create_solid(320, 180, 40, 50, 60, 255), 0.5);
    target = timeline_get_clip(tl, 0, 0);
    source = timeline_get_clip(tl, 0, 1);

    slider = effect_chain_append(&allocator, &source->effectChain, effect_registry_get("SliderControl"), NULL, 0);
    post = effect_chain_append(&allocator, &target->effectChain, effect_registry_get("Posterize"), NULL, 0);
    color_ctl = effect_chain_append(&allocator, &source->effectChain, effect_registry_get("ColorControl"), NULL, 0);
    fill = effect_chain_append(&allocator, &target->effectChain, effect_registry_get("Fill"), NULL, 0);
    if (!slider || !post || !color_ctl || !fill) {
        timeline_free(tl);
        freeVM(&vm);
        bench_fail("effect graph", "failed to create effects");
        return BENCH_FAIL;
    }

    slider->processor->set_number(slider->data, "value", 9.0);
    color_ctl->processor->set_color(color_ctl->data, "color", 200, 100, 50, 255);
    effect_link_number(&allocator, post, "levels", source->id, slider, "value", 0.75, 1.0);
    effect_link_color(&allocator, fill, "color", source->id, color_ctl, "color");

    add_keyframe(post->processor->get_number_animation(post->data, "levels"), &allocator, 0.0, 4.0, KEYFRAME_LINEAR, 0.0);
    add_keyframe(post->processor->get_number_animation(post->data, "levels"), &allocator, 1.0, 8.0, KEYFRAME_BEZIER, 0.2);

    start = now_ns();
    for (uint64_t i = 0; i < iterations; i++) {
        effect_apply_links(tl, post, ((double)(i % 240)) / 60.0);
        effect_apply_links(tl, fill, ((double)(i % 240)) / 60.0);
    }
    report_benchmark("effect graph update", now_ns() - start, iterations, iterations * 2, "link-updates");

    timeline_free(tl);
    freeVM(&vm);
    return BENCH_OK;
}

static bool init_hidden_gl(GLBenchContext* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    ctx->window = SDL_CreateWindow("luna-perf",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   64, 64,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!ctx->window) {
        SDL_Quit();
        return false;
    }

    ctx->context = SDL_GL_CreateContext(ctx->window);
    if (!ctx->context) {
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(ctx->context);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return false;
    }

    ctx->ready = true;
    return true;
}

static void free_hidden_gl(GLBenchContext* ctx) {
    if (!ctx->ready) return;
    SDL_GL_DeleteContext(ctx->context);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
    memset(ctx, 0, sizeof(*ctx));
}

static BenchStatus benchmark_compositor_basic_perf(void) {
    const uint64_t frames = 120;
    GLBenchContext glctx;
    VM vm;
    Allocator allocator;
    Timeline* tl;
    Compositor* comp;
    TimelineClip* img_tc;
    TimelineClip* adj_tc;
    uint64_t start;

    if (!init_hidden_gl(&glctx)) {
        bench_skip("compositor basic render", "OpenGL context unavailable");
        return BENCH_SKIP;
    }

    initVM(&vm);
    init_allocator(&allocator, &vm);
    effect_registry_init(&vm);
    effect_register_builtin_processors();

    tl = timeline_create(&allocator, 640, 360, 30.0);
    timeline_add_track(tl);
    timeline_add_track(tl);
    timeline_add_clip(tl, 0, clip_create_image("tests/assets/test_image.ppm", 2, 2), 0.0);
    timeline_add_clip(tl, 0, clip_create_solid(640, 360, 20, 30, 40, 255), 0.25);
    timeline_add_clip(tl, 1, clip_create_adjustment(640, 360), 0.0);
    img_tc = timeline_get_clip(tl, 0, 0);
    adj_tc = timeline_get_clip(tl, 1, 0);
    adj_tc->timeline_duration = 4.0;

    effect_chain_append(&allocator, &img_tc->effectChain, effect_registry_get("Mosaic"), NULL, 0);
    if (img_tc->effectChain) {
        img_tc->effectChain->processor->set_number(img_tc->effectChain->data, "blockSize", 8.0);
        img_tc->effectChain->processor->set_bool(img_tc->effectChain->data, "sharpColors", true);
    }
    effect_chain_append(&allocator, &adj_tc->effectChain, effect_registry_get("BrightnessContrast"), NULL, 0);
    if (adj_tc->effectChain) {
        adj_tc->effectChain->processor->set_number(adj_tc->effectChain->data, "brightness", 0.05);
        adj_tc->effectChain->processor->set_number(adj_tc->effectChain->data, "contrast", 1.1);
    }

    comp = compositor_create(&vm, tl);
    start = now_ns();
    for (uint64_t i = 0; i < frames; i++) {
        compositor_render(comp, (double)i / 30.0);
    }
    report_benchmark("compositor basic render", now_ns() - start, frames, frames, "frames");

    compositor_free(&vm, comp);
    timeline_free(tl);
    freeVM(&vm);
    free_hidden_gl(&glctx);
    return BENCH_OK;
}

static BenchStatus benchmark_compositor_nested_perf(void) {
    const uint64_t frames = 2;
    GLBenchContext glctx;
    VM vm;
    Allocator allocator;
    Timeline* outer;
    Timeline* inner_group;
    Timeline* inner_precomp;
    Clip* group;
    Clip* precomp;
    Compositor* comp;
    uint64_t start;

    if (!init_hidden_gl(&glctx)) {
        bench_skip("compositor nested render", "OpenGL context unavailable");
        return BENCH_SKIP;
    }

    initVM(&vm);
    init_allocator(&allocator, &vm);
    effect_registry_init(&vm);
    effect_register_builtin_processors();

    outer = timeline_create(&allocator, 640, 360, 24.0);
    inner_group = timeline_create(&allocator, 320, 180, 24.0);
    inner_precomp = timeline_create(&allocator, 160, 90, 24.0);
    timeline_add_track(outer);
    timeline_add_track(outer);
    timeline_add_track(outer);
    timeline_add_track(outer);
    timeline_add_track(inner_group);
    timeline_add_track(inner_precomp);

    timeline_add_clip(inner_group, 0, clip_create_solid(320, 180, 80, 20, 40, 255), 0.0);
    timeline_add_clip(inner_precomp, 0, clip_create_image("tests/assets/test_image.ppm", 2, 2), 0.0);

    group = clip_create_group(320, 180, 24.0);
    precomp = clip_create_precomp(160, 90, 24.0);
    group->nested_timeline.timeline = inner_group;
    precomp->nested_timeline.timeline = inner_precomp;

    timeline_add_clip(outer, 0, clip_create_solid(640, 360, 10, 10, 20, 255), 0.0);
    timeline_add_clip(outer, 1, group, 0.25);
    timeline_add_clip(outer, 2, precomp, 0.5);
    timeline_add_clip(outer, 3, clip_create_adjustment(640, 360), 0.0);

    TimelineClip* group_tc = timeline_get_clip(outer, 1, 0);
    TimelineClip* pre_tc = timeline_get_clip(outer, 2, 0);
    TimelineClip* adj_tc = timeline_get_clip(outer, 3, 0);
    group_tc->timeline_duration = 3.0;
    pre_tc->timeline_duration = 3.0;
    adj_tc->timeline_duration = 3.0;
    effect_chain_append(&allocator, &adj_tc->effectChain, effect_registry_get("Posterize"), NULL, 0);
    if (adj_tc->effectChain) {
        adj_tc->effectChain->processor->set_number(adj_tc->effectChain->data, "levels", 6.0);
        adj_tc->effectChain->processor->set_number(adj_tc->effectChain->data, "amount", 0.6);
    }

    comp = compositor_create(&vm, outer);
    start = now_ns();
    for (uint64_t i = 0; i < frames; i++) {
        compositor_render(comp, (double)i / 24.0);
    }
    report_benchmark("compositor nested render", now_ns() - start, frames, frames, "frames");

    compositor_free(&vm, comp);
    timeline_free(inner_group);
    timeline_free(inner_precomp);
    timeline_free(outer);
    freeVM(&vm);
    free_hidden_gl(&glctx);
    return BENCH_OK;
}

static BenchStatus benchmark_decoder_real_video_perf(void) {
    const uint64_t frames = 48;
    Clip* clip;
    Decoder* dec;
    uint64_t start;

    if (!ensure_real_video_fixture()) {
        bench_skip("decoder real video", "ffmpeg fixture generation unavailable");
        return BENCH_SKIP;
    }

    clip = clip_create_media(PERF_VIDEO_FIXTURE);
    clip->width = 320;
    clip->height = 180;
    clip->fps = 24.0;
    clip->duration = 2.0;
    clip->out_point = 2.0;

    dec = decoder_create(clip);
    if (!dec) {
        clip_free(clip);
        bench_fail("decoder real video", "failed to create decoder");
        return BENCH_FAIL;
    }

    start = now_ns();
    for (uint64_t i = 0; i < frames; i++) {
        const double t = (double)i / 24.0;
        bool ready = false;
        for (int retry = 0; retry < 400; retry++) {
            uint8_t* data[3] = {0};
            int linesize[3] = {0};
            int width = 0;
            int height = 0;
            decoder_update_video(dec, t);
            if (decoder_get_video_data(dec, data, linesize, &width, &height) &&
                width == 320 && height == 180 && data[0] != NULL) {
                ready = true;
                break;
            }
            SDL_Delay(2);
        }
        if (!ready) {
            decoder_destroy(dec);
            clip_free(clip);
            bench_fail("decoder real video", "timed out waiting for decoded frame");
            return BENCH_FAIL;
        }
    }

    report_benchmark("decoder real video", now_ns() - start, frames, frames, "frames");
    decoder_destroy(dec);
    clip_free(clip);
    return BENCH_OK;
}

static BenchStatus benchmark_transcoder_real_video_perf(void) {
    VM vm;
    Clip* clip;
    uint64_t start;

    if (!ensure_real_video_fixture()) {
        bench_skip("transcoder real video", "ffmpeg fixture generation unavailable");
        return BENCH_SKIP;
    }

    unlink(PERF_TRANSCODE_OUTPUT);

    initVM(&vm);
    clip = clip_create_media(PERF_VIDEO_FIXTURE);
    clip->width = 320;
    clip->height = 180;
    clip->fps = 24.0;
    clip->duration = 2.0;
    clip->in_point = 0.0;
    clip->out_point = 2.0;

    start = now_ns();
    transcode_clip(&vm, clip, PERF_TRANSCODE_OUTPUT);
    report_benchmark("transcoder real video", now_ns() - start, 1, 48, "frames");

    if (!file_exists_nonempty(PERF_TRANSCODE_OUTPUT)) {
        clip_free(clip);
        freeVM(&vm);
        bench_fail("transcoder real video", "output file was not created");
        return BENCH_FAIL;
    }

    unlink(PERF_TRANSCODE_OUTPUT);
    clip_free(clip);
    freeVM(&vm);
    return BENCH_OK;
}

static BenchStatus benchmark_exporter_real_video_perf(void) {
    GLBenchContext glctx;
    VM vm;
    Allocator allocator;
    Timeline* tl;
    Clip* media_a;
    Clip* media_b;
    TimelineClip* media_a_tc;
    TimelineClip* media_b_tc;
    TimelineClip* adj_tc;
    uint64_t start;

    if (!ensure_real_video_fixture()) {
        bench_skip("exporter real video", "ffmpeg fixture generation unavailable");
        return BENCH_SKIP;
    }
    if (!init_hidden_gl(&glctx)) {
        bench_skip("exporter real video", "OpenGL context unavailable");
        return BENCH_SKIP;
    }

    unlink(PERF_EXPORT_OUTPUT);

    initVM(&vm);
    init_allocator(&allocator, &vm);
    effect_registry_init(&vm);
    effect_register_builtin_processors();

    tl = timeline_create(&allocator, 320, 180, 24.0);
    timeline_add_track(tl);
    timeline_add_track(tl);
    timeline_add_track(tl);

    media_a = clip_create_media(PERF_VIDEO_FIXTURE);
    media_a->width = 320;
    media_a->height = 180;
    media_a->fps = 24.0;
    media_a->duration = 2.0;
    media_a->out_point = 2.0;

    media_b = clip_create_media(PERF_VIDEO_FIXTURE);
    media_b->width = 320;
    media_b->height = 180;
    media_b->fps = 24.0;
    media_b->duration = 2.0;
    media_b->out_point = 2.0;
    media_b->default_scale_x = 0.6;
    media_b->default_scale_y = 0.6;
    media_b->default_x = 80.0;
    media_b->default_y = -30.0;
    media_b->default_rotation = 8.0;
    media_b->default_opacity = 0.8;

    timeline_add_clip(tl, 0, media_a, 0.0);
    timeline_add_clip(tl, 1, media_b, 0.25);
    timeline_add_clip(tl, 2, clip_create_adjustment(320, 180), 0.0);
    media_a_tc = timeline_get_clip(tl, 0, 0);
    media_b_tc = timeline_get_clip(tl, 1, 0);
    adj_tc = timeline_get_clip(tl, 2, 0);
    media_a_tc->timeline_duration = 2.0;
    media_b_tc->timeline_duration = 2.0;
    adj_tc->timeline_duration = 2.0;
    tl->duration = 2.25;
    effect_chain_append(&allocator, &adj_tc->effectChain, effect_registry_get("Posterize"), NULL, 0);
    if (adj_tc->effectChain) {
        adj_tc->effectChain->processor->set_number(adj_tc->effectChain->data, "levels", 10.0);
        adj_tc->effectChain->processor->set_number(adj_tc->effectChain->data, "amount", 0.35);
    }

    start = now_ns();
    export_timeline(&vm, tl, PERF_EXPORT_OUTPUT);
    report_benchmark("exporter real video", now_ns() - start, 1, 48, "frames");

    if (!file_exists_nonempty(PERF_EXPORT_OUTPUT)) {
        timeline_free(tl);
        freeVM(&vm);
        free_hidden_gl(&glctx);
        bench_fail("exporter real video", "output file was not created");
        return BENCH_FAIL;
    }

    unlink(PERF_EXPORT_OUTPUT);
    timeline_free(tl);
    freeVM(&vm);
    free_hidden_gl(&glctx);
    return BENCH_OK;
}

static void run_benchmark(const char* name, BenchStatus (*fn)(void)) {
    BenchStatus status;
    printf("[RUN ] %s\n", name);
    fflush(stdout);
    status = fn();
    if (status == BENCH_OK) {
        printf("[PASS] %s\n", name);
        fflush(stdout);
    } else if (status == BENCH_SKIP) {
        printf("[DONE] %s (skipped)\n", name);
        fflush(stdout);
    }
}

int main(void) {
    printf("=== Luna Performance Suite ===\n");
    run_benchmark("core: clip construction", benchmark_clip_construction);
    run_benchmark("core: image loader", benchmark_image_loader_perf);
    run_benchmark("model: animation evaluate", benchmark_animation_eval_perf);
    run_benchmark("model: timeline operations", benchmark_timeline_ops_perf);
    run_benchmark("language: compile stdlib script", benchmark_compile_stdlib_perf);
    run_benchmark("language: stdlib end-to-end", benchmark_end_to_end_stdlib_perf);
    run_benchmark("binding: video graph script", benchmark_video_bindings_perf);
    run_benchmark("effects: graph update", benchmark_effect_graph_perf);
    run_benchmark("render: compositor basic", benchmark_compositor_basic_perf);
    run_benchmark("render: compositor nested", benchmark_compositor_nested_perf);
    run_benchmark("media: decoder real video", benchmark_decoder_real_video_perf);
    run_benchmark("media: transcoder real video", benchmark_transcoder_real_video_perf);
    run_benchmark("media: exporter real video", benchmark_exporter_real_video_perf);

    if (g_failures != 0) {
        fprintf(stderr, "\n%d performance benchmarks failed\n", g_failures);
        return 1;
    }

    printf("\nPerformance suite finished");
    if (g_skips > 0) {
        printf(" with %d skipped benchmark(s)", g_skips);
    }
    printf(".\n");
    return 0;
}
