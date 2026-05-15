# Luna 视频引擎 API 文档
本文档描述了 Luna 语言内置的视频处理核心类：`Clip`、`Image`、`Text`、`Solid`、`Adjustment`、`Group`、`Precomp`、`Timeline`、`Project` 和 `ClipInstance`。这些类由底层 C 语言实现，提供了视频、图片、纯色图层、调整图层、嵌套时间线图层的加载、合成与渲染功能。关键帧系统支持冻结（hold）、线性（linear）和贝塞尔（bezier）插值，可适用于所有数值属性，并允许调节贝塞尔权重。内置预设有 "hold"、"linear"、"ease_in"、"ease_out" 和 "ease_in_out"；用户可自定义预设。

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
* **示例**: `t = Text("Hello World")`

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

#### `addAnimator()`
向 `Text` 图层添加一个 `TextAnimator`，用于逐字符文本动画。
* **返回值**: `TextAnimator`

#### `getAnimatorCount()`
返回当前文字图层上的文本动画器数量。

#### `getAnimator(index: Number)`
返回指定索引处的 `TextAnimator`。

### 方法 (通用)
* `trim(start, duration)`: 调整文字显示的入点和持续时间。
* `setPos(x, y)`: 设置文字位置。
* `setScale(scale)`: 设置文字缩放。
* `setOpacity(opacity)`: 设置透明度。

### 第一阶段限制
当前第一阶段文本动画器主要对齐 AE 的基础工作流：
* 支持 `RangeSelector`
* 支持逐字符 (`characters`) 选择
* 支持位置、缩放、旋转、透明度、tracking、描边宽度、填充色、描边色
* 所有这些属性都支持关键帧

暂未支持：
* `words` / `lines`
* `Wiggly Selector`
* `Expression Selector`
* 真正的表达式求值系统

---
## 3. Image 类 (图片图层)
用于加载单张静态图片，并把它当作可进入时间线的图层素材。

### 构造函数
#### `Image(path: String)`
创建一个图片图层。
* **参数**:
    * `path`: 图片文件路径。
* **默认属性**: 时长 5 秒，可参与位置、缩放、旋转和透明度动画。

### 属性
* `width`: 图片原始宽度。
* `height`: 图片原始高度。
* `has_video`: 固定为 `1`。
* `has_audio`: 固定为 `0`。

### 方法
`Image` 复用 `Clip` 的通用变换方法：
* `setScale(...)`
* `setPos(...)`
* `setRotation(...)`
* `setOpacity(...)`
* `trim(...)`

---
## 4. Solid 类 (纯色图层)
用于创建固定尺寸的纯色矩形图层。

### 构造函数
#### `Solid(width: Number, height: Number, r: Number, g: Number, b: Number, [a: Number])`
创建一个纯色图层。
* **参数**:
    * `width`、`height`: 图层尺寸。
    * `r`、`g`、`b`、`a`: 颜色分量，范围 `0-255`。
* **默认属性**: 时长 5 秒。

### 方法
#### `setColor(r: Number, g: Number, b: Number, [a: Number])`
更新纯色图层颜色。

#### 通用方法
`Solid` 同样支持：
* `setScale(...)`
* `setPos(...)`
* `setRotation(...)`
* `setOpacity(...)`
* `trim(...)`

---
## 5. Adjustment 类 (调整图层)
用于创建调整图层。它本身不绘制独立素材，而是把挂载在实例上的效果链应用到它下方已经合成好的画面上。

### 构造函数
#### `Adjustment(width: Number, height: Number)`
创建一个调整图层。
* **参数**:
    * `width`、`height`: 调整层画幅尺寸。第一版建议直接传入项目 / 时间线尺寸。
* **默认属性**: 时长 5 秒。

