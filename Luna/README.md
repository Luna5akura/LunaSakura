### 一、 核心问题：用 C 语言合理吗？

**合理性分析：**
*   **优点：**
    *   **极致性能：** 视频合成涉及每秒处理数千万像素，C 语言没有垃圾回收（GC）卡顿，这对实时预览至关重要。
    *   **生态亲和：** 最核心的库 **FFmpeg (libav*)** 是 C 写的，**OpenGL/Vulkan** (渲染) 也是 C 接口，结合起来非常顺畅。
    *   **底层控制：** 你可以精准控制内存布局，优化图像缓冲区（Buffer）。
*   **挑战：**
    *   **文本解析：** 用 C 写一个 Python 风格（缩进敏感）的解析器比较繁琐（相比于 Rust/C++ 或用 Flex/Bison）。
    *   **内存管理：** 你需要手动管理每一帧图像的 `malloc` 和 `free`，一旦内存泄漏，预览就会崩。
    *   **开发效率：** 字符串处理、哈希表等高级数据结构需要自己造轮子或引用第三方库（推荐 `Glib` 或 `stb` 库）。

**结论：** 如果你追求高性能和底层掌控感，C 是完美选择。但建议不要从零写**所有东西**，**核心解码**请调用 FFmpeg 库，**窗口显示**请调用 SDL2 或 GLFW。

---

### 二、 语言设计（Python-like）构想

在开始写代码前，你需要确定你的 DSL（领域特定语言）长什么样。既然是 Python 风格，它应该具备：
1.  **缩进表示层级**。
2.  **清晰的函数调用**。
3.  **链式调用**（可选，但这在流媒体处理中很流行）。

**示例代码构想：**

```python
# project.ve (Video Edit)

setup:
    resolution: 1920x1080
    fps: 30

def intro_effect(clip):
    # 定义一个可复用的函数
    return clip.fade_in(duration=1.0).scale(1.2)

track "Background":
    # 加载素材，裁剪前5秒，循环播放
    c = clip("bg.mp4").trim(0, 5).loop()
    render c

track "Main":
    # 按照时间顺序排列
    c1 = clip("camera_01.mov").at(0)
    c2 = clip("camera_02.mov").at(5)
    
    # 应用自定义函数
    render intro_effect(c1)
    render c2.grayscale()
```

---

### 三、 开发清单 (Roadmap)

要把这个做出来，你需要完成以下五个核心模块。我按开发顺序排列：

#### 阶段 1：语言解析器 (The Parser)
你需要将上面的文本转换成 C 语言能理解的数据结构。
*   [ ] **词法分析器 (Lexer):** 读取源代码，识别 `Identifier`(track, clip), `Number`, `String`, `Indentation` (关键！)。
*   [ ] **语法分析器 (Parser):** 构建**抽象语法树 (AST)**。
    *   你需要处理 Python 的缩进逻辑（栈结构记录缩进层级）。
*   [ ] **指令生成 (Compiler/Interpreter):** 将 AST 转换为内部指令列表。
    *   *例如：* `OP_LOAD_CLIP`, `OP_SET_TIME`, `OP_APPLY_FILTER`.

#### 阶段 2：数据结构与时间轴 (The Timeline Model)
这是视频编辑的核心逻辑，与渲染无关，纯粹是数学和逻辑。
*   [ ] **Clip 结构体:** 包含 `path`, `in_point` (素材入点), `out_point`, `start_time` (轨道时间), `layer_index`.
*   [ ] **Track 结构体:** 一个 Track 包含多个 Clip。
*   [ ] **Project 结构体:** 包含全局设置（宽、高、FPS）和所有 Tracks。
*   [ ] **核心算法:** 给定一个时间戳 `T`，查询所有 Track，找出当前时刻应该显示哪些 Clip。

#### 阶段 3：渲染引擎 (The Render Engine) —— 最硬核的部分
这里你需要引入 **FFmpeg (libavcodec, libavformat)**。
*   [ ] **解码器封装:** 编写 C 函数，输入文件路径和时间戳，返回该时刻的图像数据 (RGB/YUV Buffer)。
    *   *难点：* 视频的关键帧（I-Frame）定位。如果你要跳到第 100 帧，可能需要从第 90 帧（关键帧）开始解码。需要做**缓存机制**。
*   [ ] **图像处理管道:**
    *   **缩放/裁剪:** 实现 `resize` 算法 (或调用 `libswscale`).
    *   **图层混合 (Blender):** 实现 Alpha Blending 算法，将上层像素覆盖在下层像素上。
*   [ ] **音频混合器:** 这是一个大坑。你需要将多轨音频的 PCM 数据进行加法混合，并处理采样率转换。

#### 阶段 4：实时预览与热更新 (Hot Reload Player)
为了实现“代码改动，立即生效”，你需要一个 GUI 窗口。
*   [ ] **窗口系统:** 使用 **SDL2** 或 **GLFW** 创建窗口。
*   [ ] **纹理上传:** 将合成好的 CPU 像素数据上传到 GPU (OpenGL Texture) 进行显示。
*   [ ] **文件监听器 (File Watcher):**
    *   使用操作系统的 API (Linux `inotify`, Windows `ReadDirectoryChangesW`, macOS `FSEvents`) 监听源代码文件。
    *   当文件变动时 -> 重新触发 Parser -> 生成新的 Timeline 数据结构 -> 刷新当前帧画面 (不要重置播放进度！)。

#### 阶段 5：导出 (Export)
*   [ ] **编码器:** 将渲染好的每一帧输送给 FFmpeg 编码器 (H.264/H.265)。
*   [ ] **封装:** 将视频流和音频流封装成 `.mp4` 或 `.mov`。

---

### 四、 给 C 语言开发者的特别建议

1.  **不要手写链表：** 视频剪辑涉及大量的列表操作（轨道上的片段）。建议直接使用成熟的 C 库，比如 `utlist` (宏实现的链表) 或者 `Glib` 的 `GList`，甚至可以自己写一个简单的动态数组（Vector）。
2.  **利用 SDL2：** SDL2 非常适合这个项目。它不仅能处理窗口和图形显示，还能处理**音频播放**和**事件循环**。用 SDL2 开发预览器能省掉一半的工作量。
3.  **脚本与引擎分离：**
    *   建议你的程序架构是：`main.c` (主循环) + `parser.c` (解析) + `engine.c` (视频处理)。
    *   热更新时，你只需要销毁旧的 `Project` 结构体，重新运行 `parser` 生成新的结构体，而不需要重启整个程序。

