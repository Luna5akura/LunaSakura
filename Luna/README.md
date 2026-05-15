# Luna

Luna 是一个还在快速演进中的实验性项目：它把一门自定义脚本语言、一个时间线媒体模型、一个基于 SDL2 + OpenGL 的实时预览宿主，以及基于 FFmpeg 的解码/导出链路放在同一个代码库里。项目的目标不是只做“另一门脚本语言”，而是探索一条更直接的创作路径：用脚本描述视频、文字、动画和导出流程。

目前它已经具备一个可运行的核心原型：

- Luna 语言前端：扫描器、编译器、字节码、VM、GC。
- 媒体与工程模型：`Clip`、`Image`、`Text`、`Solid`、`Adjustment`、`Timeline`、`Project`、`ClipInstance`。
- 文字动画器增强阶段：`TextAnimator`、`RangeSelector`、`ExpressionSelector`、`WigglySelector`、逐字符文本动画。
- 实时预览宿主：监听脚本变更并自动热重载。
- 渲染管线：FFmpeg 解码，OpenGL 合成，文字渲染，基础变换与透明度。
- 导出能力：按时间线逐帧渲染并编码输出。
- 工具配套：仓库内包含一个基础版 VS Code 语法高亮扩展。

这意味着它已经跨过“概念验证”阶段，进入了“打磨成可持续开发平台”的阶段。

## 项目定位

Luna 适合被理解成一个“可编程视频原型引擎”，而不是传统 NLE 的替代品。它更像：

- 一门用于描述媒体时间线的 DSL。
- 一个能实时预览脚本结果的宿主程序。
- 一个可继续扩展成完整创作工具链的底层引擎。

如果后续路线走顺，Luna 可以发展成：

- 用代码生成视频和动态图文内容的工具。
- 用脚本驱动动画、特效和批处理导出的平台。
- 一套面向创意编程 / 自动化视频生产的轻量运行时。

## 当前能力

基于当前仓库代码，项目已经明确具备以下模块。

### 1. 语言运行时

- 自定义词法分析器、编译器和字节码虚拟机位于 `src/core/`。
- 语言采用缩进敏感语法，风格接近 Python。
- 已有的数据与语义能力包括：
  - `var`、`if/else`、`while`、`for`
  - 函数、闭包、`lam`
  - 类、继承、`super`
  - `try/except`
  - `List`、`Dict`
  - 基础运算符重载
  - 垃圾回收和对象系统

相关入口可见 [src/core/compiler/compiler.h](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/core/compiler/compiler.h:1) 和 [src/core/vm/vm.h](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/core/vm/vm.h:1)。

### 2. 视频工程模型

- `Project` 负责全局尺寸、帧率、时间线绑定和预览范围。
- `Timeline` 负责轨道、素材摆放和总时长维护。
- `Clip` 负责媒体素材引用、裁剪和默认变换。
- `Text` 作为文字图层存在于相同的模型中。
- `ClipInstance` 负责时间线实例上的关键帧动画。

相关结构定义位于：

- [src/engine/model/project.h](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/model/project.h:1)
- [src/engine/model/timeline.h](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/model/timeline.h:1)
- [src/engine/model/animation.h](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/model/animation.h:1)

### 3. 实时预览宿主

主程序 [main.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/main.c:1) 已经实现：

- 加载 `.luna` 脚本。
- 监控脚本文件修改时间。
- 变更后销毁旧 VM 与合成器并重新编译运行。
- 从脚本侧激活当前 `Project`。
- 用 SDL2 + OpenGL 打开预览窗口并循环播放。
- 支持简单预览控制：
  - `Space` 暂停 / 继续
  - `Left` / `Right` 前后跳 1 秒
  - `R` 重置并触发重载

### 4. 渲染与媒体管线

- 视频帧通过 FFmpeg 解码。
- 合成器负责 YUV 纹理上传、矩阵变换、混合、离屏渲染和屏幕回显。
- 文字渲染已经接入 FreeType 和字体图集。
- 动画系统支持关键帧插值，包含 `hold`、`linear`、`bezier` 和预设。
- 音频混音基础设施已经存在，但整体还处在“可继续打磨”的阶段。
- 导出服务支持按时间线逐帧渲染并交给编码器输出。

相关实现可从这些文件切入：

- [src/engine/render/compositor.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/render/compositor.c:1)
- [src/engine/media/codec/decoder.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/media/codec/decoder.c:1)
- [src/engine/media/audio/mixer.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/media/audio/mixer.c:1)
- [src/engine/service/exporter.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/service/exporter.c:1)