### 方法
`Adjustment` 支持：
* `setScale(...)`
* `setPos(...)`
* `setRotation(...)`
* `setOpacity(...)`
* `trim(...)`
* `setAffectsWholeFrame(flag: Bool)`
* `affectsWholeFrame()`
* `setFeather(pixels: Number)`
* `getFeather()`
* `setBlendMode(mode: String)`
* `getBlendMode()`
* `setMaskSource(clipInst: ClipInstance | nil)`
* `getMaskSourceClipId()`
* `setMaskMode(mode: String)`
* `getMaskMode()`
* `setMaskInvert(flag: Bool)`
* `maskInverted()`

### 当前限制
调整层现在支持两种模式：
* 整帧模式：`setAffectsWholeFrame(true)`，效果作用到整张已合成画面。
* 矩形模式：`setAffectsWholeFrame(false)`，效果只作用到当前图层的矩形区域。

当前矩形模式会跟随 `setPos(...)`、`setScale(...)` 和 `setRotation(...)` 形成旋转区域，羽化也会沿这个旋转后的矩形边界生效。
支持的混合模式：
* `normal`
* `add`
* `multiply`
* `screen`
* `overlay`

支持的遮罩模式：
* `alpha`
* `luma`

---
## 6. Group / Precomp 类
第一版 `Group` 和 `Precomp` 都是“嵌套时间线图层”。

### 构造函数
#### `Group(width: Number, height: Number, fps: Number)`
创建一个内部自带 `Timeline` 的分组图层。

#### `Precomp(width: Number, height: Number, fps: Number)`
创建一个内部自带 `Timeline` 的预合成图层。

### 方法
`Group` / `Precomp` 支持：
* `setScale(...)`
* `setPos(...)`
* `setRotation(...)`
* `setOpacity(...)`
* `trim(...)`
* `getTimeline()`
* `setTimeline(timeline: Timeline)`

### 第一版语义
* `getTimeline()` 会返回这个图层内部使用的时间线，你可以继续往里面 `add(...)` 其他图层。
* `setTimeline(...)` 可以把一个已经构建好的 `Timeline` 挂到 `Group` / `Precomp` 上。
* 当前第一版里，`Group` 和 `Precomp` 的底层能力相同，差别主要体现在语义和后续工作流上。

### 当前限制
* 第一版优先保证视觉合成链路打通，嵌套时间线中的音频不会自动混入外层时间线。
* 渲染目前偏功能正确性，未做缓存优化；复杂嵌套场景会比较重。

---
## 7. Timeline 类 (时间线)
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

#### `addTrack()`
手动添加一个空轨道。
* **返回值**: 新轨道的索引（Number）。

#### `removeTrack(index: Number)`
删除指定轨道。

#### `getTrackCount()`
返回当前轨道数量。

#### `getClipCount(track: Number)`
返回指定轨道上的素材实例数量。

#### `getClip(track: Number, index: Number)`
返回指定轨道、指定索引处的 `ClipInstance`。索引按时间排序。

#### `removeClip(track: Number, index: Number)`
删除指定轨道上的一个素材实例。
* **返回值**:
    * `true`: 删除成功。
    * `false`: 索引无效或该位置不存在实例。

#### `setTrackName(index: Number, name: String)`
设置轨道名称。

#### `getTrackName(index: Number)`
获取轨道名称。

#### `setTrackVisible(index: Number, visible: Bool)`
设置轨道是否可见。

#### `isTrackVisible(index: Number)`
返回轨道是否可见。

#### `setTrackLocked(index: Number, locked: Bool)`
设置轨道是否锁定。

#### `isTrackLocked(index: Number)`
返回轨道是否锁定。

#### `setBackgroundColor(r: Number, g: Number, b: Number, [a: Number])`
设置时间线背景色。

#### `getDuration()`
返回当前时间线总时长。

