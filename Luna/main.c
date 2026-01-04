// src/main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // 用于获取文件时间
#include <time.h>
#include <SDL2/SDL.h>
#include "common.h"
#include "vm/vm.h"
#include "vm/compiler.h" // 新增：为 compile 函数
#include "engine/timeline.h"
#include "engine/compositor.h"

// 来自 bind_video.c 的外部函数
void registerVideoBindings();
Timeline* get_active_timeline();
void reset_active_timeline();

// 声明全局 vm（在 vm.c 中定义）
extern VM vm;

// --- 文件监听工具 ---
time_t get_file_mtime(const char* path) {
    struct stat attr;
    if (stat(path, &attr) == 0) {
        return attr.st_mtime;
    }
    return 0;
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    char* buffer = (char*)malloc(fileSize + 1);
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

// --- 核心逻辑 ---
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: luna [script]\n");
        return 64;
    }
    const char* script_path = argv[1];
    printf("=== Luna Hot-Reload Host ===\n");
    printf("Watching: %s\n", script_path);
    // 1. 初始化 SDL (只初始化一次窗口)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL Init Failed: %s\n", SDL_GetError());
        return 1;
    }
    // 默认窗口大小，后续会根据 Timeline 调整
    int win_w = 1280, win_h = 720;
    SDL_Window* window = SDL_CreateWindow("Luna Live Preview",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          win_w, win_h,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* screen_texture = NULL; // 每次重载可能需要重建(如果分辨率变了)
    // 2. 状态变量
    bool app_running = true;
    bool script_loaded = false;
  
    // 播放控制
    double current_time = 0.0;
    bool paused = false;
    uint64_t last_perf = SDL_GetPerformanceCounter();
    // 热重载控制
    time_t last_mtime = 0;
    Compositor* comp = NULL;
    Timeline* current_tl = NULL;
    // === 主循环 ===
    while (app_running) {
      
        // --- A. 检查文件变化 & (重)加载脚本 ---
        time_t now_mtime = get_file_mtime(script_path);
      
        // 如果文件更新了，或者脚本还没加载
        if (now_mtime > last_mtime) {
            printf("\n[Reload] Detected change. Recompiling...\n");
          
            // 1. 清理旧资源
            if (comp) { compositor_free(comp); comp = NULL; }
            if (script_loaded) { freeVM(&vm); } // 使用全局 vm
           
            // 2. 初始化新环境
            initVM(&vm); // 使用全局 vm
            reset_active_timeline();
            registerVideoBindings(); // 重新绑定
            // 3. 读取并运行脚本
            char* source = readFile(script_path);
            if (source) {
                Chunk chunk;
                initChunk(&chunk);
                if (compile(source, &chunk)) {
                    if (interpret(&vm, &chunk) == INTERPRET_OK) { // 使用全局 vm
                        script_loaded = true;
                    } else {
                        printf("[Error] Execution Failed. Waiting for fix...\n");
                        script_loaded = false;
                    }
                } else {
                    printf("[Error] Compile Failed. Waiting for fix...\n");
                    script_loaded = false;
                }
                freeChunk(&chunk);
                free(source);
            }
            // 4. 获取新 Timeline
            if (script_loaded) {
                current_tl = get_active_timeline();
                if (current_tl) {
                    printf("[Reload] Success! Timeline: %dx%d @ %.2f fps\n",
                           current_tl->width, current_tl->height, current_tl->fps);
                    // 重建合成器
                    comp = compositor_create(current_tl);
                    // 调整窗口/纹理 (如果分辨率变了)
                    if (current_tl->width != (u32)win_w || current_tl->height != (u32)win_h) {
                        win_w = current_tl->width;
                        win_h = current_tl->height;
                        SDL_SetWindowSize(window, win_w, win_h);
                        if (screen_texture) SDL_DestroyTexture(screen_texture);
                        screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                           SDL_TEXTUREACCESS_STREAMING,
                                                           win_w, win_h);
                    }
                    if (!screen_texture) {
                         screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                                           SDL_TEXTUREACCESS_STREAMING,
                                                           win_w, win_h);
                    }
                } else {
                    printf("[Warning] Script executed but no Timeline was previewed.\n");
                    script_loaded = false;
                }
            }
          
            last_mtime = now_mtime;
            // 注意：不重置 current_time，实现无缝播放
        }
        // --- B. SDL 事件处理 ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) app_running = false;
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_SPACE) paused = !paused;
                if (event.key.keysym.sym == SDLK_LEFT) current_time -= 1.0;
                if (event.key.keysym.sym == SDLK_RIGHT) current_time += 1.0;
                if (event.key.keysym.sym == SDLK_r) {
                    current_time = 0.0; // 手动重置时间
                    // 强制重载 (触碰 mtime hack)
                    last_mtime = 0;
                }
            }
        }
        // --- C. 渲染 (仅当脚本成功加载且 Timeline 有效时) ---
        if (script_loaded && comp && current_tl && screen_texture) {
            // 时间步进
            uint64_t now = SDL_GetPerformanceCounter();
            double dt = (double)((now - last_perf) * 1000 / SDL_GetPerformanceFrequency()) / 1000.0;
            last_perf = now;
            if (!paused) {
                current_time += dt;
                if (current_time > current_tl->duration) current_time = 0.0; // Loop
            }
            if (current_time < 0) current_time = 0.0;
            // 引擎核心渲染
            compositor_render(comp, current_time);
            // 显示
            void* pixels = compositor_get_buffer(comp);
            SDL_UpdateTexture(screen_texture, NULL, pixels, win_w * 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, screen_texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        } else {
            // 错误状态或空闲状态：显示个颜色标识
            SDL_SetRenderDrawColor(renderer, 50, 0, 0, 255); // 深红色表示错误/等待
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
            SDL_Delay(100); // 降低空转 CPU
            last_perf = SDL_GetPerformanceCounter(); // 防止 dt 激增
        }
        // --- D. 帧率控制 (简单 Sleep) ---
        // 生产环境应该用更精确的 vsync 等待，但为了文件 IO 检查不至于太频繁占 CPU
        // 我们每帧稍微 sleep 一下，或者每隔 N 帧检查一次文件。
        // 这里为了响应速度，每次循环只渲染一帧，如果渲染很快，循环就很快。
        // 添加一个简单的 Cap，避免渲染 1000fps 浪费电
        SDL_Delay(10);
    }
    // 3. 退出清理
    if (comp) compositor_free(comp);
    if (screen_texture) SDL_DestroyTexture(screen_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    freeVM(&vm); // 使用全局 vm
    SDL_Quit();
    return 0;
}