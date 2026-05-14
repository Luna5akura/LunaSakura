#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/chunk.h"
#include "core/compiler/compiler.h"
#include "core/memory.h"
#include "core/object.h"
#include "core/value.h"
#include "core/vm/vm.h"
#include "engine/bridge/object.h"
#include "engine/engine.h"
#include "engine/model/animation.h"
#include "engine/model/clip.h"
#include "engine/model/timeline.h"

void registerStdBindings(VM* vm);
void registerVideoBindings(VM* vm);

static int g_failures = 0;
static int g_tests_run = 0;

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
        return; \
    } \
} while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_INT_EQ(actual, expected) do { \
    long long _actual = (long long)(actual); \
    long long _expected = (long long)(expected); \
    if (_actual != _expected) { \
        fprintf(stderr, "[FAIL] %s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, _expected, _actual); \
        g_failures++; \
        return; \
    } \
} while (0)

#define EXPECT_DBL_EQ(actual, expected) do { \
    double _actual = (double)(actual); \
    double _expected = (double)(expected); \
    if (fabs(_actual - _expected) > 1e-6) { \
        fprintf(stderr, "[FAIL] %s:%d: expected %.6f, got %.6f\n", \
                __FILE__, __LINE__, _expected, _actual); \
        g_failures++; \
        return; \
    } \
} while (0)

#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "[FAIL] %s:%d: expected substring '%s'\n", \
                __FILE__, __LINE__, (needle)); \
        g_failures++; \
        return; \
    } \
} while (0)

typedef struct {
    VM vm;
    EngineContext engine_ctx;
    Chunk chunk;
    bool vm_initialized;
    bool compiled;
    InterpretResult result;
    char* stdout_text;
    char* stderr_text;
} ScriptSession;

static void* test_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size) {
    (void)ctx;
    (void)old_size;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }

    void* result = realloc(ptr, new_size);
    if (!result) {
        fprintf(stderr, "test realloc failed\n");
        exit(1);
    }
    return result;
}

static void test_mark_roots(VM* vm) {
    EngineContext* ctx = (EngineContext*)vm->user_data;
    if (ctx && ctx->active_project_obj) {
        markObject(vm, (Obj*)ctx->active_project_obj);
    }
}

static char* read_all(FILE* file) {
    long size;
    char* buffer;

    if (fseek(file, 0L, SEEK_END) != 0) return NULL;
    size = ftell(file);
    if (size < 0) return NULL;
    rewind(file);

    buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fprintf(stderr, "malloc failed while reading temp file\n");
        exit(1);
    }

    if (size > 0) {
        size_t read_count = fread(buffer, 1, (size_t)size, file);
        if (read_count != (size_t)size) {
            fprintf(stderr, "failed to read temp file\n");
            exit(1);
        }
    }

    buffer[size] = '\0';
    return buffer;
}

static void script_session_init(ScriptSession* session, bool include_video_bindings) {
    memset(session, 0, sizeof(*session));
    initChunk(&session->chunk);
    initVM(&session->vm);
    session->vm_initialized = true;
    session->vm.user_data = &session->engine_ctx;
    session->vm.host_mark_roots = test_mark_roots;
    registerStdBindings(&session->vm);
    if (include_video_bindings) {
        registerVideoBindings(&session->vm);
    }
}

static void script_session_run(ScriptSession* session, const char* source) {
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    FILE* out_file = tmpfile();
    FILE* err_file = tmpfile();

    if (saved_stdout < 0 || saved_stderr < 0 || !out_file || !err_file) {
        fprintf(stderr, "failed to prepare output capture\n");
        exit(1);
    }

    fflush(stdout);
    fflush(stderr);
    if (dup2(fileno(out_file), STDOUT_FILENO) < 0 ||
        dup2(fileno(err_file), STDERR_FILENO) < 0) {
        fprintf(stderr, "dup2 failed\n");
        exit(1);
    }

    session->compiled = compile(&session->vm, source, &session->chunk);
    session->result = session->compiled
        ? interpret(&session->vm, &session->chunk)
        : INTERPRET_COMPILE_ERROR;

    fflush(stdout);
    fflush(stderr);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0 ||
        dup2(saved_stderr, STDERR_FILENO) < 0) {
        fprintf(stderr, "failed to restore stdio\n");
        exit(1);
    }

    close(saved_stdout);
    close(saved_stderr);

    session->stdout_text = read_all(out_file);
    session->stderr_text = read_all(err_file);

    fclose(out_file);
    fclose(err_file);
}

static void script_session_dispose(ScriptSession* session) {
    if (session->vm_initialized) {
        freeChunk(&session->vm, &session->chunk);
        freeVM(&session->vm);
    }
    free(session->stdout_text);
    free(session->stderr_text);
    memset(session, 0, sizeof(*session));
}

static bool session_get_global(ScriptSession* session, const char* name, Value* out) {
    ObjString* key = copyString(&session->vm, name, (i32)strlen(name));
    return tableGet(&session->vm.globals, OBJ_VAL(key), out);
}

static bool instance_get_field(ScriptSession* session, Value instance_value, const char* field, Value* out) {
    ObjInstance* instance;
    ObjString* key;

    (void)session;
    if (!IS_INSTANCE(instance_value)) return false;

    instance = AS_INSTANCE(instance_value);
    key = copyString(&session->vm, field, (i32)strlen(field));
    return tableGet(&instance->fields, OBJ_VAL(key), out);
}

static ObjForeign* instance_get_handle(ScriptSession* session, Value instance_value) {
    Value handle;
    if (!instance_get_field(session, instance_value, "_handle", &handle)) return NULL;
    if (!IS_FOREIGN(handle)) return NULL;
    return AS_FOREIGN(handle);
}

static TimelineClip* resolve_clip_handle(ObjTimelineClip* handle, i32* out_track_index, i32* out_clip_index) {
    if (!handle || !handle->timeline || handle->clip_id == 0) return NULL;
    return timeline_find_clip_by_id(handle->timeline, handle->clip_id, out_track_index, out_clip_index);
}