### 5. 脚本绑定层

`src/binding/` 把语言对象和引擎对象接在一起，已经暴露出一批核心 API，例如：

- `Project(width, height, fps)`
- `Timeline(width, height, fps)`
- `Clip(path)`
- `Image(path)`
- `Text(content)`
- `text.addAnimator()`
- `text.getAnimatorCount()`
- `text.getAnimator(index)`
- `Solid(width, height, r, g, b[, a])`
- `Adjustment(width, height)`
  `adjustment.setAffectsWholeFrame(true|false)` 可在“整帧”与“局部矩形区域”之间切换。
  现在还支持 `setFeather(...)`、`setBlendMode(...)`、`setMaskSource(...)`、`setMaskMode(...)` 和 `setMaskInvert(...)`。
- `Group` / `Precomp`
  第一版已经能把一个内部 `Timeline` 当作单个图层放进外层时间线，用来做分组和预合成。
- `Group(width, height, fps)`
- `Precomp(width, height, fps)`
  两者都是第一版“嵌套时间线图层”，支持 `getTimeline()` / `setTimeline(...)`。
- `proj.setTimeline(tl)`
- `proj.preview([start, end])`
- `tl.add(track, clip, startTime)`
- `tl.addTrack()`
- `tl.removeTrack(index)`
- `tl.getClipCount(track)`
- `tl.getClip(track, index)`
- `tl.removeClip(track, index)`
- `tl.setTrackName(index, name)`
- `tl.setTrackVisible(index, visible)`
- `tl.setTrackLocked(index, locked)`
- `tl.setBackgroundColor(r, g, b[, a])`
- `tl.getDuration()`
- `clip.trim(start, duration)`
- `clip.setScale(...)`
- `clip.setPos(...)`
- `clip.setOpacity(...)`
- `clip.volume(...)`
- `solid.setColor(...)`
- `clipInst.x.add(...)`, `clipInst.opacity.keyframes([...])`
- `clipInst.rotation.set(...)`, `clipInst.scale_x.withPreset(...)`
- `clipInst.x.remove(...)`, `clipInst.opacity.clear()`
- `clipInst.x.count()`, `clipInst.x.time(index)`, `clipInst.x.value(index)`
- `clipInst.x.type(index)`, `clipInst.x.weight(index)`
- `clipInst.setStart(...)`
- `clipInst.setDuration(...)`
- `clipInst.setInPoint(...)`
- `clipInst.setZIndex(...)`
- `clipInst.setVisible(...)`
- `clipInst.setPositionMode("position" | "anchor")`
- `clipInst.alignTo(otherClipInst, selfAnchor, targetAnchor)`
- `clipInst.alignToComposition(selfAnchor, targetAnchor)`
- `clipInst.getAlignmentTargetClipId()`
- `clipInst.remove()`
- `clipInst.moveToTrack(track[, start])`
- `clipInst.duplicate(track[, start])`
- `clipInst.x.shift(delta)`
- `clipInst.x.scaleTimes(factor)`
- `clipInst.opacity.copyFrom(otherClipInst.x)`
- `clipInst.addEffect(effect)`
  `Adjustment` 图层会把这些效果真正应用到它下方已经合成好的画面上，适合作为全局调色、模糊、海报化这类 adjustment layer；也可以切到矩形模式只影响一个局部区域，并叠加羽化、遮罩和混合模式。
  效果现在是直接可构造的类实例，例如 `FractalNoise(scale=72)`、`Mosaic(blockSize=12, sharpColors=true)`；同一个效果对象只能挂到一个 `ClipInstance`。