---
## 8. ClipInstance 类 (时间线素材实例) [新增]
`ClipInstance` 是 `Timeline.add` 的返回值，用于管理时间线上特定素材实例的关键帧动画。它不支持直接构造（无需 `new`），仅通过 `Timeline.add` 获取。该类支持通用的关键帧系统，适用于所有数值属性，并可轻松扩展到未来类型（如形状图层、文字图层、图片图层）。

### 定位模式
`ClipInstance` 现在支持两种定位方式：
* `position`：传统绝对位置模式，`x/y` 直接表示左上角位置。
* `anchor`：锚点对齐模式，`x/y` 会作为对齐完成后的额外偏移量。

#### `setPositionMode(mode: String)`
切换定位模式。支持：
* `position`
* `anchor`

#### `getPositionMode()`
返回当前定位模式名称。

#### `alignTo(target: ClipInstance, selfAnchor: String, targetAnchor: String)`
切换到锚点模式，并把当前实例的某个锚点对齐到目标实例的某个锚点。

#### `alignToComposition(selfAnchor: String, targetAnchor: String)`
切换到锚点模式，并把当前实例的某个锚点对齐到整个合成画幅的某个锚点。

#### `clearAlignmentTarget()`
清除当前对齐目标，回到“对齐合成画幅”的目标类型，但不会强制切回 `position` 模式。

#### `getAlignmentTargetClipId()`
如果当前正在对齐某个图层实例，则返回目标 `ClipInstance` 的内部 `clip_id`；否则返回 `nil`。

支持的锚点名称：
* `top_left`
* `top_center`
* `top_right`
* `center_left`
* `center`
* `center_right`
* `bottom_left`
* `bottom_center`
* `bottom_right`

示例：

```luna
badgeInst = tl.add(1, badge, 0)
    .setDuration(2)
    .alignTo(titleInst, "top_left", "bottom_left")

badgeInst.alignToComposition("center", "center")
```

当前第一版的对齐边界基于图层的未旋转矩形尺寸计算；也就是说，缩放会参与对齐，旋转后的外接轮廓暂时不会参与锚点求解。这是为了给后续更完整的约束布局留出扩展空间。

## 8A. TextAnimator 类
`TextAnimator` 挂在 `Text` 图层本体上，用于给文字的每个字符施加动画影响。

### 方法
#### `addRangeSelector()`
添加一个 `RangeSelector`。
* **返回值**: `RangeSelector`

#### `addExpressionSelector()`
添加一个 `ExpressionSelector`。
* **返回值**: `ExpressionSelector`

#### `addWigglySelector()`
添加一个 `WigglySelector`。
* **返回值**: `WigglySelector`

#### `getRangeSelectorCount()`
返回当前动画器上的选择器数量。

#### `getRangeSelector(index: Number)`
返回指定索引处的 `RangeSelector`。

#### `getExpressionSelectorCount()`
返回当前动画器上的表达式选择器数量。

#### `getExpressionSelector(index: Number)`
返回指定索引处的 `ExpressionSelector`。

#### `getWigglySelectorCount()`
返回当前动画器上的 wiggly 选择器数量。

#### `getWigglySelector(index: Number)`
返回指定索引处的 `WigglySelector`。

### 可动画属性
以下属性会直接返回 `AnimatedProperty`：
* `x`
* `y`
* `scaleX`
* `scaleY`
* `rotation`
* `opacity`
* `tracking`
* `strokeWidth`
* `anchorX`
* `anchorY`
* `skew`
* `skewAxis`
* `fillOpacity`
* `strokeOpacity`
* `fillHue`
* `fillSaturation`
* `fillBrightness`
* `strokeHue`
* `strokeSaturation`
* `strokeBrightness`
* `characterOffset`
* `characterValue`
* `fillColor`
* `strokeColor`

示例：

```luna
anim = text.addAnimator()
anim.y = 40
anim.opacity.keyframes([
    [0.0, -100.0],
    [1.0, 0.0]
])
anim.fillColor = [255, 120, 80, 255]
```

