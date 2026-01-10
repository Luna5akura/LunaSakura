// src/main.c

#include <sys/stat.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include "core/memory.h"
#include "core/compiler/compiler.h"
#include "engine/compositor.h"
// 全局 VA 显示
// (Removed: VADisplay g_va_display = NULL;)
// --- 外部绑定函数声明 ---
// [新增] 注册标准库绑定 (List, etc.)
void registerStdBindings(VM* vm);
// [原有] 注册视频引擎绑定 (Video, Project, etc.)
void registerVideoBindings(VM* vm);
// --- 引擎接口声明 ---
Project* get_active_project(VM* vm);
void reset_active_project(VM* vm);
// Timeline* get_active_timeline(VM* vm);
// void reset_active_timeline(VM* vm);
// --- 辅助函数 ---
time_t get_file_mtime(const char* path) {
    struct stat attr;
    if (stat(path, &attr) == 0) return attr.st_mtime;
    return 0;
}
static char* readFile(VM* vm, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    char* buffer = ALLOCATE(vm, char, fileSize + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}
// --- 主程序 ---
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: luna [script]\n");
        return 64;
    }
    const char* script_path = argv[1];
    printf("=== Luna Hot-Reload Host (OpenGL) ===\n");
    // 1. 初始化 SDL (GL 模式)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL Init Failed: %s\n", SDL_GetError());
        return 1;
    }
    // 设置 OpenGL 属性 (3.3 Core)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    int win_w = 1280, win_h = 720;
    SDL_Window* window = SDL_CreateWindow("Luna Live Preview",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          win_w, win_h,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
 
    // 创建 GL Context
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "GL Context Failed: %s\n", SDL_GetError());
        return 1;
    }
 
    // 初始化 GLAD
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }
 
    // (Removed: VA-API initialization block)
 
    // 开启垂直同步
    SDL_GL_SetSwapInterval(1);
    // 2. 状态变量
    bool app_running = true;
    bool script_loaded = false;
    double current_time = 0.0;
    bool paused = false;
    uint64_t last_perf = SDL_GetPerformanceCounter();
    time_t last_mtime = 0;
 
    Compositor* comp = NULL;
    Project* current_pr = NULL;
    // 本地 VM 实例
    // 必须初始化为 0，防止在第一次加载前（如果文件不存在）调用 freeVM 导致崩溃
    VM vm;
    memset(&vm, 0, sizeof(VM));
    bool vm_initialized = false;
    // === 主循环 ===
    while (app_running) {
        // --- A. 检查文件变化 & 重载 ---
        time_t now_mtime = get_file_mtime(script_path);
        if (now_mtime > last_mtime) {
            printf("\n[Reload] Recompiling...\n");
         
            // 重要：在重置 VM 前，必须先释放旧的 Compositor (因为它持有 VM 分配的内存或资源)
            if (comp) { compositor_free(&vm, comp); comp = NULL; }
         
            // 释放旧 VM
            if (vm_initialized) { freeVM(&vm); }
          
            // 初始化新 VM
            initVM(&vm);
            vm_initialized = true;
         
            reset_active_project(&vm);
         
            // [新增] 注册标准库 (List, etc.)
            registerStdBindings(&vm);
          
            // 注册视频绑定
            registerVideoBindings(&vm);
          
            // 读取并编译运行脚本
            char* source = readFile(&vm, script_path);
            if (source) {
                Chunk chunk;
                initChunk(&chunk);
              
                if (compile(&vm, source, &chunk)) {
                    if (interpret(&vm, &chunk) == INTERPRET_OK) {
                        script_loaded = true;
                    } else script_loaded = false;
                } else script_loaded = false;
             
                freeChunk(&vm, &chunk);
                void* unused = reallocate(&vm, source, strlen(source) + 1, 0); // 捕获返回值
                UNUSED(unused);
            }
            if (script_loaded) {
                current_pr = get_active_project(&vm);
                if (current_pr && current_pr->timeline) { // 确保Timeline已设置
                    printf("[Reload] Project: %dx%d @ %.2f fps\n", current_pr->width, current_pr->height, current_pr->fps);
                    u32 target_w = current_pr->timeline ? current_pr->timeline->width : current_pr->width;
                    u32 target_h = current_pr->timeline ? current_pr->timeline->height : current_pr->height;
                    if (target_w != (u32)win_w || target_h != (u32)win_h) {
                        win_w = target_w;
                        win_h = target_h;
                        SDL_SetWindowSize(window, win_w, win_h);
                    }
                    comp = compositor_create(&vm, current_pr->timeline); // 使用持有的Timeline
                }
            }
            last_mtime = now_mtime;
        }
        // --- B. SDL 事件 ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) app_running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = event.window.data1;
                win_h = event.window.data2;
                glViewport(0, 0, win_w, win_h);
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_SPACE) paused = !paused;
                if (event.key.keysym.sym == SDLK_LEFT) current_time -= 1.0;
                if (event.key.keysym.sym == SDLK_RIGHT) current_time += 1.0;
                if (event.key.keysym.sym == SDLK_r) {
                    current_time = 0.0;
                    last_mtime = 0; // 强制重载
                }
            }
        }
        // --- C. 渲染 ---
        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)((now - last_perf) * 1000 / SDL_GetPerformanceFrequency()) / 1000.0;
        last_perf = now;
        if (script_loaded && comp && current_pr && current_pr->timeline) {
            if (!paused) {
                current_time += dt;
                // [修改] 循环逻辑
                double loop_end = current_pr->timeline->duration;
                double loop_start = 0.0;
                // 如果启用了范围预览，且范围有效
                if (current_pr->use_preview_range && current_pr->preview_end <= loop_end) {
                    loop_start = current_pr->preview_start;
                    loop_end = current_pr->preview_end;
                }
                // 处理循环
                if (current_time >= loop_end) {
                    current_time = loop_start;
                }
            }
           
            // [修改] 确保时间不低于起点 (处理热重载或手动seek导致的越界)
            double min_time = current_pr->use_preview_range ? current_pr->preview_start : 0.0;
            if (current_time < min_time) {
                current_time = min_time;
            }
            compositor_render(comp, current_time);
            compositor_blit_to_screen(comp, win_w, win_h);
        } else {
            // 空闲/错误状态：红色背景
            glClearColor(0.2f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        SDL_GL_SwapWindow(window); // 交换缓冲区
    }
    // --- 清理 ---
    if (comp) compositor_free(&vm, comp);
    // (Removed: vaTerminate(g_va_display);)
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
 
    if (vm_initialized) freeVM(&vm);
 
    SDL_Quit();
    return 0;
}