- `clipInst.getEffectCount()`
- `clipInst.getEffect(index)`
- `clipInst.removeEffect(index)`
- `clipInst.clearEffects()`
- `clipInst.opacity.withPreset(...)`
- `anim = text.addAnimator()`
- `anim.y = 40`, `anim.tracking = 12`, `anim.skew = 16`, `anim.fillHue = 20`, `anim.characterOffset = 1`, `anim.fillColor = [255, 120, 80, 255]`
- `anim.opacity.keyframes([...])`, `anim.strokeColor.keyframes([...])`
- `selector = anim.addRangeSelector()`
- `selector.start.keyframes([...])`, `selector.offset.keyframes([...])`
- `selector.setShape("smooth")`
- `selector.setBasedOn("words")`
- `expr = anim.addExpressionSelector()`
- `expr.setExpression("smoothstep(0, 100, position) * 100")`
- `expr.setCallback(selectorFn)`
- `expr.setMode("max")`
- `expr.amount.keyframes([...])`
- `wiggly = anim.addWigglySelector()`
- `wiggly.wigglesPerSecond = 2.0`, `wiggly.correlation = 20`
- `wiggly.setMode("subtract")`
- `effect.getName()`
- `effect.someNumber = value`
- `effect.someBool = value`
- `effect.someColor = [r, g, b, a]`
- `effect.setSource(clipInstance | nil)`
- `effect.getSourceClipId()`
- `effect.linkNumber(key, otherEffect, otherKey[, scale, offset])`
- `effect.linkColor(key, otherEffect, otherKey)`
- `effect.unlinkNumber(key)`
- `effect.unlinkColor(key)`
- `effect.amount.add(...)`, `effect.evolution.keyframes([...])`
- `effect.amount.set(...)`, `effect.startColor.keyframes([...])`
- `effect.amount.remove(...)`, `effect.startColor.clear()`
- `effect.amount.count()`, `effect.amount.time(index)`, `effect.amount.value(index)`
- `effect.amount.type(index)`, `effect.amount.weight(index)`
- `effect.amount.shift(delta)`, `effect.amount.scaleTimes(factor)`
- `effect.color.copyFrom(otherEffect.startColor)`
  Built-in effects: `Tint`, `Fill`, `BrightnessContrast`, `Blur`, `Glow`, `Mosaic`, `Grid`, `GradientRamp`, `FractalNoise`, `DisplacementMap`, `Posterize`, `SliderControl`, `AngleControl`, `CheckboxControl`, `PointControl`, `ColorControl`
  `DisplacementMap` 现在支持外部 `ClipInstance` 作为位移源，并会优先采样该图层应用效果后的结果；同时支持更接近 AE 的逐像素置换语义，包括 50% 灰中性点和横向/纵向独立通道选择。控制类效果也可以通过 `linkNumber` / `linkColor` 驱动其他效果参数。
  `Glow` 则提供了一版偏 AE / Deep Glow 使用习惯的高光扩散效果，支持 `radius`、`intensity`、`threshold`、`softness` 和 `color`。
- 当前文本动画器已经支持 `RangeSelector`、`ExpressionSelector` 和 `WigglySelector` 三类选择器，并支持 `characters / words / lines` 三种选择基础；更完整的 AE 高级 selector 行为和真正脚本级表达式系统仍在后续路线里。
- `proj.setPreviewRange(...)`
- `proj.clearPreviewRange()`
- `proj.setBackgroundColor(...)`
- `proj.getDuration()`

文档草稿见：

- [src/binding/Video.md](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/binding/Video.md:1)
- [src/binding/Standard.md](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/binding/Standard.md:1)

## 当前阶段判断

从工程状态看，Luna 现在最准确的阶段是：

**“可运行的纵向原型已经打通，但离稳定可持续迭代的平台还有一段距离。”**

优势很明显：

- 架构分层已经形成。
- 从语言到预览再到导出的主链路基本具备。
- 项目方向有辨识度，不是普通教程级玩具。

短板也同样明确：

- 文档、样例、测试和构建说明还不成体系。
- API 语义还没有完全稳定。
- 音频、效果、导入系统、错误恢复和性能验证还不够扎实。
- 仓库里还混有实验痕迹和阶段性文稿，信息密度高但对新读者不友好。

## 构建与运行

当前仓库使用 `makefile` 构建，依赖：

- `gcc`
- `SDL2`
- `OpenGL`
- `EGL`
- `FFmpeg` 相关库：`libavcodec`、`libavformat`、`libavutil`、`libswscale`、`libswresample`
- `freetype2`
- 若干 Linux 图形依赖：`x11`、`libva`、`libdrm` 等

构建：

```bash
make
```

运行功能回归测试：

```bash
make test
```

运行性能测试：

```bash
make perf
```

`make perf` 会生成并运行独立的 `luna_perf`，当前覆盖：

- 核心对象构造与释放
- 图片加载
- 动画求值
- 时间线增删查改
- 语言编译与端到端执行
- 视频绑定脚本构建
- 效果链链接更新
- 合成器基础渲染与嵌套渲染
- 真实视频夹具驱动的解码性能
- 真实视频夹具驱动的纯转码性能
- 真实视频时间线导出性能

