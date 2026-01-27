# Luna 视频引擎 API 文档
本文档描述了 Luna 语言内置的视频处理核心类：`Clip`、`Timeline`、`Project` 和 `ClipInstance` [新增]。这些类由底层 C 语言实现，提供了高性能的视频加载、合成与渲染功能。关键帧系统支持冻结（hold）、线性（linear）和贝塞尔（bezier）插值，可适用于所有数值属性，并允许调节贝塞尔权重。内置预设有 "hold"、"linear"、"ease_in"、"ease_out" 和 "ease_in_out"；用户可自定义预设。该系统设计为通用，便于未来扩展到其他类型（如形状图层、文字图层、图片图层）的数值属性。

## 1. Clip 类 (素材)
`Clip` 类用于加载和操作单个视频文件。它是时间线上的基本元素。

### 构造函数
#### `Clip(path: String)`
创建一个新的素材实例并加载视频元数据。
* **参数**:
    * `path`: 视频文件的路径 (字符串)。
* **返回值**: `Clip` 实例。
* **注意**: 如果文件加载失败，会在控制台打印错误，建议检查文件路径是否正确。

### 属性 (只读/同步)
这些属性在初始化或调用特定方法后会自动更新：
* `width`: 视频原始宽度 (Number)。
* `height`: 视频原始高度 (Number)。
* `fps`: 视频帧率 (Number)。
* `duration`: 素材的持续时间（秒）(Number)。
* `has_video`: 是否包含视频流 (Number, 1 为真, 0 为假)。
* `volume`: 素材的当前音量 (Number)。
* `has_audio`: 是否包含音频流 (Number, 1 为真, 0 为假)。
* `in_point`: 素材的入点时间（从视频文件开头的偏移秒数）(Number)。
* `default_x`: X 轴坐标 (Number)。
* `default_y`: Y 轴坐标 (Number)。
* `default_scale_x`: X 轴缩放比例 (Number)。
* `default_scale_y`: Y 轴缩放比例 (Number)。
* `default_opacity`: 不透明度 (0.0 - 1.0) (Number)。

### 方法
#### `trim(start: Number, duration: Number)`
裁剪素材。
* **参数**:
    * `start`: 入点时间（秒）。表示从视频文件的第几秒开始播放。
    * `duration`: 持续时间（秒）。表示裁剪后素材在时间线上占据的时长。
* **示例**: `clip.trim(10, 5)` // 从第10秒开始，截取5秒长的片段。

#### `setScale(sx: Number, [sy: Number])`
设置素材的缩放比例。
* **参数**:
    * `sx`: X 轴缩放比例 (1.0 为原始大小)。
    * `sy`: (可选) Y 轴缩放比例。如果省略，则 `sy` 等于 `sx` (保持等比缩放)。
* **示例**:
    * `clip.setScale(0.5)` // 缩小一半
    * `clip.setScale(1.0, 0.5)` // 压扁

#### `setPos(x: Number, y: Number)`
设置素材在画布上的位置。
* **参数**:
    * `x`: X 轴坐标 (像素)。
    * `y`: Y 轴坐标 (像素)。
* **示例**: `clip.setPos(100, 200)`

#### `setOpacity(opacity: Number)`
设置素材的不透明度。
* **参数**:
    * `opacity`: 0.0 (全透明) 到 1.0 (不透明)。
* **示例**: `clip.setOpacity(0.8)`

#### `volume(level: Number)`
设置素材的音量。
* **参数**:
    * `level`: 音量大小。
        * `0.0`: 静音。
        * `1.0`: 原始音量 (100%)。
        * 大于 `1.0`: 音频增益 (例如 `2.0` 为 200% 音量)。
* **示例**: `clip.volume(0.5)` // 将音量减半

#### `export(filename: Number)`
将当前素材（包含裁剪设置）导出为新文件。
* **参数**:
    * `filename`: 导出文件的路径。

---
## 2. Text 类 (文字图层)
用于创建纯文字图层。