static char* read_text_file(const char* path) {
    FILE* file = fopen(path, "rb");
    long size;
    char* buffer;

    if (!file) {
        fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    if (fseek(file, 0L, SEEK_END) != 0) exit(1);
    size = ftell(file);
    if (size < 0) exit(1);
    rewind(file);

    buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) exit(1);

    if (size > 0) {
        size_t read_count = fread(buffer, 1, (size_t)size, file);
        if (read_count != (size_t)size) exit(1);
    }
    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

static void run_test(void (*fn)(void), const char* name) {
    int before = g_failures;
    printf("[TEST] %s\n", name);
    g_tests_run++;
    fn();
    if (g_failures == before) {
        printf("[PASS] %s\n", name);
    }
}

static void test_clip_model_defaults(void) {
    Clip* media = clip_create_media("demo.mp4");
    Clip* text = clip_create_text("Hello", "arial.ttf", 48, 1, 2, 3);
    Clip* image = clip_create_image("tests/assets/test_image.ppm", 2, 2);
    Clip* solid = clip_create_solid(320, 180, 10, 20, 30, 200);
    Clip* group = clip_create_group(320, 180, 24.0);
    Clip* precomp = clip_create_precomp(640, 360, 30.0);

    EXPECT_TRUE(media != NULL);
    EXPECT_TRUE(text != NULL);
    EXPECT_TRUE(image != NULL);
    EXPECT_TRUE(solid != NULL);
    EXPECT_TRUE(group != NULL);
    EXPECT_TRUE(precomp != NULL);
    EXPECT_INT_EQ(media->type, CLIP_TYPE_MEDIA);
    EXPECT_INT_EQ(text->type, CLIP_TYPE_TEXT);
    EXPECT_INT_EQ(image->type, CLIP_TYPE_IMAGE);
    EXPECT_INT_EQ(solid->type, CLIP_TYPE_SOLID);
    EXPECT_INT_EQ(group->type, CLIP_TYPE_GROUP);
    EXPECT_INT_EQ(precomp->type, CLIP_TYPE_PRECOMP);
    EXPECT_DBL_EQ(media->default_scale_x, 1.0);
    EXPECT_DBL_EQ(media->default_scale_y, 1.0);
    EXPECT_DBL_EQ(media->default_opacity, 1.0);
    EXPECT_DBL_EQ(media->default_rotation, 0.0);
    EXPECT_DBL_EQ(media->volume, 1.0);
    EXPECT_DBL_EQ(text->duration, 5.0);
    EXPECT_INT_EQ(text->text.font_size, 48);
    EXPECT_INT_EQ(text->text.color.r, 1);
    EXPECT_INT_EQ(text->text.color.g, 2);
    EXPECT_INT_EQ(text->text.color.b, 3);
    EXPECT_TRUE(strcmp(media->path, "demo.mp4") == 0);
    EXPECT_TRUE(strcmp(text->text.content, "Hello") == 0);
    EXPECT_TRUE(strcmp(image->path, "tests/assets/test_image.ppm") == 0);
    EXPECT_INT_EQ(image->width, 2);
    EXPECT_INT_EQ(image->height, 2);
    EXPECT_INT_EQ(solid->width, 320);
    EXPECT_INT_EQ(solid->height, 180);
    EXPECT_INT_EQ(solid->solid.color.r, 10);
    EXPECT_INT_EQ(solid->solid.color.g, 20);
    EXPECT_INT_EQ(solid->solid.color.b, 30);
    EXPECT_INT_EQ(solid->solid.color.a, 200);
    EXPECT_INT_EQ(group->width, 320);
    EXPECT_INT_EQ(group->height, 180);
    EXPECT_DBL_EQ(group->fps, 24.0);
    EXPECT_INT_EQ(precomp->width, 640);
    EXPECT_INT_EQ(precomp->height, 360);
    EXPECT_DBL_EQ(precomp->fps, 30.0);

    clip_free(media);
    clip_free(text);
    clip_free(image);
    clip_free(solid);
    clip_free(group);
    clip_free(precomp);
}

static void test_animation_model_behaviour(void) {
    Allocator allocator = { test_realloc, NULL };
    Animation anim;
    Animation copied;
    const Keyframe* keyframe;

    init_animation(&anim, &allocator, 42.0);
    EXPECT_DBL_EQ(evaluate_animation(&anim, 0.0), 42.0);

    add_keyframe(&anim, &allocator, 2.0, 20.0, KEYFRAME_LINEAR, 0.0);
    add_keyframe(&anim, &allocator, 0.0, 10.0, KEYFRAME_LINEAR, 0.0);
    add_keyframe(&anim, &allocator, 1.0, 15.0, KEYFRAME_HOLD, 0.0);

    EXPECT_INT_EQ(anim.count, 3);
    EXPECT_DBL_EQ(anim.keyframes[0].time, 0.0);
    EXPECT_DBL_EQ(anim.keyframes[1].time, 1.0);
    EXPECT_DBL_EQ(anim.keyframes[2].time, 2.0);
    EXPECT_DBL_EQ(evaluate_animation(&anim, 0.5), 12.5);
    EXPECT_DBL_EQ(evaluate_animation(&anim, 1.5), 15.0);
    EXPECT_DBL_EQ(evaluate_animation(&anim, 5.0), 20.0);

    add_keyframe_with_preset(&anim, &allocator, 3.0, 40.0, "ease_in");
    EXPECT_INT_EQ(anim.count, 4);
    EXPECT_DBL_EQ(anim.keyframes[3].time, 3.0);
    EXPECT_INT_EQ(get_keyframe_count(&anim), 4);

    set_keyframe(&anim, &allocator, 1.0, 99.0, KEYFRAME_BEZIER, 0.2);
    EXPECT_INT_EQ(anim.count, 4);
    keyframe = find_keyframe(&anim, 1.0);
    EXPECT_TRUE(keyframe != NULL);
    EXPECT_DBL_EQ(keyframe->value, 99.0);
    EXPECT_INT_EQ(keyframe->type, KEYFRAME_BEZIER);
    EXPECT_DBL_EQ(keyframe->bezier_weight, 0.2);

    set_keyframe(&anim, &allocator, 4.0, 55.0, KEYFRAME_LINEAR, 0.0);
    EXPECT_INT_EQ(anim.count, 5);
    keyframe = get_keyframe_at(&anim, 4);
    EXPECT_TRUE(keyframe != NULL);
    EXPECT_DBL_EQ(keyframe->time, 4.0);
    EXPECT_DBL_EQ(keyframe->value, 55.0);

    EXPECT_TRUE(remove_keyframe(&anim, 2.0));
    EXPECT_FALSE(remove_keyframe(&anim, 123.0));
    EXPECT_INT_EQ(anim.count, 4);
    EXPECT_TRUE(find_keyframe(&anim, 2.0) == NULL);

    clear_keyframes(&anim);
    EXPECT_INT_EQ(anim.count, 0);
    EXPECT_DBL_EQ(evaluate_animation(&anim, 5.0), 42.0);

    add_keyframe(&anim, &allocator, 0.0, 10.0, KEYFRAME_LINEAR, 0.0);
    add_keyframe(&anim, &allocator, 2.0, 20.0, KEYFRAME_LINEAR, 0.0);
    shift_keyframe_times(&anim, 1.5);
    EXPECT_DBL_EQ(anim.keyframes[0].time, 1.5);
    EXPECT_DBL_EQ(anim.keyframes[1].time, 3.5);
    scale_keyframe_times(&anim, 2.0);
    EXPECT_DBL_EQ(anim.keyframes[0].time, 3.0);
    EXPECT_DBL_EQ(anim.keyframes[1].time, 7.0);

    init_animation(&copied, &allocator, -1.0);
    copy_keyframes(&copied, &allocator, &anim);
    EXPECT_INT_EQ(copied.count, anim.count);
    EXPECT_DBL_EQ(copied.keyframes[0].time, 3.0);
    EXPECT_DBL_EQ(copied.keyframes[1].value, 20.0);

    add_user_preset(&allocator, "custom_curve", KEYFRAME_BEZIER, 0.25);
    EXPECT_INT_EQ(user_preset_count, 1);
    reset_user_presets();
    EXPECT_INT_EQ(user_preset_count, 0);
    EXPECT_TRUE(user_presets == NULL);

    free_animation(&copied, &allocator);
    free_animation(&anim, &allocator);
}

static void test_timeline_model_behaviour(void) {
    Allocator allocator = { test_realloc, NULL };
    Timeline* tl = timeline_create(&allocator, 1920, 1080, 30.0);
    Clip* clip_a = clip_create_media("a.mp4");
    Clip* clip_b = clip_create_media("b.mp4");
    Clip* title = clip_create_text("Title", "arial.ttf", 64, 255, 255, 255);
    TimelineClip* active;
    TimelineClip* queried;
    u32 duplicate_id;
    i32 track_index = -1;
    i32 clip_index = -1;

    EXPECT_TRUE(tl != NULL);
    EXPECT_INT_EQ(timeline_add_track(tl), 0);
    EXPECT_INT_EQ(timeline_add_track(tl), 1);

    clip_a->duration = 4.0;
    clip_a->default_x = 10.0;
    clip_b->duration = 2.0;
    clip_b->default_scale_x = 0.5;
    title->duration = 3.0;

    EXPECT_INT_EQ(timeline_add_clip(tl, 0, clip_a, 5.0), 0);
    EXPECT_INT_EQ(timeline_add_clip(tl, 0, clip_b, 2.0), 0);
    EXPECT_INT_EQ(timeline_add_clip(tl, 1, title, 1.0), 0);

    EXPECT_DBL_EQ(tl->duration, 9.0);
    EXPECT_DBL_EQ(tl->tracks[0].clips[0].timeline_start, 2.0);
    EXPECT_DBL_EQ(tl->tracks[0].clips[1].timeline_start, 5.0);
    EXPECT_DBL_EQ(tl->tracks[0].clips[0].transform.scale_x, 0.5);
    EXPECT_DBL_EQ(tl->tracks[0].clips[1].transform.x, 10.0);
    EXPECT_DBL_EQ(tl->tracks[1].clips[0].anim.font_size.default_value, 64.0);
    EXPECT_INT_EQ(tl->tracks[0].clips[0].flags & 1, 1);
    EXPECT_DBL_EQ(tl->tracks[0].clips[0].source_in, clip_b->in_point);

    active = timeline_get_clip_at(&tl->tracks[0], 2.5);
    EXPECT_TRUE(active != NULL);
    EXPECT_TRUE(active->media == clip_b);
    EXPECT_TRUE(timeline_get_clip_at(&tl->tracks[0], 20.0) == NULL);
    EXPECT_INT_EQ(timeline_get_clip_count(tl, 0), 2);
    queried = timeline_get_clip(tl, 0, 1);
    EXPECT_TRUE(queried != NULL);
    EXPECT_TRUE(queried->media == clip_a);
    EXPECT_TRUE(timeline_find_clip_by_id(tl, queried->id, &track_index, &clip_index) == queried);
    EXPECT_INT_EQ(track_index, 0);
    EXPECT_INT_EQ(clip_index, 1);

    EXPECT_TRUE(timeline_move_clip_by_id(tl, queried->id, 1, 6.0));
    queried = timeline_find_clip_by_id(tl, queried->id, &track_index, &clip_index);
    EXPECT_TRUE(queried != NULL);
    EXPECT_INT_EQ(track_index, 1);
    EXPECT_DBL_EQ(queried->timeline_start, 6.0);

    duplicate_id = timeline_duplicate_clip_by_id(tl, queried->id, 0, 0.25);
    EXPECT_TRUE(duplicate_id != 0);
    queried = timeline_find_clip_by_id(tl, duplicate_id, &track_index, &clip_index);
    EXPECT_TRUE(queried != NULL);
    EXPECT_INT_EQ(track_index, 0);
    EXPECT_DBL_EQ(queried->timeline_start, 0.25);
    EXPECT_DBL_EQ(queried->transform.x, clip_a->default_x);
    EXPECT_TRUE(timeline_remove_clip_by_id(tl, duplicate_id));
    EXPECT_TRUE(timeline_find_clip_by_id(tl, duplicate_id, NULL, NULL) == NULL);

    timeline_remove_clip(tl, 0, 0);
    EXPECT_INT_EQ(tl->tracks[0].clip_count, 0);
    EXPECT_DBL_EQ(tl->duration, 10.0);

    timeline_remove_track(tl, 0);
    EXPECT_INT_EQ(tl->track_count, 1);
    EXPECT_DBL_EQ(tl->duration, 10.0);

    timeline_free(tl);
    clip_free(clip_a);
    clip_free(clip_b);
    clip_free(title);
}

static void test_language_and_stdlib_script(void) {
    static const char* source =
        "var nums = [1, 2, 3]\n"
        "print len(nums)\n"
        "set(nums, 1, 9)\n"
        "push(nums, 12)\n"
        "print get(nums, 1)\n"
        "print pop(nums)\n"
        "var doubled = [i * 2 for i in range(4)]\n"
        "print len(doubled)\n"
        "print get(doubled, 3)\n"
        "var reverse = range(5, -1, -2)\n"
        "print len(reverse)\n"
        "print get(reverse, 2)\n"
        "var d = Dict()\n"
        "dict_put(d, \"name\", \"luna\")\n"
        "dict_put(d, \"lang\", \"vm\")\n"
        "print dict_get(d, \"name\")\n"
        "print len(dict_keys(d))\n"
        "print len(dict_values(d))\n"
        "print dict_remove(d, \"lang\")\n"
        "fun makeAdder(x):\n"
        "    fun add(y):\n"
        "        return x + y\n"
        "    return add\n"
        "var add7 = makeAdder(7)\n"
        "print add7(8)\n"
        "var add = lam a, b: a + b\n"
        "print add(3, 4)\n"
        "fun greet(name, suffix=\"?\"):\n"
        "    return name + suffix\n"
        "print greet(\"Luna\")\n"
        "class Base:\n"
        "    fun greet():\n"
        "        return \"base\"\n"
        "class Child < Base:\n"
        "    fun greet():\n"
        "        return super.greet() + \"-child\"\n"
        "print Child().greet()\n"
        "class Vec:\n"
        "    fun init(x, y):\n"
        "        this.x = x\n"
        "        this.y = y\n"
        "    fun __add(other):\n"
        "        return Vec(this.x + other.x, this.y + other.y)\n"
        "var v = Vec(2, 3) + Vec(4, 5)\n"
        "print v.x\n"
        "print v.y\n"
        "try:\n"
        "    print -\"oops\"\n"
        "except:\n"
        "    print \"caught\"\n";
    ScriptSession session;

    script_session_init(&session, false);
    script_session_run(&session, source);

    if (session.result != INTERPRET_OK) {
        printf("script stdout:\n%s\n", session.stdout_text ? session.stdout_text : "(null)");
        printf("script stderr:\n%s\n", session.stderr_text ? session.stderr_text : "(null)");
        fflush(stdout);
    }
    EXPECT_TRUE(session.compiled);
    EXPECT_INT_EQ(session.result, INTERPRET_OK);
    EXPECT_STR_CONTAINS(session.stdout_text, "3\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "9\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "12\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "4\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "6\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "1\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "luna\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "15\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "7\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "Luna?\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "base-child\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "caught\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "6\n8\n");

    script_session_dispose(&session);
}

static void test_video_bindings_script(void) {
    static const char* source =
        "var proj = Project(640, 360, 24)\n"
        "var tl = Timeline(640, 360, 24)\n"
        "proj.setTimeline(tl)\n"
        "print tl.addTrack()\n"
        "print tl.getTrackCount()\n"
        "tl.setTrackName(0, \"Video\")\n"
        "print tl.getTrackName(0)\n"
        "tl.setTrackVisible(0, false)\n"
        "print tl.isTrackVisible(0)\n"
        "tl.setTrackVisible(0, true)\n"
        "tl.setTrackLocked(0, true)\n"
        "print tl.isTrackLocked(0)\n"
        "tl.setBackgroundColor(5, 6, 7, 200)\n"
        "var clip = Clip(\"test.mp4\")\n"
        "clip.trim(1, 2.5)\n"
        "clip.setScale(0.5)\n"
        "clip.setPos(12, 34)\n"
        "clip.setOpacity(0.75)\n"
        "clip.setVolume(0.25)\n"
        "var image = Image(\"tests/assets/test_image.ppm\")\n"
        "image.setScale(2)\n"
        "var matte = Solid(200, 40, 100, 110, 120, 220)\n"
        "matte.setColor(1, 2, 3, 240)\n"
        "var adjust = Adjustment(640, 360)\n"
        "var group = Group(320, 180, 24)\n"
        "var groupTl = group.getTimeline()\n"
        "var groupSolid = Solid(320, 180, 20, 30, 40, 255)\n"
        "var groupInnerInst = groupTl.add(0, groupSolid, 0)\n"
        "print groupTl.getTrackCount()\n"
        "print groupInnerInst.getStart()\n"
        "var precomp = Precomp(160, 90, 12)\n"
        "var preTl = Timeline(160, 90, 12)\n"
        "var preText = Text(\"P\")\n"
        "preTl.add(0, preText, 0)\n"
        "print precomp.setTimeline(preTl)\n"
        "print precomp.getTimeline().fps\n"
        "print adjust.affectsWholeFrame()\n"
        "print adjust.setAffectsWholeFrame(false)\n"
        "print adjust.affectsWholeFrame()\n"
        "print adjust.setFeather(18)\n"
        "print adjust.getFeather()\n"
        "print adjust.setBlendMode(\"screen\")\n"
        "print adjust.getBlendMode()\n"
        "adjust.setPos(80, 90)\n"
        "adjust.setScale(0.5, 0.25)\n"
        "adjust.setRotation(15)\n"
        "var text = Text(\"Hi\")\n"
        "text.setLetterSpacing(1.5)\n"
        "text.setStroke(true, 3, 10, 20, 30)\n"
        "var inst = tl.add(0, clip, 0.5)\n"
        "var imgInst = tl.add(0, image, 6.0)\n"
        "var matteInst = tl.add(0, matte, 6.5)\n"
        "var groupOuterInst = tl.add(0, group, 7.0)\n"
        "groupOuterInst.setDuration(2)\n"
        "var preOuterInst = tl.add(1, precomp, 7.25)\n"
        "preOuterInst.setDuration(2)\n"
        "var adjustInst = tl.add(2, adjust, 0)\n"
        "adjustInst.setDuration(8)\n"
        "var adjustFx = BrightnessContrast(contrast=1.1)\n"
        "print adjustInst.addEffect(adjustFx) != nil\n"
        "print adjustFx.setNumber(\"contrast\", 1.1)\n"
        "print adjustInst.getEffectCount()\n"
        "print adjust.setMaskSource(imgInst)\n"
        "print adjust.getMaskSourceClipId() > 0\n"
        "print adjust.setMaskMode(\"luma\")\n"
        "print adjust.getMaskMode()\n"
        "print adjust.setMaskInvert(true)\n"
        "print adjust.maskInverted()\n"
        "var tint = Tint(amount=0.75, color=[255, 128, 0, 200])\n"
        "print imgInst.addEffect(tint) != nil\n"
        "print tint.getName()\n"
        "print tint.setNumber(\"amount\", 0.75)\n"
        "print tint.getNumber(\"amount\")\n"
        "print tint.setColor(\"color\", 255, 128, 0, 200)\n"
        "print imgInst.getEffectCount()\n"
        "print imgInst.getEffect(0).getName()\n"
        "var fill = Fill(amount=0.4, color=[10, 20, 30, 255])\n"
        "print imgInst.addEffect(fill) != nil\n"
        "print fill.setNumber(\"amount\", 0.4)\n"
        "print fill.getNumber(\"amount\")\n"
        "print fill.setColor(\"color\", 10, 20, 30, 255)\n"
        "print fill.addNumberKeyframe(\"amount\", 0, 0.2, \"linear\")\n"
        "print fill.setNumberKeyframe(\"amount\", 1, 0.9, \"bezier\", 0.4)\n"
        "print fill.getNumberKeyframeCount(\"amount\")\n"
        "var mosaic = Mosaic(blockSize=6, sharpColors=true)\n"
        "print imgInst.addEffect(mosaic) != nil\n"
        "print mosaic.setNumber(\"blockSize\", 6)\n"
        "print mosaic.getNumber(\"blockSize\")\n"
        "print mosaic.setBool(\"sharpColors\", true)\n"
        "print mosaic.getBool(\"sharpColors\")\n"
        "print mosaic.addNumberKeyframe(\"blockSize\", 0, 4, \"linear\")\n"
        "print mosaic.setNumberKeyframe(\"blockSize\", 1, 10, \"linear\")\n"
        "print mosaic.getNumberKeyframeCount(\"blockSize\")\n"
        "var grid = Grid(sizeX=12, sizeY=8, lineWidth=0.2, opacity=0.5, color=[255, 255, 0, 255])\n"
        "print imgInst.addEffect(grid) != nil\n"
        "print grid.setNumber(\"sizeX\", 12)\n"
        "print grid.setNumber(\"sizeY\", 8)\n"
        "print grid.setNumber(\"lineWidth\", 0.2)\n"
        "print grid.setNumber(\"opacity\", 0.5)\n"
        "print grid.setColor(\"color\", 255, 255, 0, 255)\n"
        "print grid.addNumberKeyframe(\"opacity\", 0, 0.2, \"linear\")\n"
        "print grid.getNumberKeyframeCount(\"opacity\")\n"
        "var ramp = GradientRamp(startX=0, startY=0, endX=1, endY=1, blend=0.6, startColor=[0, 0, 255, 255], endColor=[255, 0, 255, 255])\n"
        "print imgInst.addEffect(ramp) != nil\n"
        "print ramp.setNumber(\"startX\", 0)\n"
        "print ramp.setNumber(\"startY\", 0)\n"
        "print ramp.setNumber(\"endX\", 1)\n"
        "print ramp.setNumber(\"endY\", 1)\n"
        "print ramp.setNumber(\"blend\", 0.6)\n"
        "print ramp.setColor(\"startColor\", 0, 0, 255, 255)\n"
        "print ramp.setColor(\"endColor\", 255, 0, 255, 255)\n"
        "print ramp.addColorKeyframe(\"startColor\", 0, 255, 0, 0, 255, \"linear\")\n"
        "print ramp.setColorKeyframe(\"startColor\", 1, 0, 255, 0, 255, \"linear\")\n"
        "print ramp.getColorKeyframeCount(\"startColor\")\n"
        "print ramp.addColorKeyframe(\"endColor\", 0, 0, 0, 255, 255, \"linear\")\n"
        "print ramp.setColorKeyframe(\"endColor\", 1, 255, 255, 0, 255, \"linear\")\n"
        "print ramp.getColorKeyframeCount(\"endColor\")\n"
        "var fractal = FractalNoise(scale=90, evolution=12, contrast=1.3, brightness=0.1, octaves=5, amount=0.6, offsetX=4, offsetY=8, invert=true)\n"
        "print imgInst.addEffect(fractal) != nil\n"
        "print fractal.setNumber(\"scale\", 90)\n"
        "print fractal.setNumber(\"evolution\", 12)\n"
        "print fractal.setNumber(\"contrast\", 1.3)\n"
        "print fractal.setNumber(\"brightness\", 0.1)\n"
        "print fractal.setNumber(\"octaves\", 5)\n"
        "print fractal.setNumber(\"amount\", 0.6)\n"
        "print fractal.setNumber(\"offsetX\", 4)\n"
        "print fractal.setNumber(\"offsetY\", 8)\n"
        "print fractal.setBool(\"invert\", true)\n"
        "print fractal.getBool(\"invert\")\n"
        "print fractal.addNumberKeyframe(\"evolution\", 0, 0, \"linear\")\n"
        "print fractal.setNumberKeyframe(\"evolution\", 1, 45, \"bezier\", 0.2)\n"
        "print fractal.getNumberKeyframeCount(\"evolution\")\n"
        "print fractal.getNumberKeyframeTime(\"evolution\", 0)\n"
        "print fractal.getNumberKeyframeValue(\"evolution\", 0)\n"
        "print fractal.getNumberKeyframeType(\"evolution\", 1)\n"
        "print fractal.getNumberKeyframeWeight(\"evolution\", 1)\n"
        "print fractal.shiftNumberKeyframes(\"evolution\", 1.5)\n"
        "print fractal.scaleNumberKeyframeTimes(\"evolution\", 2)\n"
        "print fractal.getNumberKeyframeTime(\"evolution\", 0)\n"
        "var disp = DisplacementMap(scaleX=16, scaleY=12, amount=0.8, offsetX=2, offsetY=3, useLuma=false)\n"
        "print imgInst.addEffect(disp) != nil\n"
        "print disp.setNumber(\"scaleX\", 16)\n"
        "print disp.setNumber(\"scaleY\", 12)\n"
        "print disp.setNumber(\"amount\", 0.8)\n"
        "print disp.setNumber(\"offsetX\", 2)\n"
        "print disp.setNumber(\"offsetY\", 3)\n"
        "print disp.setBool(\"useLuma\", false)\n"
        "print disp.getBool(\"useLuma\")\n"
        "print disp.setSource(matteInst)\n"
        "print disp.getSourceClipId() > 0\n"
        "var post = Posterize(levels=7, amount=0.5)\n"
        "print imgInst.addEffect(post) != nil\n"
        "print post.setNumber(\"levels\", 7)\n"
        "print post.getNumber(\"levels\")\n"
        "print post.setNumber(\"amount\", 0.5)\n"
        "var slider = SliderControl(value=12)\n"
        "print imgInst.addEffect(slider) != nil\n"
        "print slider.setNumber(\"value\", 12)\n"
        "print slider.addNumberKeyframe(\"value\", 0, 2, \"linear\")\n"
        "print slider.setNumberKeyframe(\"value\", 1, 8, \"bezier\", 0.1)\n"
        "print slider.getNumberKeyframeCount(\"value\")\n"
        "print slider.getNumberKeyframeTime(\"value\", 0)\n"
        "print slider.getNumberKeyframeValue(\"value\", 0)\n"
        "print slider.getNumberKeyframeType(\"value\", 1)\n"
        "print slider.getNumberKeyframeWeight(\"value\", 1)\n"
        "print slider.copyNumberKeyframesFrom(\"value\", fractal, \"evolution\")\n"
        "print slider.getNumberKeyframeCount(\"value\")\n"
        "var angle = AngleControl(angle=45)\n"
        "print imgInst.addEffect(angle) != nil\n"
        "print angle.setNumber(\"angle\", 45)\n"
        "print angle.getNumber(\"angle\")\n"
        "var check = CheckboxControl(value=true)\n"
        "print imgInst.addEffect(check) != nil\n"
        "print check.setBool(\"value\", true)\n"
        "print check.getBool(\"value\")\n"
        "print check.addNumberKeyframe(\"value\", 0, 1, \"linear\")\n"
        "print check.getNumberKeyframeValue(\"value\", 0)\n"
        "var point = PointControl(x=10, y=20)\n"
        "print imgInst.addEffect(point) != nil\n"
        "print point.setNumber(\"x\", 10)\n"
        "print point.setNumber(\"y\", 20)\n"
        "print point.addNumberKeyframe(\"x\", 0, 3, \"linear\")\n"
        "print point.getNumberKeyframeValue(\"x\", 0)\n"
        "var colorCtl = ColorControl(color=[1, 2, 3, 255])\n"
        "print imgInst.addEffect(colorCtl) != nil\n"
        "print colorCtl.setColor(\"color\", 1, 2, 3, 255)\n"
        "print colorCtl.addColorKeyframe(\"color\", 0, 10, 20, 30, 255, \"linear\")\n"
        "print colorCtl.setColorKeyframe(\"color\", 1, 40, 50, 60, 255, \"bezier\", 0.3)\n"
        "print colorCtl.getColorKeyframeCount(\"color\")\n"
        "print colorCtl.getColorKeyframeTime(\"color\", 0)\n"
        "print colorCtl.getColorKeyframeValue(\"color\", 0, \"r\")\n"
        "print colorCtl.getColorKeyframeType(\"color\", 1)\n"
        "print colorCtl.getColorKeyframeWeight(\"color\", 1)\n"
        "print colorCtl.shiftColorKeyframes(\"color\", 0.5)\n"
        "print colorCtl.scaleColorKeyframeTimes(\"color\", 2)\n"
        "print colorCtl.copyColorKeyframesFrom(\"color\", ramp, \"startColor\")\n"
        "print colorCtl.getColorKeyframeCount(\"color\")\n"
        "print post.linkNumber(\"levels\", slider, \"value\", 0.5, 1)\n"
        "print post.unlinkNumber(\"levels\")\n"
        "print post.linkNumber(\"levels\", slider, \"value\", 0.5, 1)\n"
        "print fill.linkColor(\"color\", colorCtl, \"color\")\n"
        "print fill.unlinkColor(\"color\")\n"
        "print fill.linkColor(\"color\", colorCtl, \"color\")\n"
        "print imgInst.getEffectCount()\n"
        "var grade = BrightnessContrast(brightness=0.1, contrast=1.5)\n"
        "print matteInst.addEffect(grade) != nil\n"
        "grade.setNumber(\"brightness\", 0.1)\n"
        "grade.setNumber(\"contrast\", 1.5)\n"
        "var blur = Blur(radius=3)\n"
        "print matteInst.addEffect(blur) != nil\n"
        "print blur.setNumber(\"radius\", 3)\n"
        "print blur.getNumber(\"radius\")\n"
        "var reuse = Blur(radius=1)\n"
        "print imgInst.addEffect(reuse) != nil\n"
        "print matteInst.addEffect(reuse) == nil\n"
        "print matteInst.getEffectCount()\n"
        "print matteInst.removeEffect(0)\n"
        "print matteInst.getEffectCount()\n"
        "matteInst.clearEffects()\n"
        "print matteInst.getEffectCount()\n"
        "imgInst.addKeyframe(\"opacity\", 0, 1, \"linear\")\n"
        "matteInst.setZIndex(-1)\n"
        "inst.addKeyframe(\"x\", 0, 12, \"linear\")\n"
        "inst.setKeyframe(\"x\", 0, 88, \"bezier\", 0.4)\n"
        "inst.addKeyframeWithPreset(\"opacity\", 1, 0.1, \"ease_out\")\n"
        "inst.setKeyframe(\"opacity\", 1, 0.5, \"linear\")\n"
        "addUserPreset(\"soft\", \"bezier\", 0.3)\n"
        "inst.addKeyframeWithPreset(\"scale_x\", 2, 0.8, \"soft\")\n"
        "inst.addKeyframe(\"scale_x\", 3, 0.9, \"linear\")\n"
        "print inst.getKeyframeCount(\"x\")\n"
        "print inst.getKeyframeTime(\"x\", 0)\n"
        "print inst.getKeyframeValue(\"x\", 0)\n"
        "print inst.getKeyframeType(\"x\", 0)\n"
        "print inst.getKeyframeWeight(\"x\", 0)\n"
        "print inst.removeKeyframe(\"scale_x\", 2)\n"
        "print inst.getKeyframeCount(\"scale_x\")\n"
        "inst.clearKeyframes(\"opacity\")\n"
        "print inst.getKeyframeCount(\"opacity\")\n"
        "inst.setStart(1.25)\n"
        "inst.setDuration(4.5)\n"
        "inst.setInPoint(0.75)\n"
        "inst.setZIndex(9)\n"
        "inst.setVisible(false)\n"
        "print inst.getStart()\n"
        "print inst.getDuration()\n"
        "print inst.getInPoint()\n"
        "print inst.getZIndex()\n"
        "print inst.isVisible()\n"
        "print tl.getClipCount(0)\n"
        "var clipAgain = tl.getClip(0, 0)\n"
        "print clipAgain.getStart()\n"
        "var copy = inst.duplicate(1, 2.0)\n"
        "print tl.getTrackCount()\n"
        "print tl.getClipCount(1)\n"
        "copy.moveToTrack(0, 3.5)\n"
        "copy.shiftKeyframes(\"x\", 1.5)\n"
        "copy.scaleKeyframeTimes(\"x\", 2)\n"
        "copy.copyKeyframesFrom(\"opacity\", inst, \"x\")\n"
        "print copy.getKeyframeCount(\"x\")\n"
        "print copy.getKeyframeTime(\"x\", 0)\n"
        "print copy.getKeyframeCount(\"opacity\")\n"
        "print copy.getKeyframeValue(\"opacity\", 0)\n"
        "print tl.getClipCount(1)\n"
        "print tl.getClipCount(0)\n"
        "print tl.removeClip(0, 0)\n"
        "print tl.getClipCount(0)\n"
        "print copy.remove()\n"
        "print tl.getClipCount(0)\n"
        "proj.setBackgroundColor(11, 22, 33, 255)\n"
        "proj.setPreviewRange(2, 4)\n"
        "print proj.getDuration()\n"
        "proj.clearPreviewRange()\n"
        "proj.preview(0.5, 1.5)\n"
        "print tl.getDuration()\n"
        "print clip.volume\n"
        "print image.width\n"
        "print image.height\n"
        "print matte.width\n"
        "print matte.height\n"
        "print text.letter_spacing\n"
        "print text.stroke_enabled\n";
    ScriptSession session;
    Value proj_value;
    Value tl_value;
    Value clip_value;
    Value image_value;
    Value matte_value;
    Value adjust_value;
    Value group_value;
    Value precomp_value;
    Value text_value;
    Value inst_value;
    Value adjust_inst_value;
    Value adjust_fx_value;
    Value tint_value;
    Value fill_value;
    Value mosaic_value;
    Value grid_value;
    Value ramp_value;
    Value fractal_value;
    Value disp_value;
    Value post_value;
    Value slider_value;
    Value angle_value;
    Value check_value;
    Value point_value;
    Value color_ctl_value;
    Value field;
    ObjProject* project_handle;
    ObjTimeline* timeline_handle;
    ObjClip* clip_handle;
    ObjClip* image_handle;
    ObjClip* matte_handle;
    ObjClip* group_handle;
    ObjClip* precomp_handle;
    ObjTimelineClip* inst_handle;
    ObjEffectHandle* tint_handle;
    ObjEffectHandle* fill_handle;
    ObjEffectHandle* mosaic_handle;
    ObjEffectHandle* grid_handle;
    ObjEffectHandle* ramp_handle;
    ObjEffectHandle* fractal_handle;
    ObjEffectHandle* disp_handle;
    ObjEffectHandle* post_handle;
    ObjEffectHandle* slider_handle;
    ObjEffectHandle* angle_handle;
    ObjEffectHandle* check_handle;
    ObjEffectHandle* point_handle;
    ObjEffectHandle* color_ctl_handle;
    TimelineClip* inst_clip;
    Value copy_value;
    ObjTimelineClip* copy_handle;
    TimelineClip* copy_clip;
    i32 copy_track_index = -1;
    script_session_init(&session, true);
    script_session_run(&session, source);

    EXPECT_TRUE(session.compiled);
    EXPECT_INT_EQ(session.result, INTERPRET_OK);
    EXPECT_TRUE(session.engine_ctx.active_project != NULL);
    EXPECT_TRUE(session.engine_ctx.active_project_obj != NULL);
    EXPECT_DBL_EQ(session.engine_ctx.active_project->preview_start, 0.5);
    EXPECT_DBL_EQ(session.engine_ctx.active_project->preview_end, 1.5);
    EXPECT_TRUE(session.engine_ctx.active_project->use_preview_range);
    EXPECT_STR_CONTAINS(session.stdout_text, "[Binding] Project preview range set: 0.50 - 1.50");
    EXPECT_STR_CONTAINS(session.stdout_text, "0\n1\nVideo\nfalse\ntrue\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "1\n0\ntrue\n12\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\nfalse\ntrue\n18\ntrue\nscreen\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "0.25\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\ntrue\nluma\ntrue\ntrue\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "2\n2\n200\n40\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "1.5\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\ntrue\n2\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\n2\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\n1\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\n2\ntrue\ntrue\n2\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\ntrue\n3\ntrue\ntrue\n2\ntrue\n1\n0\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "bezier\n0.2\ntrue\ntrue\n3\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\nfalse\ntrue\ntrue\ntrue\ntrue\n7\ntrue\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\ntrue\n1\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\n2\n0\n10\nbezier\n0.3\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "true\ntrue\ntrue\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "13\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "1\n0\n88\nbezier\n0.4\ntrue\n1\n0\n");
    EXPECT_STR_CONTAINS(session.stdout_text, "1.25\n4.5\n0.75\n9\nfalse\n");
    EXPECT_TRUE(session_get_global(&session, "proj", &proj_value));
    EXPECT_TRUE(session_get_global(&session, "tl", &tl_value));
    EXPECT_TRUE(session_get_global(&session, "clip", &clip_value));
    EXPECT_TRUE(session_get_global(&session, "image", &image_value));
    EXPECT_TRUE(session_get_global(&session, "matte", &matte_value));
    EXPECT_TRUE(session_get_global(&session, "adjust", &adjust_value));
    EXPECT_TRUE(session_get_global(&session, "group", &group_value));
    EXPECT_TRUE(session_get_global(&session, "precomp", &precomp_value));
    EXPECT_TRUE(session_get_global(&session, "text", &text_value));
    EXPECT_TRUE(session_get_global(&session, "inst", &inst_value));
    EXPECT_TRUE(session_get_global(&session, "adjustInst", &adjust_inst_value));
    EXPECT_TRUE(session_get_global(&session, "adjustFx", &adjust_fx_value));
    EXPECT_TRUE(session_get_global(&session, "tint", &tint_value));
    EXPECT_TRUE(session_get_global(&session, "fill", &fill_value));
    EXPECT_TRUE(session_get_global(&session, "mosaic", &mosaic_value));
    EXPECT_TRUE(session_get_global(&session, "grid", &grid_value));
    EXPECT_TRUE(session_get_global(&session, "ramp", &ramp_value));
    EXPECT_TRUE(session_get_global(&session, "fractal", &fractal_value));
    EXPECT_TRUE(session_get_global(&session, "disp", &disp_value));
    EXPECT_TRUE(session_get_global(&session, "post", &post_value));
    EXPECT_TRUE(session_get_global(&session, "slider", &slider_value));
    EXPECT_TRUE(session_get_global(&session, "angle", &angle_value));
    EXPECT_TRUE(session_get_global(&session, "check", &check_value));
    EXPECT_TRUE(session_get_global(&session, "point", &point_value));
    EXPECT_TRUE(session_get_global(&session, "colorCtl", &color_ctl_value));
    EXPECT_TRUE(session_get_global(&session, "copy", &copy_value));

    project_handle = (ObjProject*)instance_get_handle(&session, proj_value);
    timeline_handle = (ObjTimeline*)instance_get_handle(&session, tl_value);
    clip_handle = (ObjClip*)instance_get_handle(&session, clip_value);
    image_handle = (ObjClip*)instance_get_handle(&session, image_value);
    matte_handle = (ObjClip*)instance_get_handle(&session, matte_value);
    group_handle = (ObjClip*)instance_get_handle(&session, group_value);
    precomp_handle = (ObjClip*)instance_get_handle(&session, precomp_value);
    ObjClip* adjust_handle = (ObjClip*)instance_get_handle(&session, adjust_value);
    inst_handle = (ObjTimelineClip*)instance_get_handle(&session, inst_value);
    ObjTimelineClip* adjust_inst_handle = (ObjTimelineClip*)instance_get_handle(&session, adjust_inst_value);
    ObjEffectHandle* adjust_fx_handle = (ObjEffectHandle*)instance_get_handle(&session, adjust_fx_value);
    tint_handle = (ObjEffectHandle*)instance_get_handle(&session, tint_value);
    fill_handle = (ObjEffectHandle*)instance_get_handle(&session, fill_value);
    mosaic_handle = (ObjEffectHandle*)instance_get_handle(&session, mosaic_value);
    grid_handle = (ObjEffectHandle*)instance_get_handle(&session, grid_value);
    ramp_handle = (ObjEffectHandle*)instance_get_handle(&session, ramp_value);
    fractal_handle = (ObjEffectHandle*)instance_get_handle(&session, fractal_value);
    disp_handle = (ObjEffectHandle*)instance_get_handle(&session, disp_value);
    post_handle = (ObjEffectHandle*)instance_get_handle(&session, post_value);
    slider_handle = (ObjEffectHandle*)instance_get_handle(&session, slider_value);
    angle_handle = (ObjEffectHandle*)instance_get_handle(&session, angle_value);
    check_handle = (ObjEffectHandle*)instance_get_handle(&session, check_value);
    point_handle = (ObjEffectHandle*)instance_get_handle(&session, point_value);
    color_ctl_handle = (ObjEffectHandle*)instance_get_handle(&session, color_ctl_value);
    copy_handle = (ObjTimelineClip*)instance_get_handle(&session, copy_value);
    inst_clip = resolve_clip_handle(inst_handle, NULL, NULL);
    copy_clip = resolve_clip_handle(copy_handle, &copy_track_index, NULL);

    EXPECT_TRUE(project_handle != NULL);
    EXPECT_TRUE(timeline_handle != NULL);
    EXPECT_TRUE(clip_handle != NULL);
    EXPECT_TRUE(image_handle != NULL);
    EXPECT_TRUE(matte_handle != NULL);
    EXPECT_TRUE(group_handle != NULL);
    EXPECT_TRUE(precomp_handle != NULL);
    EXPECT_TRUE(adjust_handle != NULL);
    EXPECT_TRUE(inst_handle != NULL);
    EXPECT_TRUE(adjust_inst_handle != NULL);
    EXPECT_TRUE(adjust_fx_handle != NULL);
    EXPECT_TRUE(tint_handle != NULL);
    EXPECT_TRUE(fill_handle != NULL);
    EXPECT_TRUE(mosaic_handle != NULL);
    EXPECT_TRUE(grid_handle != NULL);
    EXPECT_TRUE(ramp_handle != NULL);
    EXPECT_TRUE(fractal_handle != NULL);
    EXPECT_TRUE(disp_handle != NULL);
    EXPECT_TRUE(post_handle != NULL);
    EXPECT_TRUE(slider_handle != NULL);
    EXPECT_TRUE(angle_handle != NULL);
    EXPECT_TRUE(check_handle != NULL);
    EXPECT_TRUE(point_handle != NULL);
    EXPECT_TRUE(color_ctl_handle != NULL);
    EXPECT_TRUE(copy_handle != NULL);
    EXPECT_TRUE(project_handle->timelineObj == (struct ObjTimeline*)timeline_handle);
    EXPECT_DBL_EQ(project_handle->project->preview_start, 0.5);
    EXPECT_DBL_EQ(project_handle->project->preview_end, 1.5);
    EXPECT_DBL_EQ(clip_handle->clip->volume, 0.25);
    EXPECT_DBL_EQ(clip_handle->clip->default_x, 12.0);
    EXPECT_DBL_EQ(clip_handle->clip->default_y, 34.0);
    EXPECT_DBL_EQ(clip_handle->clip->default_scale_x, 0.5);
    EXPECT_DBL_EQ(clip_handle->clip->default_opacity, 0.75);
    EXPECT_INT_EQ(image_handle->clip->type, CLIP_TYPE_IMAGE);
    EXPECT_INT_EQ(image_handle->clip->width, 2);
    EXPECT_INT_EQ(image_handle->clip->height, 2);
    EXPECT_DBL_EQ(image_handle->clip->default_scale_x, 2.0);
    EXPECT_INT_EQ(matte_handle->clip->type, CLIP_TYPE_SOLID);
    EXPECT_INT_EQ(matte_handle->clip->width, 200);
    EXPECT_INT_EQ(matte_handle->clip->height, 40);
    EXPECT_INT_EQ(matte_handle->clip->solid.color.r, 1);
    EXPECT_INT_EQ(matte_handle->clip->solid.color.g, 2);
    EXPECT_INT_EQ(matte_handle->clip->solid.color.b, 3);
    EXPECT_INT_EQ(matte_handle->clip->solid.color.a, 240);
    EXPECT_INT_EQ(group_handle->clip->type, CLIP_TYPE_GROUP);
    EXPECT_TRUE(group_handle->timelineObj != NULL);
    EXPECT_TRUE(group_handle->clip->nested_timeline.timeline == group_handle->timelineObj->timeline);
    EXPECT_INT_EQ(group_handle->clip->width, 320);
    EXPECT_INT_EQ(group_handle->clip->height, 180);
    EXPECT_INT_EQ(precomp_handle->clip->type, CLIP_TYPE_PRECOMP);
    EXPECT_TRUE(precomp_handle->timelineObj != NULL);
    EXPECT_TRUE(precomp_handle->clip->nested_timeline.timeline == precomp_handle->timelineObj->timeline);
    EXPECT_INT_EQ(precomp_handle->clip->width, 160);
    EXPECT_INT_EQ(precomp_handle->clip->height, 90);
    EXPECT_DBL_EQ(precomp_handle->clip->fps, 12.0);
    EXPECT_INT_EQ(adjust_handle->clip->type, CLIP_TYPE_ADJUSTMENT);
    EXPECT_INT_EQ(adjust_handle->clip->width, 640);
    EXPECT_INT_EQ(adjust_handle->clip->height, 360);
    EXPECT_TRUE(!adjust_handle->clip->adjustment.affects_whole_frame);
    EXPECT_DBL_EQ(adjust_handle->clip->adjustment.feather, 18.0);
    EXPECT_INT_EQ(adjust_handle->clip->adjustment.blend_mode, 3);
    EXPECT_INT_EQ(adjust_handle->clip->adjustment.mask_mode, 1);
    EXPECT_TRUE(adjust_handle->clip->adjustment.mask_invert);
    EXPECT_DBL_EQ(adjust_handle->clip->default_x, 80.0);
    EXPECT_DBL_EQ(adjust_handle->clip->default_y, 90.0);
    EXPECT_DBL_EQ(adjust_handle->clip->default_scale_x, 0.5);
    EXPECT_DBL_EQ(adjust_handle->clip->default_scale_y, 0.25);
    EXPECT_DBL_EQ(adjust_handle->clip->default_rotation, 15.0);
    EXPECT_TRUE(adjust_fx_handle->effect != NULL);
    EXPECT_TRUE(strcmp(adjust_fx_handle->effect->name, "BrightnessContrast") == 0);
    EXPECT_TRUE(tint_handle->effect != NULL);
    EXPECT_TRUE(strcmp(tint_handle->effect->name, "Tint") == 0);
    EXPECT_TRUE(fill_handle->effect != NULL);
    EXPECT_TRUE(strcmp(fill_handle->effect->name, "Fill") == 0);
    EXPECT_TRUE(mosaic_handle->effect != NULL);
    EXPECT_TRUE(strcmp(mosaic_handle->effect->name, "Mosaic") == 0);
    EXPECT_TRUE(grid_handle->effect != NULL);
    EXPECT_TRUE(strcmp(grid_handle->effect->name, "Grid") == 0);
    EXPECT_TRUE(ramp_handle->effect != NULL);
    EXPECT_TRUE(strcmp(ramp_handle->effect->name, "GradientRamp") == 0);
    EXPECT_TRUE(fractal_handle->effect != NULL);
    EXPECT_TRUE(strcmp(fractal_handle->effect->name, "FractalNoise") == 0);
    EXPECT_TRUE(disp_handle->effect != NULL);
    EXPECT_TRUE(strcmp(disp_handle->effect->name, "DisplacementMap") == 0);
    EXPECT_TRUE(post_handle->effect != NULL);
    EXPECT_TRUE(strcmp(post_handle->effect->name, "Posterize") == 0);
    EXPECT_TRUE(slider_handle->effect != NULL);
    EXPECT_TRUE(strcmp(slider_handle->effect->name, "SliderControl") == 0);
    EXPECT_TRUE(angle_handle->effect != NULL);
    EXPECT_TRUE(strcmp(angle_handle->effect->name, "AngleControl") == 0);
    EXPECT_TRUE(check_handle->effect != NULL);
    EXPECT_TRUE(strcmp(check_handle->effect->name, "CheckboxControl") == 0);
    EXPECT_TRUE(point_handle->effect != NULL);
    EXPECT_TRUE(strcmp(point_handle->effect->name, "PointControl") == 0);
    EXPECT_TRUE(color_ctl_handle->effect != NULL);
    EXPECT_TRUE(strcmp(color_ctl_handle->effect->name, "ColorControl") == 0);
    EXPECT_TRUE(disp_handle->effect->processor->get_source_clip != NULL);
    EXPECT_INT_EQ((int)disp_handle->effect->processor->get_source_clip(disp_handle->effect->data),
                  (int)timeline_get_clip(timeline_handle->timeline, 0, 1)->id);
    EXPECT_TRUE(post_handle->effect->links != NULL);
    EXPECT_TRUE(strcmp(post_handle->effect->links->target_key, "levels") == 0);
    EXPECT_INT_EQ((int)post_handle->effect->links->kind, (int)EFFECT_LINK_NUMBER);
    EXPECT_TRUE(fill_handle->effect->links != NULL);
    EXPECT_TRUE(strcmp(fill_handle->effect->links->target_key, "color") == 0);
    EXPECT_INT_EQ((int)fill_handle->effect->links->kind, (int)EFFECT_LINK_COLOR);
    EXPECT_INT_EQ(effect_chain_count(timeline_get_clip(timeline_handle->timeline, 0, 0)->effectChain), 14);
    EXPECT_INT_EQ(effect_chain_count(timeline_get_clip(timeline_handle->timeline, 2, 0)->effectChain), 1);
    EXPECT_INT_EQ(effect_chain_count(timeline_get_clip(timeline_handle->timeline, 0, 1)->effectChain), 0);
    EXPECT_TRUE(inst_clip == NULL);
    EXPECT_TRUE(copy_clip == NULL);
    EXPECT_INT_EQ(timeline_handle->timeline->track_count, 3);
    EXPECT_INT_EQ(timeline_get_clip_count(timeline_handle->timeline, 0), 3);
    EXPECT_INT_EQ(timeline_get_clip_count(timeline_handle->timeline, 1), 1);
    EXPECT_INT_EQ(timeline_get_clip_count(timeline_handle->timeline, 2), 1);
    EXPECT_TRUE(strcmp(timeline_handle->timeline->tracks[0].name, "Video") == 0);
    EXPECT_INT_EQ(timeline_handle->timeline->tracks[0].flags & 1, 1);
    EXPECT_INT_EQ(timeline_handle->timeline->tracks[0].flags & 2, 2);
    EXPECT_INT_EQ(timeline_handle->timeline->background_color.r, 11);
    EXPECT_INT_EQ(timeline_handle->timeline->background_color.g, 22);
    EXPECT_INT_EQ(timeline_handle->timeline->background_color.b, 33);
    EXPECT_DBL_EQ(timeline_handle->timeline->duration, 11.5);

    EXPECT_TRUE(instance_get_field(&session, text_value, "letter_spacing", &field));
    EXPECT_DBL_EQ(AS_NUMBER(field), 1.5);
    EXPECT_TRUE(instance_get_field(&session, text_value, "stroke_enabled", &field));
    EXPECT_TRUE(AS_BOOL(field));

    collectGarbage(&session.vm);
    EXPECT_TRUE(session.engine_ctx.active_project != NULL);
    EXPECT_TRUE(session.engine_ctx.active_project_obj != NULL);
    EXPECT_TRUE(project_handle->project == session.engine_ctx.active_project);

    script_session_dispose(&session);
}

static void test_repository_sample_scripts(void) {
    const char* paths[] = { "test.luna", "engine.luna", "text.luna", "layers.luna" };
    const bool uses_video[] = { false, true, true, true };
    size_t count = sizeof(paths) / sizeof(paths[0]);
    size_t i;

    for (i = 0; i < count; i++) {
        char* source = read_text_file(paths[i]);
        ScriptSession session;

        script_session_init(&session, uses_video[i]);
        script_session_run(&session, source);

        EXPECT_TRUE(session.compiled);
        EXPECT_INT_EQ(session.result, INTERPRET_OK);

        script_session_dispose(&session);
        free(source);
    }
}

static void test_displacement_uses_post_effect_source(void) {
    const char* source =
        "var proj = Project(64, 64, 24)\n"
        "var tl = Timeline(64, 64, 24)\n"
        "proj.setTimeline(tl)\n"
        "var base = Solid(64, 64, 20, 30, 40, 255)\n"
        "var sourceClip = Solid(64, 64, 0, 0, 0, 255)\n"
        "var baseInst = tl.add(0, base, 0)\n"
        "baseInst.setDuration(1)\n"
        "var sourceInst = tl.add(1, sourceClip, 0)\n"
        "sourceInst.setDuration(1)\n"
        "var noise = FractalNoise(scale=32, evolution=12, contrast=1.2, amount=1.0)\n"
        "print sourceInst.addEffect(noise) != nil\n"
        "var disp = DisplacementMap(amount=0.8)\n"
        "print baseInst.addEffect(disp) != nil\n"
        "print disp.setSource(sourceInst)\n"
        "print disp.getSourceClipId() > 0\n"
        "proj.preview(0, 1)\n";
    ScriptSession session;
    Value tl_value;
    Value base_inst_value;
    Value source_inst_value;
    Value disp_value;
    ObjTimeline* timeline_handle;
    ObjTimelineClip* base_inst_handle;
    ObjTimelineClip* source_inst_handle;
    ObjEffectHandle* disp_handle;
    TimelineClip* base_clip;
    TimelineClip* source_clip;
    script_session_init(&session, true);
    script_session_run(&session, source);

    EXPECT_TRUE(session.compiled);
    EXPECT_INT_EQ(session.result, INTERPRET_OK);
    EXPECT_TRUE(session_get_global(&session, "tl", &tl_value));
    EXPECT_TRUE(session_get_global(&session, "baseInst", &base_inst_value));
    EXPECT_TRUE(session_get_global(&session, "sourceInst", &source_inst_value));
    EXPECT_TRUE(session_get_global(&session, "disp", &disp_value));
    timeline_handle = (ObjTimeline*)instance_get_handle(&session, tl_value);
    base_inst_handle = (ObjTimelineClip*)instance_get_handle(&session, base_inst_value);
    source_inst_handle = (ObjTimelineClip*)instance_get_handle(&session, source_inst_value);
    disp_handle = (ObjEffectHandle*)instance_get_handle(&session, disp_value);
    EXPECT_TRUE(timeline_handle != NULL);
    EXPECT_TRUE(base_inst_handle != NULL);
    EXPECT_TRUE(source_inst_handle != NULL);
    EXPECT_TRUE(disp_handle != NULL);
    base_clip = resolve_clip_handle(base_inst_handle, NULL, NULL);
    source_clip = resolve_clip_handle(source_inst_handle, NULL, NULL);
    EXPECT_TRUE(base_clip != NULL);
    EXPECT_TRUE(source_clip != NULL);
    EXPECT_TRUE(source_clip->effectChain != NULL);
    EXPECT_TRUE(strcmp(source_clip->effectChain->name, "FractalNoise") == 0);
    EXPECT_TRUE(disp_handle->effect->processor->get_source_clip != NULL);
    EXPECT_INT_EQ((int)disp_handle->effect->processor->get_source_clip(disp_handle->effect->data), (int)source_clip->id);
    script_session_dispose(&session);
}

int main(void) {
    run_test(test_clip_model_defaults, "clip model defaults");
    run_test(test_animation_model_behaviour, "animation model behaviour");
    run_test(test_timeline_model_behaviour, "timeline model behaviour");
    run_test(test_language_and_stdlib_script, "language and stdlib integration");
    run_test(test_video_bindings_script, "video bindings integration");
    run_test(test_displacement_uses_post_effect_source, "displacement post-effect source");
    run_test(test_repository_sample_scripts, "repository sample scripts");

    if (g_failures > 0) {
        fprintf(stderr, "\n%d/%d tests failed\n", g_failures, g_tests_run);
        return 1;
    }

    printf("\nAll %d tests passed.\n", g_tests_run);
    return 0;
}