性能测试默认输出吞吐与单次耗时，不做固定机器相关阈值断言，更适合作为长期趋势回归。
默认的真实视频性能回归会在 `/tmp` 自动生成一个稳定的小型 MP4 基准夹具，再用它驱动解码器、转码器和导出器。

运行一个脚本：

```bash
./luna examples/engine.luna
```

如果你使用 Nix，仓库里也提供了 [shell.nix](/home/luna/Projects/VSCode/Clion/Luna/Luna/shell.nix:1)。

## 一个最小示例

仓库中的 [engine.luna](/home/luna/Projects/VSCode/Clion/Luna/Luna/examples/engine.luna:1) 展示了典型工作流。简化版示例如下：

```luna
proj = Project(960, 540, 30)
tl = Timeline(960, 540, 30)
proj.setTimeline(tl)

clip = Clip("assets/media/test.mp4")
clip.trim(0, 10)
clip.setScale(0.5)
clip.setPos(100, 200)
clip.setOpacity(0.8)

inst = tl.add(0, clip, 0)
inst.opacity.keyframes([
  [0, 1.0],
  [5, 0.0, "bezier", 0.5]
])

proj.preview()
```

这个例子已经体现了 Luna 的核心思路：先声明项目与时间线，再通过脚本操控素材、位置、透明度和关键帧，最后交给宿主实时预览。

## 仓库结构

```text
.
├── main.c                         # 热重载宿主入口
├── src/core/                      # 语言前端、字节码、VM、GC
├── src/binding/                   # Luna <-> 引擎 绑定层
├── src/engine/model/              # Project / Timeline / Clip / Animation
├── src/engine/render/             # 合成与文字渲染
├── src/engine/media/              # 解码、编码、音频、探测
├── src/engine/service/            # 预览、导出、转码服务
├── luna-vscode-extension/luna/    # VS Code 语法扩展
└── *.luna                         # 样例和测试脚本
```

## 长期路线图

下面这份路线图不是“功能愿望清单”，而是结合当前架构后的长期推进顺序。核心原则是先把主链路打稳，再扩张能力面。

### 阶段 1：把原型打磨成稳定内核

目标：让项目从“能跑”进入“能持续开发”。

- 整理构建系统，保证新环境能稳定编译。
- 清理 README、API 文档和样例，统一脚本风格。
- 建立最基本的回归测试：
  - 语言单元测试
  - 脚本集成测试
  - 简单导出 smoke test
- 收敛核心 API 命名和行为，减少“文档写了但实现漂移”的情况。
- 把错误信息做得更友好，特别是编译报错、运行时报错、媒体加载报错。
- 梳理内存所有权和 GC 根集，优先排查热重载时的资源生命周期问题。

建议里程碑：`v0.1`。

### 阶段 2：补强脚本语言与标准库

目标：让 Luna 先成为“足够顺手的描述语言”。

- 稳定并补齐当前已经部分存在的语言特性。
- 统一 `List` / `Dict` / `String` / `range` 等标准库体验。
- 完善模块化能力：
  - `import`
  - 多文件脚本组织
  - 搜索路径约定
- 增强调试能力：
  - 更好的栈追踪
  - 更清晰的源码定位
  - 可选字节码 / 执行跟踪输出
- 建立脚本层最佳实践，避免用户直接依赖过多底层细节。

建议里程碑：`v0.2`。

### 阶段 3：把时间线和动画系统做成真正可用

目标：让脚本驱动媒体编辑这件事开始变得“好用”。

- 继续扩展 `ClipInstance` 上的属性动画。
- 增加更完整的关键帧编辑语义：
  - 删除关键帧
  - 查询关键帧
  - 批量预设
  - 更稳定的贝塞尔控制
- 增加更多图层类型：
  - 图片
  - 纯色块 / shape
  - 更完整的文字图层
- 补充项目级参数：
  - 背景色
  - 预览窗口策略
  - 安全区域 / 对齐辅助
- 让时间线查询、轨道管理和时长维护更健壮。

建议里程碑：`v0.3`。

### 阶段 4：把渲染、音频和导出链路做扎实

目标：让输出质量和稳定性追上功能野心。

- 强化音频系统：
  - 更可靠的同步
  - 多轨混音
  - 音量包络
  - 静音 / 淡入淡出
- 提升导出能力：
  - 编码参数可配置
  - 导出进度 / 取消
  - 更稳定的长时间渲染
- 优化解码与合成性能：
  - 复用解码器
  - 减少 CPU/GPU 间拷贝
  - 针对热路径做 profiling