### 构造函数
#### `Text(content: String)`
创建一个包含指定内容的文字图层。
* **默认属性**: 字体 Arial, 字号 32px, 颜色 白色, 时长 5秒。
* **示例**: `var t = Text("Hello World")`

### 方法 (Text 特有)
#### `setFont(path: String)`
设置字体文件路径。
* **示例**: `t.setFont("fonts/Roboto-Bold.ttf")`

#### `setSize(pixels: Number)`
设置字号大小。
* **示例**: `t.setSize(64)`

#### `setColor(r: Number, g: Number, b: Number)`
设置文字颜色 (0-255)。
* **示例**: `t.setColor(255, 0, 0)`

### 方法 (通用)
* `trim(start, duration)`: 调整文字显示的入点和持续时间。
* `setPos(x, y)`: 设置文字位置。
* `setScale(scale)`: 设置文字缩放。
* `setOpacity(opacity)`: 设置透明度。

---
## 3. Timeline 类 (时间线)
`Timeline` 用于管理多个轨道和素材，负责将素材组合在一起。

### 构造函数
#### `Timeline(width: Number, height: Number, fps: Number)`
创建一个新的时间线。
* **参数**:
    * `width`: 时间线画布宽度。
    * `height`: 时间线画布高度。
    * `fps`: 时间线帧率。

### 属性
* `duration`: 当前时间线的总时长（秒），会根据添加的素材自动增长 (Number)。

### 方法
#### `add(trackId: Number, clip: Clip, startTime: Number)` [修改]
将素材添加到指定轨道，并返回一个 `ClipInstance` 实例，用于进一步配置关键帧动画。
* **参数**:
    * `trackId`: 轨道索引 (整数，从 0 开始)。如果轨道不存在会自动创建。
    * `clip`: 要添加的 `Clip` 实例。
    * `startTime`: 素材在时间线上的起始时间（秒）。
* **返回值**: `ClipInstance` 实例（允许链式调用关键帧方法）。
* **注意**: 该操作会根据素材的结束时间自动更新时间线的 `duration` 属性。

---
## 4. ClipInstance 类 (时间线素材实例) [新增]
`ClipInstance` 是 `Timeline.add` 的返回值，用于管理时间线上特定素材实例的关键帧动画。它不支持直接构造（无需 `new`），仅通过 `Timeline.add` 获取。该类支持通用的关键帧系统，适用于所有数值属性，并可轻松扩展到未来类型（如形状图层、文字图层、图片图层）。

### 方法
#### `addKeyframe(property: String, time: Number, value: Number, type: String, [weight: Number])`
为指定属性添加关键帧。
* **参数**:
    * `property`: 属性名称 (字符串)。支持："x"、"y"、"scale_x"、"scale_y"、"rotation"、"opacity"、"volume"、"font_size"（适用于文字图层）。
    * `time`: 关键帧时间（秒，从素材在时间线上的起始时间开始计算）。
    * `value`: 属性值（Number）。
    * `type`: 插值类型（字符串）。支持："hold"（冻结）、"linear"（线性）、"bezier"（贝塞尔）。
    * `weight` (可选): 贝塞尔权重（0.0 - 1.0，调节曲线强度）。仅在 `type` 为 "bezier" 时有效，默认 0.0。
* **示例**: `inst.addKeyframe("opacity", 0, 0, "linear")` // 在 0 秒时不透明度为 0，使用线性插值到下一个关键帧。

#### `addKeyframeWithPreset(property: String, time: Number, value: Number, preset: String)`
使用预设添加关键帧。
* **参数**:
    * `property`、`time`、`value`: 同上。
    * `preset`: 预设名称 (字符串)。内置预设："hold"、"linear"、"ease_in"、"ease_out"、"ease_in_out"。可使用自定义预设（通过全局 `addUserPreset` 添加）。
* **示例**: `inst.addKeyframeWithPreset("rotation", 2, 180, "ease_in_out")` // 在 2 秒时旋转到 180 度，使用 ease_in_out 预设。

---
## 5. Project 类 (项目)
`Project` 是最高层级的容器，用于配置全局参数并连接渲染引擎。

