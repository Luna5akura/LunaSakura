// src/main.c

#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include <glad/glad.h>

#include "core/memory.h"
#include "core/compiler/compiler.h"
#include "core/vm/vm.h"

// [新增] 必须包含这两个头文件，否则编译器不知道 Project 和 Timeline 的成员结构
#include "engine/timeline.h"
#include "engine/object.h" 
#include "engine/compositor.h"

// --- 外部绑定函数声明 ---
// 这些函数通常在 bind_std.c 和 bind_video.c 中实现

// 注册标准库绑定 (List, Map, String utils etc.)
void registerStdBindings(VM* vm);

// 注册视频引擎绑定 (Video, Project, Timeline etc.)
void registerVideoBindings(VM* vm);

// 获取当前脚本创建的活跃 Project C结构体 (需在 bind_video.c 中实现并导出)
Project* get_active_project(VM* vm);
// 重置活跃 Project (需在 bind_video.c 中实现并导出)
void reset_active_project(VM* vm);


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
    VM vm;
    memset(&vm, 0, sizeof(VM));
    bool vm_initialized = false;

    // === 主循环 ===
    while (app_running) {
        // --- A. 检查文件变化 & 重载 ---
        time_t now_mtime = get_file_mtime(script_path);
        
        // 如果文件更新或尚未加载
        if (now_mtime > last_mtime || last_mtime == 0) {
            if (last_mtime != 0) printf("\n[Reload] Recompiling...\n");
            else printf("\n[Init] Compiling...\n");

            // 1. 清理旧资源
            // 必须在释放 VM 之前释放 Compositor，因为 Compositor 可能引用了 VM 里的资源
            if (comp) { 
                compositor_free(&vm, comp); 
                comp = NULL; 
            }

            // 释放旧 VM
            if (vm_initialized) { 
                freeVM(&vm); 
            }

            // 2. 初始化新环境
            initVM(&vm);
            vm_initialized = true;

            // 清除旧的 Project 指针引用 (bind_video 内部的静态变量也需要在 initVM 后重新设置，
            // 但如果你使用的是静态变量，可能需要显式调用 reset)
            reset_active_project(&vm);

            // 注册绑定
            registerStdBindings(&vm);
            registerVideoBindings(&vm);

            // 3. 编译并运行脚本
            char* source = readFile(&vm, script_path);
            if (source) {
                Chunk chunk;
                initChunk(&chunk);

                if (compile(&vm, source, &chunk)) {
                    // 执行脚本，脚本执行过程中会调用 newProject 创建 active_project
                    if (interpret(&vm, &chunk) == INTERPRET_OK) {
                        script_loaded = true;
                    } else {
                        script_loaded = false;
                        printf("[Error] Runtime error.\n");
                    }
                } else {
                    script_loaded = false;
                    printf("[Error] Compile error.\n");
                }

                freeChunk(&vm, &chunk);
                // 释放源代码 buffer (reallocate newSize=0 即 free)
                reallocate(&vm, source, strlen(source) + 1, 0);
            }

            // 4. 加载后处理
            if (script_loaded) {
                // 从 bind_video 模块获取当前创建的 Project
                current_pr = get_active_project(&vm);

                if (current_pr && current_pr->timeline) {
                    printf("[Reload] Project: %dx%d @ %.2f fps\n", 
                           current_pr->width, current_pr->height, current_pr->fps);
                    
                    // 根据 Project 调整窗口大小
                    u32 target_w = current_pr->width;
                    u32 target_h = current_pr->height;
                    
                    if ((u32)win_w != target_w || (u32)win_h != target_h) {
                        win_w = target_w;
                        win_h = target_h;
                        SDL_SetWindowSize(window, win_w, win_h);
                    }

                    // 创建合成器
                    comp = compositor_create(&vm, current_pr->timeline);
                } else {
                    printf("[Warning] Script executed but no active project/timeline found.\n");
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
                    last_mtime = 0; // 触发重载
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

                // 循环逻辑
                double loop_end = current_pr->timeline->duration;
                double loop_start = 0.0;

                // 如果 Project 启用了预览范围
                if (current_pr->use_preview_range && current_pr->preview_end > current_pr->preview_start) {
                    loop_start = current_pr->preview_start;
                    // 如果预览结束点超过了时间轴总长，通常还是以预览结束点为准，或者取最小值
                    // 这里假设预览范围优先
                    loop_end = current_pr->preview_end; 
                }

                // 只有当有长度时才循环
                if (loop_end > loop_start) {
                    if (current_time >= loop_end) {
                        current_time = loop_start;
                    }
                }
            }

            // 范围钳制 (Clamp)
            double min_time = current_pr->use_preview_range ? current_pr->preview_start : 0.0;
            if (current_time < min_time) current_time = min_time;

            // 调用合成器渲染
            compositor_render(comp, current_time);
            
            // 显示到屏幕
            compositor_blit_to_screen(comp, win_w, win_h);
        
        } else {
            // 错误/空闲状态：深红色背景
            glClearColor(0.2f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        SDL_GL_SwapWindow(window);
    }

    // --- 清理 ---
    if (comp) compositor_free(&vm, comp);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);

    if (vm_initialized) freeVM(&vm);

    SDL_Quit();
    return 0;
}