说明：
* `scaleX` / `scaleY` 以百分比增量方式工作，`100` 表示额外放大 100%。
* `opacity` 以百分比增量方式工作，`-100` 表示完全隐藏，`0` 表示不改动原透明度。
* `fillOpacity` / `strokeOpacity` 也是百分比增量，`-100` 表示完全压到 0，`0` 表示不改动。
* `anchorX` / `anchorY` 会改变单字符的局部变换支点。
* `skew` / `skewAxis` 提供更接近 AE 的逐字符斜切控制。
* `fillHue` / `fillSaturation` / `fillBrightness` 和 `strokeHue` / `strokeSaturation` / `strokeBrightness`
  会按 HSV 方式对每个字符的填充/描边颜色做增量调节。
* `characterOffset` 会按字符码偏移当前字符。
* `characterValue` 会直接把字符替换成指定字符码对应的字符。

## 8B. RangeSelector 类
`RangeSelector` 决定一个 `TextAnimator` 对哪些字符、以多大权重生效。

### 可动画属性
这些属性返回 `AnimatedProperty`：
* `start`
* `end`
* `offset`
* `amount`
* `easeHigh`
* `easeLow`

### 方法
#### `setShape(name: String)`
设置选择器形状。当前支持：
* `square`
* `ramp_up`
* `ramp_down`
* `triangle`
* `smooth`

#### `getShape()`
返回当前选择器形状名称。

#### `setBasedOn(name: String)`
设置选择基础。当前支持：
* `characters`
* `words`
* `lines`

#### `getBasedOn()`
返回当前 `basedOn` 名称。

#### `setMode(name: String)`
设置选择器组合模式。支持：
* `add`
* `subtract`
* `intersect`
* `min`
* `max`

#### `getMode()`
返回当前选择器组合模式名称。

示例：

```luna
selector = anim.addRangeSelector()
selector.setShape("smooth")
selector.setBasedOn("characters")
selector.offset.keyframes([
    [0.0, -100.0],
    [2.0, 100.0]
])
```

## 8C. ExpressionSelector 类
`ExpressionSelector` 会对每个字符、单词或行计算一条数值表达式，并把结果当成动画器影响权重。

### 可动画属性
这些属性返回 `AnimatedProperty`：
* `amount`

### 方法
#### `setExpression(source: String)`
设置表达式字符串。表达式会按字符逐个求值。

当前内置变量：
* `index` / `i`
* `count` / `n`
* `time` / `t`
* `position` / `p`
* `charIndex`, `charCount`
* `wordIndex`, `wordCount`
* `lineIndex`, `lineCount`
* `PI`, `TAU`

当前内置函数：
* `sin`, `cos`, `tan`
* `abs`, `floor`, `ceil`, `round`
* `sqrt`, `exp`, `log`
* `min`, `max`, `clamp`
* `mix`, `smoothstep`
* `frac`, `sign`, `pow`, `mod`
* `step`, `ifelse`

#### `getExpression()`
返回当前表达式字符串。

#### `setBasedOn(name: String)`
设置表达式选择基础，支持：
* `characters`
* `words`
* `lines`

#### `getBasedOn()`
返回当前 `basedOn` 名称。

#### `setMode(name: String)`
设置组合模式，支持：
* `add`
* `subtract`
* `intersect`
* `min`
* `max`

#### `getMode()`
返回当前组合模式。

#### `setCallback(fn)`
设置一个 Luna 可调用对象。回调会收到一个上下文字典，返回数值后直接作为该 selector 的逐字符权重来源。

上下文字典会包含：
* `index`
* `count`
* `time`
* `position`
* `charIndex`
* `charCount`
* `wordIndex`
* `wordCount`
* `lineIndex`
* `lineCount`

#### `getCallback()`
返回当前 callback；如果没有设置则返回 `nil`。

示例：