### 构造函数
#### `Project(width: Number, height: Number, fps: Number)`
创建一个新项目。
* **参数**:
    * `width`: 项目宽度。
    * `height`: 项目高度。
    * `fps`: 项目帧率。

### 属性
* `width`: 项目宽度 (Number)。
* `height`: 项目高度 (Number)。
* `fps`: 项目帧率 (Number)。

### 方法
#### `setTimeline(timeline: Timeline)`
将时间线绑定到项目。
* **参数**:
    * `timeline`: 一个 `Timeline` 实例。
* **注意**: 必须绑定时间线后，项目才能进行预览或渲染。

#### `preview(start: Number, end: Number)`
激活当前项目进行实时预览。
* **参数**:
    * `start` (可选): 预览循环的起始时间（秒）。
    * `end` (可选): 预览循环的结间（秒）。
* **描述**:
    告诉宿主程序（Luna Host）将当前项目设置为活跃项目。
    * 如果提供了 `start` 和 `end`，预览将在该时间范围内循环播放。
    * 如果不提供参数（即 `preview()`），预览将在整个时间线的长度内循环播放。

---
## 6. 全局函数 [新增]
#### `addUserPreset(name: String, type: String, weight: Number)`
添加用户自定义关键帧预设。
* **参数**:
    * `name`: 预设名称 (字符串)。
    * `type`: 插值类型 (字符串，支持 "hold"、"linear"、"bezier")。
    * `weight`: 贝塞尔权重 (0.0 - 1.0，仅 "bezier" 有效)。
* **示例**: `addUserPreset("custom_ease", "bezier", 0.3)` // 添加一个自定义贝塞尔预设。

---
## 7. 完整示例代码
```luna
# 1. 创建项目
var proj = Project(1920, 1080, 30)
# 2. 创建时间线
var tl = Timeline(1920, 1080, 30)
proj.setTimeline(tl)
# 3. 加载并处理素材
var clip = Clip("test.mp4")
if clip == nil:
    print "加载失败"
else:
    # 裁剪：从第0秒开始，取10秒
    clip.trim(0, 10)
   
    # 变换：缩小并移动
    clip.setScale(0.5)
    clip.setPos(100, 200)
    clip.setOpacity(0.8)
    # 4. 添加到时间线，并获取 ClipInstance
    var clipInst = tl.add(0, clip, 0)  # 轨道0，从时间线第0秒开始播放
    # 新增: 测试关键帧 - opacity 淡出, volume 渐弱
    clipInst.addKeyframe("opacity", 0, 1.0, "linear")  # 0秒全不透明
    clipInst.addKeyframe("opacity", 5, 0.0, "bezier", 0.5)  # 5秒淡出到透明，贝塞尔权重0.5
    clipInst.addKeyframe("volume", 0, 1.0, "hold")  # 0秒全音量，冻结到下一个
    clipInst.addKeyframeWithPreset("volume", 8, 0.0, "ease_out")  # 8秒渐弱到静音，使用预设
    # 5. 添加第二个素材
    var clip2 = Clip("test.mp4")
    clip2.trim(5, 5) # 截取5秒
    clip2.setPos(500, 500)
    # 轨道1 (上层)，从时间线第2秒开始播放
    var clip2Inst = tl.add(1, clip2, 2)
    # 新增: 测试关键帧 - rotation 旋转, scale_x 缩放
    clip2Inst.addKeyframe("rotation", 0, 0, "linear")  # 0秒0度
    clip2Inst.addKeyframe("rotation", 4, 360, "bezier", 0.7)  # 4秒旋转360度，贝塞尔权重0.7
    clip2Inst.addKeyframeWithPreset("scale_x", 0, 1.0, "ease_in")  # 0秒原始缩放，使用预设
    clip2Inst.addKeyframeWithPreset("scale_x", 3, 0.5, "ease_in")  # 3秒缩小到0.5
    print "时间线总时长: "
    print tl.duration
    # 6. 启动预览
    proj.preview()
```