- 明确实时预览和离线导出的差异策略，避免“预览看起来对，导出不一致”。

建议里程碑：`v0.5`。

### 阶段 5：效果系统与创作工作流

目标：让 Luna 不只是能摆素材，而是真正能“创作”。

- 正式化效果链 API。
- 增加内置效果：
  - 颜色校正
  - 模糊
  - 透明度 / 混合模式
  - 简单转场
- 定义脚本层 effect registry 的扩展方式。
- 支持更完整的模板化工作流：
  - 批量替换文本
  - 批量替换素材
  - 参数化渲染
- 为自动化内容生产场景设计项目模板与 CLI 用法。

建议里程碑：`v0.7`。

### 阶段 6：编辑器、工具链和生态

目标：降低新用户进入成本，让项目有协作基础。

- 提升 VS Code 扩展：
  - 更完整的语法高亮
  - snippets
  - 诊断提示
  - 简单跳转或 hover 文档
- 增加命令行工具：
  - `preview`
  - `render`
  - `lint`
  - `fmt`
- 建立示例项目库和 cookbook。
- 整理插件或宿主扩展边界，为未来 GUI 编辑器或节点系统留接口。

建议里程碑：`v1.0` 前准备。

## 下一步的几个可能发展方向

如果只看“下一步最值得投资源的方向”，我会优先考虑下面几个分支。它们都合理，但重点不同。

### 方向 A：先做“稳定工程内核”

适合目标：把项目从个人实验推进到长期维护。

优先事项：

- 测试体系
- API 收敛
- 文档与样例
- 热重载和资源生命周期稳定性
- 编译 / 运行错误体验

这是风险最低、复利最高的方向。它不会立刻让功能列表暴增，但会显著提高后续每一步开发效率。

### 方向 B：先做“可编程动画与时间线”

适合目标：尽快形成 Luna 最独特的卖点。

优先事项：

- 完善 `ClipInstance` 动画能力
- 丰富关键帧预设和查询编辑 API
- 增加图片 / shape / 更强文字图层
- 做出几个有表现力的脚本示例

这个方向最容易让项目展示出差异化，也最适合做 demo、作品和对外展示。

### 方向 C：先做“导出与批处理生产力”

适合目标：尽快把 Luna 变成实用工具。

优先事项：

- 稳定离线渲染
- 编码参数控制
- CLI 化
- 模板化输入
- 批量导出工作流

如果你更想让它服务自动化视频生成、字幕卡片、批量内容生产，这会是很有价值的路线。

### 方向 D：先做“语言平台”

适合目标：把 Luna 打造成更完整的 DSL / 嵌入式语言。

- `import` 与模块系统
- 更好的标准库
- 调试 / 诊断 / 格式化
- 更清晰的语义边界

这条路会让语言本身更扎实，但短期内对视频能力的可见提升没那么直接。

## 我对下一步的建议

如果要兼顾长期收益和近期成果，最合适的顺序通常是：

1. 先补工程稳定性和文档。
2. 然后强化时间线动画系统，做出 2 到 3 个高质量脚本示例。
3. 再回头把导出、音频和 CLI 打磨成真正可用的工具链。

换句话说，**最值得先做的不是继续横向加功能，而是把现有纵向主链路做稳、做清楚、做可演示**。Luna 现在最稀缺的不是想法，而是把这些好想法收束成一个可靠产品雏形。

## 已知现实与限制

当前 README 基于仓库现状整理，但项目仍处于高迭代阶段，因此有几件事需要提前说明：

- API 和脚本示例可能继续变化。
- 构建依赖较重，目前更偏向 Linux 开发环境。
- 音频、效果链和部分服务模块仍有明显继续打磨空间。
- 仓库中的部分样例、文档和实现之间仍可能存在轻微漂移，建议把 `src/binding/*.md` 与实际代码一起阅读。

## 适合从哪里开始读代码

如果你准备继续开发，建议按这个顺序进入：

1. [main.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/main.c:1)
2. [src/binding/bind_video.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/binding/bind_video.c:1)
3. [src/engine/model/timeline.h](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/model/timeline.h:1)
4. [src/engine/render/compositor.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/engine/render/compositor.c:1)
5. [src/core/compiler/compiler.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/core/compiler/compiler.c:1) 与 [src/core/vm/vm.c](/home/luna/Projects/VSCode/Clion/Luna/Luna/src/core/vm/vm.c:1)

这样最快能看清“脚本是怎么变成预览画面的”。