```luna
expr = anim.addExpressionSelector()
expr.setExpression("smoothstep(0, 100, position) * 100")
expr.setBasedOn("characters")
expr.setMode("add")
expr.amount.keyframes([
    [0.0, 0.0],
    [0.4, 100.0]
])

fun selectorFn(ctx):
    return dict_get(ctx, "position")

expr.setCallback(selectorFn)
```

## 8D. WigglySelector 类
`WigglySelector` 会持续生成随时间变化的随机权重，适合做逐字符抖动、跳动、呼吸感动画。

### 可动画属性
这些属性返回 `AnimatedProperty`：
* `amount`
* `wigglesPerSecond`
* `correlation`
* `temporalPhase`
* `spatialPhase`
* `minAmount`
* `maxAmount`

### 方法
#### `setBasedOn(name: String)`
设置选择基础，支持：
* `characters`
* `words`
* `lines`

#### `getBasedOn()`
返回当前 `basedOn` 名称。

#### `setMode(name: String)`
设置组合模式，支持：
* `add`
* `subtract`
* `intersect`
* `min`
* `max`

#### `getMode()`
返回当前组合模式名称。

说明：
* `wigglesPerSecond` 控制时间变化频率。
* `correlation` 越高，字符之间的变化越趋向一致；越低，每个字符越独立。
* `temporalPhase` / `spatialPhase` 分别控制时间相位和空间相位。
* `minAmount` / `maxAmount` 用于限制输出权重范围。

示例：

```luna
wiggly = anim.addWigglySelector()
wiggly.setBasedOn("characters")
wiggly.amount = 40
wiggly.wigglesPerSecond = 2.0
wiggly.correlation = 20
wiggly.minAmount = 10
wiggly.maxAmount = 100
```

### 方法
可动画属性不再通过字符串接口管理，而是直接返回 `AnimatedProperty` 对象。当前支持：

- `inst.x`
- `inst.y`
- `inst.scale_x`
- `inst.scale_y`
- `inst.rotation`
- `inst.opacity`
- `inst.volume`
- `inst.font_size`

这些属性都支持下面这组方法：

#### `property.keyframes(entries: List)`
批量重建关键帧。调用时会先清空现有关键帧，再按顺序写入。

- 数值属性条目格式：
  - `[time, value]`
  - `[time, value, type]`
  - `[time, value, type, weight]`
- 颜色属性条目格式：
  - `[time, [r, g, b, a]]`
  - `[time, [r, g, b, a], type]`
  - `[time, [r, g, b, a], type, weight]`

#### `property.add(time: Number, value, [type: String], [weight: Number])`
追加关键帧。

#### `property.set(time: Number, value, [type: String], [weight: Number])`
设置关键帧；同时间点存在则覆盖，否则新增。

#### `property.withPreset(time: Number, value, preset: String)`
使用预设添加关键帧。

#### `property.remove(time: Number)`
删除指定时间点的关键帧，返回 `true` / `false`。

#### `property.clear()`
清空该属性上的全部关键帧。

#### `property.count()`
返回关键帧数量。

#### `property.time(index: Number)`
返回指定关键帧的时间，越界返回 `nil`。

#### `property.value(index: Number)`
返回指定关键帧的值。颜色属性返回 `[r, g, b, a]`。

#### `property.type(index: Number)`
返回指定关键帧的插值类型：`"hold"`、`"linear"`、`"bezier"`。

#### `property.weight(index: Number)`
返回指定关键帧的贝塞尔权重。

#### `property.shift(delta: Number)`
整体平移该属性全部关键帧时间。

#### `property.scaleTimes(factor: Number)`
整体缩放该属性全部关键帧时间。

#### `property.copyFrom(other: AnimatedProperty)`
从另一动画属性复制全部关键帧。

#### 示例
```luna
inst.opacity.keyframes([
    [0, 1.0],
    [5, 0.0, "bezier", 0.5]
])

inst.volume.keyframes([[0, 1.0, "hold"]])
inst.volume.withPreset(8, 0.0, "ease_out")

inst.rotation.add(0, 0)
inst.rotation.set(4, 360, "bezier", 0.7)
inst.scale_x.withPreset(3, 0.5, "ease_in")
```

