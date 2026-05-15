基于我们刚才完成的架构重构和代码清理，以下是各个核心文件的简短功能说明，按照模块分类：

### 1. Engine Core (引擎入口)
*   **`src/engine/engine.h`**:
    **引擎总头文件**。聚合了所有子模块的头文件，定义了引擎的上下文结构，供外部（如绑定层）一次性引入。

### 2. Model (纯数据模型层)
> **特点**：不依赖虚拟机，纯 C 数据结构。
*   **`src/engine/model/clip.h / .c`**:
    **素材模型**。定义单个媒体文件的基础属性（路径、原始时长、分辨率、是否有音轨等）。
*   **`src/engine/model/timeline.h / .c`**:
    **时间轴模型**。管理多轨道（Track）和片段（TimelineClip）的排列、插入、删除和排序逻辑。
*   **`src/engine/model/transform.h`**:
    **变换结构**。定义位置（x, y）、缩放、旋转和透明度的数据结构，内存对齐以利于计算。
*   **`src/engine/model/project.h`**:
    **项目配置**。持有时间轴实例以及全局设置（画布分辨率、帧率、预览范围）。

### 3. Media (媒体处理层)
> **特点**：基于 FFmpeg 和 SDL 处理音视频 I/O。
*   **`src/engine/media/utils/ffmpeg_utils.h / .c`**:
    **FFmpeg 封装工具**。提供统一的打开文件、查找流、初始化编解码器上下文的辅助函数，简化错误处理。
*   **`src/engine/media/utils/probe.h / .c`**:
    **元数据探测器**。在不完全解码的情况下，快速读取视频文件的宽、高、FPS 和时长信息。
*   **`src/engine/media/codec/decoder.h / .c`**:
    **解码器**。负责后台线程读取视频文件，维护帧队列，输出原始 YUV 图像数据和 PCM 音频数据。
*   **`src/engine/media/codec/encoder.h / .c`**:
    **编码器**。负责将原始图像数据（RGB 或 YUV）压缩编码为 H.264/MP4 文件，支持格式自动转换。
*   **`src/engine/media/audio/mixer.h / .c`**:
    **音频混音器**。管理 SDL 音频设备，将多个解码器的音频流进行实时叠加（混音）并输出到扬声器。

### 4. Render (渲染层)
> **特点**：基于 OpenGL 处理图像合成。
*   **`src/engine/render/compositor.h / .c`**:
    **合成器**。渲染管线的核心。根据时间轴数据，驱动解码器获取纹理，运行 Shader 处理位置变换和图层叠加，将结果绘制到 FBO 或屏幕。

### 5. Service (业务服务层)
> **特点**：组合上述模块实现高级功能。
*   **`src/engine/service/exporter.h / .c`**:
    **导出服务**。协调 Compositor 和 Encoder，逐帧渲染时间轴内容并写入最终的视频文件。
*   **`src/engine/service/transcoder.h / .c`**:
    **转码服务**。高效的“解码 -> 编码”管道。用于素材格式转换或剪切，绕过渲染层以获得最高速度。
*   **`src/engine/service/preview.h / .c`**:
    **预览服务**。提供一个独立的调试窗口，用于播放和查看单个素材的内容。

### 6. Bridge (虚拟机桥接层)
> **特点**：连接 Core 和 Engine，管理生命周期。
*   **`src/engine/bridge/object.h`**:
    **对象定义**。定义虚拟机可识别的结构体（`ObjClip`, `ObjTimeline`），它们内部持有指向 Model 层纯数据的指针。
*   **`src/engine/bridge/bridge.c`**:
    **生命周期管理**。实现这些对象的创建函数，以及垃圾回收（GC）所需的标记（Mark）和释放（Free）回调，确保 C 内存不泄漏。

以下是 **Core (虚拟机核心层)** 各个模块的职责总结，以及对解耦情况的分析。

### 1. 基础数据表示 (Data Representation)
> **职责**：定义语言中“数据”如何在内存中存在。

*   **`src/core/value.h / .c`**:
    *   定义了 `Value` 类型。使用了 **NaN Boxing** 技术（`u64` 既能存双精度浮点数，也能利用 NaN 的位模式存指针、布尔值、Nil），这是极致的内存优化。
    *   **解耦性**：最底层的基础设施，不依赖任何上层逻辑。
*   **`src/core/object.h / .c`**:
    *   定义了所有堆上分配的对象结构：`ObjString`, `ObjList`, `ObjFunction`, `ObjClass` 等。
    *   **关键解耦点**：定义了 **`ObjForeign`** 和 **`ForeignClassMethods`**。
        *   Core 层只知道 `ObjForeign` 有一个 `methods` 指针（包含 `mark` 和 `free` 回调）。
        *   Core 层**完全不知道** `Clip` 或 `Timeline` 的存在，它只负责分配 `size` 大小的内存，并在 GC 时回调宿主的函数。
*   **`src/core/chunk.h / .c`**:
    *   定义字节码容器 (`Chunk`) 和指令集 (`OpCode`)。
    *   负责存储编译后的指令流和常量池。

### 2. 内存管理 (Memory Management)
> **职责**：掌管内存分配与回收，确保宿主资源被正确释放。

*   **`src/core/memory.h / .c`**:
    *   实现了 **Mark-and-Sweep (标记-清除)** 垃圾回收器。
    *   实现了 **增量标记 (Incremental Marking)**，避免 GC 造成的卡顿（这对视频渲染引擎至关重要）。
    *   **解耦性**：当 GC 扫描到 `OBJ_FOREIGN` 时，它盲目地调用 `foreign->methods->mark(...)`。这使得 `engine` 层可以将自己的资源（如 `Timeline` 中的 `Clip`）加入 GC 追踪链，而不需要修改 Core 的代码。

### 3. 编译器前端 (Compiler / Front-end)
> **职责**：将源代码文本转换为字节码。

*   **`src/core/scanner.h / .c`**:
    *   词法分析器。处理 Python 风格的缩进（Indent/Dedent），生成 Token 流。
*   **`src/core/compiler/compiler.c` (及 `_emit`, `_expr`, `_stmt`, `_resolve`)**:
    *   单遍编译器 (Single-pass Compiler)。直接将 Token 编译为 Bytecode，没有显式的 AST（抽象语法树），速度极快。
    *   支持复杂的语法特性：Lambda 表达式、列表推导式、默认参数、关键字参数。
    *   **解耦性**：编译器只生成通用的字节码（如 `OP_CALL`, `OP_GET_PROPERTY`）。它不校验方法是否存在（如 `clip.trim`），这些检查推迟到运行时。因此编译器不需要知道具体的宿主 API。

### 4. 虚拟机运行时 (VM Runtime / Back-end)
> **职责**：执行字节码，维护调用栈。

*   **`src/core/vm/vm.c`**:
    *   解释器主循环。使用了 **Computed Goto** (在 GCC/Clang 下) 进行指令分发优化。
    *   维护全局状态（Global Variables, Strings Interning）。
*   **`src/core/vm/vm_handler.h`**:
    *   包含所有 OpCode 的具体实现宏。逻辑与主循环分离，便于维护。
*   **`src/core/vm/call_utils.c`**:
    *   处理复杂的函数调用逻辑，包括参数绑定、关键字参数匹配、默认值填充。

### 5. 基础设施 (Infrastructure)
*   **`src/core/table.c`**: 哈希表实现，用于变量存储、属性查找。
*   **`src/core/vm/error.c`**: 运行时错误处理和栈回溯打印。
