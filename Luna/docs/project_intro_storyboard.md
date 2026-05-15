# Luna 项目介绍视频分镜脚本

## 定位

- 目标时长：70 到 95 秒
- 输出方向：一支通过 `.luna` 脚本直接生成的项目介绍短片
- 目标观众：对创作工具、编程语言、图形系统、动效设计感兴趣的人
- 核心信息：`Luna` 不是播放器脚本，而是一门可以直接描述视频、图层、效果、动画与合成关系的语言

## 整体视觉方向

- 基础气质：克制、现代、偏设计工具感
- 主视觉母题：渐变、分形杂色、置换、网格、海报化、局部高亮文字
- 背景原则：以深色或低饱和底为主，让颜色变化来自效果链本身
- 文字原则：大字号、强留白、弱装饰、少量描边
- 节奏原则：用平滑位移、淡入、局部扭曲和层间递进代替花哨转场

## 分镜结构

### 镜头 1：开场定性

- 时长：0:00 - 0:06
- 目标：快速建立项目气质
- 画面：
  - 全屏渐变底
  - 隐藏分形噪声作为置换源，让背景轻微流动
  - 叠一层细网格，营造“设计系统 / 合成空间”感觉
  - 中央出现主标题 `Luna`
  - 下方出现副标题 `A programmable motion graphics language`
- 屏幕文案：
  - `Luna`
  - `A programmable motion graphics language`
- 技术点：
  - `Solid`
  - `GradientRamp`
  - `FractalNoise`
  - `DisplacementMap`
  - `Grid`
  - `Text`

### 镜头 2：代码驱动画面生成

- 时长：0:06 - 0:18
- 目标：让观众理解“代码直接构成画面”
- 画面：
  - 左侧是精简过的 `.luna` 代码片段
  - 右侧是与代码同步搭建出来的图层和效果
  - 每增加一段代码，画面就增加一个对应元素
- 屏幕文案：
  - `Project`
  - `Timeline`
  - `Solid`
  - `Text`
  - `Image`
- 技术点：
  - 项目 / 时间线结构
  - 图层实例
  - 链式写法

### 镜头 3：图层与时间线

- 时长：0:18 - 0:30
- 目标：展示它不是单一 shader 玩具，而是有时间线与层级结构的系统
- 画面：
  - 多轨图层依次进入
  - 背景层、图片层、文字层、调整层分开出现
  - 主画面同步叠加
- 屏幕文案：
  - `Layers`
  - `Timeline`
  - `Instances`
  - `Keyframes`
- 技术点：
  - `tl.add(...)`
  - `ClipInstance`
  - 透明度 / 位移 / 旋转关键帧

### 镜头 4：效果链

- 时长：0:30 - 0:48
- 目标：展示 Luna 当前最有辨识度的视觉能力
- 画面：
  - 同一底图逐步增加效果
  - `GradientRamp`
  - `FractalNoise`
  - `DisplacementMap`
  - `Posterize`
  - `Mosaic`
  - `Grid`
- 屏幕文案：
  - `Effects`
  - `Displacement`
  - `Posterize`
  - `Mosaic`
- 技术点：
  - 效果对象
  - `addEffect(...)`
  - 属性式参数与关键帧

### 镜头 5：可编程动画

- 时长：0:48 - 1:00
- 目标：强调这是一门语言，不只是参数面板
- 画面：
  - 同一组元素按规则批量出现
  - 动画参数呈有规律变化
  - 代码中出现循环、函数、批量关键帧片段
- 屏幕文案：
  - `Animation as code`
  - `Reusable`
  - `Composable`
- 技术点：
  - 变量
  - 函数
  - 批量时间控制

### 镜头 6：系统组成

- 时长：1:00 - 1:10
- 目标：说明这不是单点功能，而是一整条链路
- 画面：
  - 关键模块词组依次出现并被连线
  - `Language`
  - `VM`
  - `Timeline`
  - `Renderer`
  - `Effects`
  - `Export`
- 屏幕文案：
  - `Language`
  - `VM`
  - `Timeline`
  - `Renderer`
  - `Effects`
  - `Export`

### 镜头 7：收尾

- 时长：1:10 - 1:18
- 目标：给出明确记忆点
- 画面：
  - 背景回到更简洁的渐变与轻微杂色
  - 标题再次居中出现
  - 小字给出一句总结
- 屏幕文案：
  - `Luna`
  - `Script motion. Render ideas.`

## 制作建议

- 第一版先只做镜头 1 到镜头 4，控制在 35 到 45 秒
- 等导出和视觉节奏稳定后，再补镜头 5 到镜头 7
- 尽量优先使用当前已经稳定的能力：
  - `Solid`
  - `Image`
  - `Text`
  - `Adjustment`
  - `GradientRamp`
  - `FractalNoise`
  - `DisplacementMap`
  - `Posterize`
  - `Mosaic`
  - `Grid`

## 当前建议避免过度依赖的点

- 复杂文本排版
- 需要高级混合模式的镜头
- 依赖精细遮罩的长段过渡
- 过于复杂的递归 source effect graph
- 高度依赖音频驱动的节拍动画