#### `setStart(time: Number)`
设置该实例在时间线上的起始时间。
* **注意**: 调用后会自动维护所在轨道的时间排序。

#### `getStart()`
返回该实例在时间线上的起始时间。

#### `setDuration(duration: Number)`
设置该实例在时间线上的持续时间。
* **注意**: 修改后会重新计算时间线总时长。

#### `getDuration()`
返回该实例在时间线上的持续时间。

#### `setInPoint(time: Number)`
设置该实例的素材入点偏移。它作用于当前时间线实例，不修改原始 `Clip` 默认值。

#### `getInPoint()`
返回该实例的素材入点偏移。

#### `setZIndex(value: Number)`
设置该实例的绘制层级。较大的 `z_index` 会在较小值之后绘制。

#### `getZIndex()`
返回该实例当前的 `z_index`。

#### `setVisible(visible: Bool)`
设置该实例是否可见。

#### `isVisible()`
返回该实例当前是否可见。

#### `remove()`
从时间线中删除当前实例。
* **返回值**:
    * `true`: 删除成功。
    * `false`: 当前实例已经失效或不存在。

#### `moveToTrack(track: Number, [start: Number])`
把当前实例移动到另一条轨道。
* **参数**:
    * `track`: 目标轨道索引。若轨道不存在，会自动补齐。
    * `start` (可选): 新的时间线起始时间；若省略则保持当前起始时间。
* **返回值**:
    * `true`: 移动成功。
    * `false`: 当前实例已失效，或参数无效。

#### `duplicate(track: Number, [start: Number])`
复制当前实例，并把副本放到指定轨道。
* **参数**:
    * `track`: 目标轨道索引。
    * `start` (可选): 新副本的起始时间；若省略则沿用当前实例起始时间。
* **返回值**:
    * 新的 `ClipInstance`。
    * 失败时返回 `nil`。

动画相关操作请直接通过属性对象完成，例如 `inst.x.shift(...)`、`inst.opacity.copyFrom(other.opacity)`。

#### `addEffect(effect: EffectInstance)`
把一个效果对象挂到当前实例上，并返回这个效果对象本身。
* 现在不再支持字符串形式的 `addEffect("FractalNoise")`。
* 每个效果对象只能挂到一个 `ClipInstance`；重复挂载会失败并返回 `nil`。

#### `getEffectCount()`
返回当前实例上的效果数量。

#### `getEffect(index: Number)`
返回指定索引的 `Effect` 对象。

#### `removeEffect(index: Number)`
删除指定索引的效果。
* **返回值**:
    * `true`: 删除成功。
    * `false`: 索引无效。

#### `clearEffects()`
清空当前实例上的全部效果。

---
## 9. Effect 类 (效果控件)
效果现在是可直接构造的一等对象，再通过 `clipInst.addEffect(effect)` 挂到时间线实例上。第一版内置效果类包括：
* `Tint`
* `Fill`
* `BrightnessContrast`
* `Blur`
* `Glow`
* `Mosaic`
* `Grid`
* `GradientRamp`
* `FractalNoise`
* `DisplacementMap`
* `Posterize`
* `SliderControl`
* `AngleControl`
* `CheckboxControl`
* `PointControl`
* `ColorControl`

### 构造
每个效果都是同名类，支持直接构造；构造参数会映射到对应效果参数。推荐使用关键字参数：

```luna
fractal = FractalNoise(scale=120, evolution=45, contrast=1.3)
mosaic = Mosaic(blockSize=12, sharpColors=true)
tint = Tint(amount=0.8, color=[255, 120, 80, 255])
fractal = clipInst.addEffect(fractal)
mosaic = clipInst.addEffect(mosaic)
tint = clipInst.addEffect(tint)
```

