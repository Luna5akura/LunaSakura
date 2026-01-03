// src/binding/bind_video.c
#include <stdio.h>
#include "vm/vm.h"
#include "vm/object.h"

// 声明外部函数 (来自 ffmpeg_dec.c)
// 最好把它放在 src/engine/video_engine.h 头文件里，这里偷懒直接声明
typedef struct {
    double duration;
    int width;
    int height;
    double fps;
    bool success;
} VideoMeta;

VideoMeta load_video_metadata(const char* filepath);

void play_video_clip(ObjClip* clip);

void export_video_clip(ObjClip* clip, const char* output_filename);

Value nativeCreateClip(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }

    ObjString* path = AS_STRING(args[0]);
    
    // 1. 调用 FFmpeg 读取真实信息
    VideoMeta meta = load_video_metadata(path->chars);
    
    if (!meta.success) {
        printf("Runtime Error: Failed to load video '%s'\n", path->chars);
        return NIL_VAL; // 或者抛出异常
    }

    // 2. 创建对象并填充真实数据
    ObjClip* clip = newClip(path);
    clip->duration = meta.duration;
    clip->width = meta.width;
    clip->height = meta.height;
    clip->fps = meta.fps;

    printf("[Native] Loaded: %s (%.2fs, %dx%d, %.2f fps)\n", 
           path->chars, clip->duration, clip->width, clip->height, clip->fps);

    return OBJ_VAL(clip);
}

Value nativePreview(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;

    if (IS_CLIP(args[0])) {
        // 直接传 Clip 对象
        play_video_clip(AS_CLIP(args[0]));
    } 
    // 暂时移除对纯字符串的支持，强制用户创建 Clip 对象
    else {
        printf("Error: preview() requires a Clip object.\n");
    }
    return NIL_VAL;
}

Value nativeTrim(int argCount, Value* args) {
    // 1. 参数校验
    if (argCount != 3) {
        printf("Error: trim() takes 3 arguments (clip, start, duration).\n");
        return NIL_VAL;
    }
    if (!IS_CLIP(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        printf("Error: trim() arguments must be (Clip, Number, Number).\n");
        return NIL_VAL;
    }

    // 2. 获取参数
    ObjClip* clip = AS_CLIP(args[0]);
    double start = AS_NUMBER(args[1]);
    double duration = AS_NUMBER(args[2]);

    // 3. 逻辑检查
    if (start < 0) start = 0;
    // 如果剪辑时长超过了素材原本的剩余时长，就截断
    // (这里 clip->duration 是素材原始时长)
    
    // 4. 修改 Clip 对象的核心属性
    clip->in_point = start;     // 素材从第几秒开始读
    clip->duration = duration;  // 这个 Clip 在轨道上持续多久

    printf("[Native] Trimmed clip: start=%.2fs, duration=%.2fs\n", 
           clip->in_point, clip->duration);

    return NIL_VAL;
}

Value nativeExport(int argCount, Value* args) {
    if (argCount != 2) {
        printf("Error: export() takes 2 arguments.\n");
        return NIL_VAL;
    }
    
    if (!IS_CLIP(args[0]) || !IS_STRING(args[1])) {
        printf("Error: export() types must be (Clip, String).\n");
        return NIL_VAL;
    }

    ObjClip* clip = AS_CLIP(args[0]);
    ObjString* filename = AS_STRING(args[1]);

    export_video_clip(clip, filename->chars);
    
    return NIL_VAL;
}