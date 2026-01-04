Depending:

```
common.h
value.h
chunk.h
table.h
scanner.h
object.h
memory.h
vm.h
timeline.h
value.c
chunk.c
memory.c
table.c
object.c
scanner.c
compiler.h
compiler.c
vm.c
video.h
bind_video.c
video_export.c
video_player.c
video_probe.c
timeline.c
compositor.h
compositor.c
main.c
```
这是一份为您定制的项目规划文档。你可以将其保存为 `README.md` 或 `docs/PLANNING.md`，用于梳理思路和指导后续开发。

---

# 🌙 Luna Video Engine - 开发规划与架构文档

**版本**: 0.5.0 (Alpha)
**更新日期**: 2024-01-XX
**定位**: 可编程、热重载、高性能的非线性编辑 (NLE) 引擎

---

## 1. 项目概览 (Overview)

Luna 是一个基于 C 语言开发的视频合成引擎，内置了一个定制的脚本语言（Luna Script）。
它的核心理念是 **"Code as Video"** —— 通过编写脚本来描述剪辑逻辑，支持实时热重载（Hot-Reload），实现类似 After Effects 的代码化、实时反馈的创作体验。

**核心技术栈**:
- **宿主语言**: C11 (追求极致性能与底层控制)
- **脚本虚拟机**: 自研基于栈的 VM (NaN Boxing, Mark-Sweep GC, Bytecode Compiler)
- **多媒体后端**: FFmpeg (解码/缩放), SDL2 (窗口/渲染/事件)
- **构建系统**: Make / CMake

---

## 2. 项目目录结构 (Project Structure)

项目采用了模块化分层设计，清晰地分离了脚本层、绑定层和引擎层。

```text
Luna/
├── src/
│   ├── main.c              # 程序入口 (宿主循环、文件监听、热重载逻辑)
│   │
│   ├── vm/                 # [大脑] 脚本虚拟机核心
│   │   ├── vm.c/h          # 字节码解释器
│   │   ├── compiler.c/h    # 编译器 (Token -> Bytecode)
│   │   ├── scanner.c/h     # 词法分析器
│   │   ├── object.c/h      # 对象系统 (String, List, Native, UserObj)
│   │   ├── memory.c/h      # 内存管理与 GC
│   │   └── chunk.h         # 指令块定义
│   │
│   ├── engine/             # [肌肉] 视频处理核心 (纯 C 实现，不依赖 VM)
│   │   ├── timeline.c/h    # 数据结构：轨道、Clip、时间轴管理
│   │   ├── compositor.c/h  # 渲染管线：FFmpeg 解码、色彩转换、混合
│   │   ├── video.h         # 视频 I/O 接口
│   │   └── video_probe.c   # 元数据探测
│   │
│   ├── binding/            # [神经] 胶水代码 (连接 VM 与 Engine)
│   │   └── bind_video.c    # 将 C 函数暴露给 Luna 脚本 (Video, Project, preview...)
│   │
│   └── common.h            # 通用宏与类型定义
│
├── include/                # 第三方头文件 (SDL2, FFmpeg)
├── demo.luna               # 测试脚本
└── makefile                # 构建脚本
```

---

## 3. 当前已实现功能 (Current Status)

截止至 **Milestone 0.5**，核心链路已打通，具备基础剪辑能力。

### ✅ 脚本语言层
- [x] **完整解释器**: 词法分析 -> 语法分析 -> 字节码编译 -> VM 执行。
- [x] **数据类型**: Number (double), Bool, String, Nil, Object (GC Managed).
- [x] **变量与控制流**: `var`, `print`, 以及基础运算。
- [x] **内存管理**: 基础的垃圾回收 (GC) 框架。

### ✅ 视频引擎层
- [x] **多轨道架构**: 支持无限轨道叠加 (Track/Layer)。
- [x] **素材加载**: 支持 MP4 等常见格式的解码 (FFmpeg)。
- [x] **时间轴编排**: 支持 `Trim` (裁剪)、`Offset` (入点偏移)。
- [x] **基础变换**: 支持 `Scale` (缩放) 和 `Position` (位移)。
- [x] **合成器**: 软件渲染管线，支持 YUV 到 RGBA 的转换与画面覆盖。