颜色参数在构造时使用 `List` 传入，支持 `[r, g, b]` 或 `[r, g, b, a]`。

### 方法
#### `getName()`
返回效果名称。

#### 直接属性访问
效果参数现在统一通过对象属性读写，不再使用 `setNumber` / `setBool` / `setColor` 这类字符串接口。

数值参数可直接写成：
```luna
blur.radius = 6
ramp.startX = 0
fractal.scale = 120
print fractal.scale
```

布尔参数可直接写成：
```luna
mosaic.sharpColors = true
fractal.invert = false
print mosaic.sharpColors
```

颜色参数使用 `List`：
```luna
tint.color = [255, 120, 80, 255]
ramp.startColor = [255, 0, 0, 255]
```

支持的布尔参数：
* `Mosaic`: `sharpColors`
* `FractalNoise`: `invert`
* `DisplacementMap`: `useLuma`
* `CheckboxControl`: `value`

`DisplacementMap` 还支持两个数值参数：
* `horizontalChannel`
* `verticalChannel`

通道编号约定：
* `0`: red
* `1`: green
* `2`: blue
* `3`: alpha
* `4`: luma

当 `useLuma = true` 时，会忽略 `horizontalChannel` / `verticalChannel`，并按 AE 风格使用“50% 灰为中性点”的亮度位移。
当 `useLuma = false` 时，会对每个像素分别读取横向和纵向通道值，直接生成位移量。

支持的颜色参数：
* `Tint`: `color`
* `Fill`: `color`
* `Grid`: `color`
* `Glow`: `color`
* `GradientRamp`: `startColor`, `endColor`
* `ColorControl`: `color`

#### `setSource(sourceClip: ClipInstance | nil)`
为支持外部源的效果绑定一个外部图层实例；传 `nil` 可清除。
* 当前支持：`DisplacementMap`
* `DisplacementMap` 会优先采样外部图层应用效果后的结果。
* 当前第一版限制：
  如果外部图层自己的效果链里还依赖别的外部源，系统会回退到采样该图层的原始内容。

#### `getSourceClipId()`
返回当前绑定的外部图层实例 id；若未绑定则返回 `nil`。

#### `linkNumber(key: String, otherEffect: Effect, otherKey: String, [scale: Number], [offset: Number])`
把当前效果的数值参数驱动到另一效果的数值参数上。`scale` 和 `offset` 用于做线性映射。

#### `linkColor(key: String, otherEffect: Effect, otherKey: String)`
把当前效果的颜色参数驱动到另一效果的颜色参数上。

#### `unlinkNumber(key: String)`
移除某个数值参数上的驱动关系。

#### `unlinkColor(key: String)`
移除某个颜色参数上的驱动关系。

效果参数的动画接口也统一成属性对象。也就是说：

- 数值参数直接使用 `effect.amount`、`effect.evolution`、`effect.levels`
- 布尔参数也可动画，例如 `effect.sharpColors`
- 颜色参数使用 `effect.startColor`、`effect.endColor`、`effect.color`

这些属性同样返回 `AnimatedProperty`，支持：

- `property.keyframes(...)`
- `property.add(...)`
- `property.set(...)`
- `property.withPreset(...)`
- `property.remove(...)`
- `property.clear()`
- `property.count()`
- `property.time(index)`
- `property.value(index)`
- `property.type(index)`
- `property.weight(index)`
- `property.shift(delta)`
- `property.scaleTimes(factor)`
- `property.copyFrom(otherProperty)`

