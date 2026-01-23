以下是各个模块和主要文件的职责介绍：

### 1. Core (虚拟机核心层)
> **职责**：负责脚本语言的编译、执行、内存管理和垃圾回收 (GC)。这部分代码不知道“视频剪辑”是什么，它只懂字节码和对象。

*   **`core/vm/`**: 虚拟机的运行时环境。
    *   `vm.c/h`: 解释器的主循环，负责执行指令、管理栈。
*   **`core/memory.c/h`**: 内存分配器。负责 `malloc`/`free` 以及最重要的 GC (标记-清除)。
*   **`core/object.h`**: 定义了脚本中对象的基础头结构 (`Obj`, `ObjString`, `ObjInstance` 等)。

### 2. Binding (绑定层 / API 适配层)
> **职责**：充当“翻译官”。它将脚本语言的函数调用（如 `clip.trim()`）翻译成 C 语言的底层操作，并将底层数据（C 结构体）包装成脚本对象返回。

*   **`binding/bind_video.c`**:
    *   注册类（Clip, Timeline, Project）到虚拟机中。
    *   解析脚本传递的参数（检查类型、转换数字等）。
    *   **关键点**：它持有 `ObjClip`（包装器），从中取出 `Clip*`（纯数据），然后传给 Engine 层。

### 3. Engine - Bridge (桥接层 / 对象定义层)
> **职责**：定义 C 引擎的数据如何在虚拟机中“存在”。它是连接 Core 和 Engine Model 的胶水。

*   **`engine/bridge/object.h`**:
    *   定义 `ObjClip`, `ObjTimeline`。
    *   这些结构体必须包含 `ObjForeign` 头，以便被 GC 管理。
    *   **重构后**：它们现在只持有一个指向底层纯数据的指针（如 `Clip* clip`），不再包含具体业务属性。
*   **`engine/bridge/bridge.c`**:
    *   实现 GC 的回调函数（`mark` 和 `free`）。当虚拟机想销毁 `ObjClip` 时，这里负责去 `free(clip)`。

### 4. Engine - Model (纯数据模型层) - *核心重构区域*
> **职责**：定义业务数据的**纯 C 结构**。这部分代码**不依赖**虚拟机的对象系统（`Obj`），理论上可以移植到任何 C 项目中。

*   **`engine/model/clip.h/c`**:
    *   定义 `struct Clip`。包含路径、时长、变换参数等纯数据。
    *   提供 `clip_create`/`clip_free`。
*   **`engine/model/timeline.h/c`**:
    *   定义 `struct Timeline` 和 `struct Track`。
    *   管理 `Clip*` 的集合，处理插入、删除、排序逻辑。
*   **`engine/model/transform.h`**:
    *   定义位置、缩放、旋转的数学结构。

### 5. Engine - Media (媒体处理层)
> **职责**：处理音视频文件的编解码 I/O。依赖 FFmpeg，服务于 Model。

*   **`engine/media/utils/ffmpeg_utils.c`**: 封装 FFmpeg 繁琐的初始化和打开文件流程。
*   **`engine/media/utils/probe.c`**: “探针”。只读不写，快速获取视频的宽高、时长、FPS。
*   **`engine/media/codec/decoder.c`**:
    *   **消费者**：读取 `Clip` 数据。
    *   **生产者**：输出 GL 纹理 (YUV) 和 音频 PCM 数据。
    *   实现核心的解码线程。
*   **`engine/media/codec/encoder.c`**:
    *   负责将 RGB 像素数据编码成 H.264/MP4 文件。
*   **`engine/media/audio/mixer.c`**:
    *   音频混音器。将多个 Decoder 的 PCM 数据叠加，输出到扬声器。

### 6. Engine - Render (渲染层)
> **职责**：处理图像合成。依赖 OpenGL。

*   **`engine/render/compositor.c`**:
    *   **输入**：`Timeline`（包含多个 Clip 的位置信息）+ 当前时间 `t`。
    *   **处理**：调用 `Decoder` 获取纹理，根据 `Transform` 运行 Shader 进行合成。
    *   **输出**：渲染到 FBO（用于导出）或 屏幕（用于预览）。

### 7. Engine - Service (业务服务层)
> **职责**：组合上述模块，提供高级功能。这是 `bind_video.c` 通常直接调用的地方。

*   **`engine/service/exporter.c`**:
    *   导出流程控制器。循环调用：`Compositor` 渲染一帧 -> `Encoder` 编码一帧。
*   **`engine/service/transcoder.c`**:
    *   转码服务。不经过渲染层，直接从 `Decoder` 管道接 `Encoder`，用于格式转换或剪切。
*   **`engine/service/preview.c`**:
    *   提供简单的调试预览窗口（SDL Window）。

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