### ✅ 交互与工作流
- [x] **原生绑定**: `Video()`, `Project()`, `add()`, `trim()`, `setScale()`, `setPos()`。
- [x] **实时预览**: SDL2 窗口播放，支持暂停、跳转、循环。
- [x] **🔥 热重载 (Hot-Reload)**: 修改 `.luna` 脚本保存后，窗口不关闭，自动重编译并刷新画面，保持播放进度。

---

## 4. 接下来要做什么？ (Roadmap)

为了让 Luna 从一个“原型”变成一个真正可用的“工具”，以下是分阶段的开发计划：

### 阶段一：视觉强化 (Visual Polish) —— *当前优先级最高*
目前的合成器是简单的像素覆盖（`memcpy`），不支持透明度。
- [ ] **Alpha Blending (透明度混合)**: 实现标准的 `Src over Dst` 混合算法，支持 PNG 图片叠加和视频透明度。
- [ ] **Opacity (不透明度)**: 给 Clip 增加 `opacity` 属性和 `setOpacity()` API。
- [ ] **Rotation (旋转)**: 完善 Transform，支持视频旋转（需要更新 Compositor 的数学计算）。
- [ ] **Z-Index 控制**: 虽然有轨道，但需要确保渲染顺序严格遵循轨道层级。

### 阶段二：音频系统 (Audio System) —— *不可或缺*
没有声音的视频是没有灵魂的。
- [ ] **音频解码**: 使用 FFmpeg 解码音频流 (AAC/MP3/PCM)。
- [ ] **音频重采样**: 将不同素材的采样率统一 (如 44100Hz)。
- [ ] **音频混音**: 将多轨音频数据混合。
- [ ] **SDL 音频同步**: 确保画面和声音同步播放 (A/V Sync)。

### 阶段三：动画系统 (Animation / Keyframes) —— *对标 AE 的关键*
实现随时间变化的属性。
- [ ] **关键帧结构**: 设计 `Keyframe` 数据结构 (Time, Value, Easing)。
- [ ] **属性插值**: 在渲染循环中，根据当前时间计算属性值 (Lerp)。
- [ ] **脚本 API**: 例如 `animate(clip, "scale", 0.0, 1.0, 1.0, 2.0)` (从第0秒到第1秒，缩放从1.0变2.0)。

### 阶段四：导出与性能 (Export & Optimization)
- [ ] **工程导出**: 实现 `render(project, "output.mp4")`，将时间轴渲染为文件（目前只有简单的单个 Clip 导出）。
- [ ] **多线程解码**: 优化 FFmpeg 解码性能，使用解码线程池。
- [ ] **缓存机制**: 实现解码帧缓存 (LRU Cache)，避免卡顿。

---

## 5. 技术架构图 (Architecture)

```mermaid
graph TD
    subgraph "Editor / IDE (VSCode)"
        Code[Luna Script (.luna)]
    end

    subgraph "Luna Host (main.c)"
        Watcher[File Watcher] -->|Detect Change| Loader
        Loader[Script Loader] --> Compiler
        
        subgraph "Virtual Machine"
            Compiler -->|Bytecode| VM
            VM -->|API Calls| Binding
        end

        subgraph "Video Engine"
            Binding -->|Create/Config| Timeline
            Timeline --> Compositor
            FFmpeg[FFmpeg Decoders] -->|Raw Frames| Compositor
        end
        
        subgraph "Output"
            Compositor -->|RGBA Buffer| SDL[SDL2 Renderer]
            Compositor -->|Encoded Packet| File[MP4 File]
        end
    end

    Code --> Watcher
```

---

## 6. 开发建议

1.  **保持简单**: 现在的架构非常清晰，不要过早引入过度设计（比如极其复杂的 GUI）。专注于“脚本驱动”这一核心优势。
2.  **测试驱动**: 现在的 `test.mp4` 是你的好朋友。每增加一个功能（比如旋转），就写一个 `test_rotation.luna` 来验证。
3.  **内存泄漏监控**: 随着功能的增加（特别是音频和图像Buffer），C 语言的内存管理会变难。养成每次 `malloc` 都思考在哪里 `free` 的习惯。

祝贺你，你的项目骨架已经非常健壮，现在是填充血肉的时候了！🚀