### 示例
```luna
tint = clipInst.addEffect(Tint(amount=0.8, color=[255, 120, 80, 255]))
blur = clipInst.addEffect(Blur(radius=6))
glow = clipInst.addEffect(Glow(radius=18, intensity=1.1, threshold=0.6, softness=0.2, color=[255, 220, 180, 255]))
fill = clipInst.addEffect(Fill(amount=0.4, color=[20, 140, 255, 255]))

ramp = clipInst.addEffect(GradientRamp(
    startX=0,
    startY=0,
    endX=1,
    endY=1,
    startColor=[255, 0, 0, 255],
    endColor=[255, 255, 0, 255]
))
ramp.startColor.keyframes([
    [0, [255, 0, 0, 255]],
    [1, [0, 255, 0, 255], "linear"]
])

fractal = clipInst.addEffect(FractalNoise(scale=120, evolution=45, invert=false))

disp = clipInst.addEffect(DisplacementMap(scaleX=20, scaleY=12, horizontalChannel=0, verticalChannel=1, useLuma=false))
disp.setSource(otherClipInst)

posterize = clipInst.addEffect(Posterize(levels=6))

slider = clipInst.addEffect(SliderControl(value=10))
slider.value.keyframes([
    [0, 10],
    [1, 20, "bezier", 0.2]
])
posterize.linkNumber("levels", slider, "value", 0.5, 1)

colorCtl = clipInst.addEffect(ColorControl(color=[255, 0, 0, 255]))
colorCtl.color.keyframes([[0, [255, 0, 0, 255]]])
fill.linkColor("color", colorCtl, "color")
```

---
## 10. Project 类 (项目)
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

#### `setPreviewRange(start: Number, end: Number)`
仅设置预览范围，不立即启动预览。

#### `clearPreviewRange()`
清除预览范围，恢复为整条时间线预览。

#### `setBackgroundColor(r: Number, g: Number, b: Number, [a: Number])`
设置项目对应时间线的背景色。
* **注意**: 调用前项目必须已经绑定时间线。

#### `getDuration()`
返回当前项目时间线的总时长；若还未绑定时间线则返回 `0`。

---
## 9. 全局函数 [新增]
#### `addUserPreset(name: String, type: String, weight: Number)`
添加用户自定义关键帧预设。
* **参数**:
    * `name`: 预设名称 (字符串)。
    * `type`: 插值类型 (字符串，支持 "hold"、"linear"、"bezier")。
    * `weight`: 贝塞尔权重 (0.0 - 1.0，仅 "bezier" 有效)。
* **示例**: `addUserPreset("custom_ease", "bezier", 0.3)` // 添加一个自定义贝塞尔预设。

---
## 10. 完整示例代码
```luna
# 1. 创建项目
proj = Project(1920, 1080, 30)
# 2. 创建时间线
tl = Timeline(1920, 1080, 30)
proj.setTimeline(tl)
# 3. 加载并处理素材
clip = Clip("assets/media/test.mp4")
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
    clipInst = tl.add(0, clip, 0)  # 轨道0，从时间线第0秒开始播放
    # 新增: 测试关键帧 - opacity 淡出, volume 渐弱
    clipInst.opacity.keyframes([
        [0, 1.0, "linear"],
        [5, 0.0, "bezier", 0.5]
    ])
    clipInst.volume.keyframes([[0, 1.0, "hold"]])
    clipInst.volume.withPreset(8, 0.0, "ease_out")
    # 5. 添加第二个素材
    clip2 = Clip("assets/media/test.mp4")
    clip2.trim(5, 5) # 截取5秒
    clip2.setPos(500, 500)
    # 轨道1 (上层)，从时间线第2秒开始播放
    clip2Inst = tl.add(1, clip2, 2)
    # 新增: 测试关键帧 - rotation 旋转, scale_x 缩放
    clip2Inst.rotation.keyframes([
        [0, 0, "linear"],
        [4, 360, "bezier", 0.7]
    ])
    clip2Inst.scale_x.withPreset(0, 1.0, "ease_in")
    clip2Inst.scale_x.withPreset(3, 0.5, "ease_in")
    print "时间线总时长: "
    print tl.duration
    # 6. 启动预览
    proj.preview()
```
