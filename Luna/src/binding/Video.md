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

#### `setKeyframe(property: String, time: Number, value: Number, type: String, [weight: Number])`
设置关键帧。
* **描述**:
    * 如果相同 `property` 在相同 `time` 上已经存在关键帧，则覆盖其值和插值参数。
    * 如果不存在，则新增关键帧。
* **参数**:
    * `property`、`time`、`value`、`type`、`weight`: 同 `addKeyframe(...)`。
* **示例**: `inst.setKeyframe("x", 0, 320, "bezier", 0.4)`

#### `removeKeyframe(property: String, time: Number)`
删除指定时间点上的关键帧。
* **参数**:
    * `property`: 属性名称。
    * `time`: 关键帧时间。
* **返回值**:
    * `true`: 删除成功。
    * `false`: 该时间点不存在关键帧。
* **示例**: `inst.removeKeyframe("opacity", 5)`

#### `clearKeyframes(property: String)`
清空某个属性上的全部关键帧。
* **参数**:
    * `property`: 属性名称。
* **示例**: `inst.clearKeyframes("rotation")`

#### `getKeyframeCount(property: String)`
获取某个属性当前的关键帧数量。
* **参数**:
    * `property`: 属性名称。
* **返回值**: 关键帧数量（Number）。
* **示例**: `print inst.getKeyframeCount("x")`

#### `getKeyframeTime(property: String, index: Number)`
获取某个属性下指定索引关键帧的时间。
* **参数**:
    * `property`: 属性名称。
    * `index`: 关键帧索引，按时间排序，从 `0` 开始。
* **返回值**:
    * 成功时返回关键帧时间。
    * 越界时返回 `nil`。
* **示例**: `print inst.getKeyframeTime("x", 0)`

#### `getKeyframeValue(property: String, index: Number)`
获取某个属性下指定索引关键帧的值。
* **参数**:
    * `property`: 属性名称。
    * `index`: 关键帧索引，按时间排序，从 `0` 开始。
* **返回值**:
    * 成功时返回关键帧值。
    * 越界时返回 `nil`。
* **示例**: `print inst.getKeyframeValue("x", 0)`

#### `getKeyframeType(property: String, index: Number)`
获取某个属性下指定索引关键帧的插值类型。
* **返回值**:
    * 成功时返回 `"hold"`、`"linear"` 或 `"bezier"`。
    * 越界时返回 `nil`。

#### `getKeyframeWeight(property: String, index: Number)`
获取某个属性下指定索引关键帧的贝塞尔权重。
* **返回值**:
    * 成功时返回权重值。
    * 越界时返回 `nil`。

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

#### `shiftKeyframes(property: String, delta: Number)`
把某个属性上的全部关键帧整体平移 `delta` 秒。

#### `scaleKeyframeTimes(property: String, factor: Number)`
按比例缩放某个属性上全部关键帧的时间位置。
* **示例**: `inst.scaleKeyframeTimes("x", 2)` 会把 `0.5s` 的关键帧移动到 `1.0s`。

#### `copyKeyframesFrom(property: String, other: ClipInstance, otherProperty: String)`
把另一个实例某个属性上的关键帧复制到当前实例的目标属性上。
* **描述**:
    * 目标属性原有的关键帧会被替换。
    * 关键帧的时间、值、插值类型和贝塞尔权重都会一并复制。

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
var fractal = FractalNoise(scale=120, evolution=45, contrast=1.3)
var mosaic = Mosaic(blockSize=12, sharpColors=true)
var tint = Tint(amount=0.8, color=[255, 120, 80, 255])
clipInst.addEffect(fractal)
clipInst.addEffect(mosaic)
clipInst.addEffect(tint)
```

颜色参数在构造时使用 `List` 传入，支持 `[r, g, b]` 或 `[r, g, b, a]`。

### 方法
#### `getName()`
返回效果名称。

#### `setNumber(key: String, value: Number)`
设置数值参数。
* `Tint`: `amount`
* `Fill`: `amount`
* `BrightnessContrast`: `brightness`, `contrast`
* `Blur`: `radius`
* `Mosaic`: `blockSize`
* `Grid`: `sizeX`, `sizeY`, `lineWidth`, `opacity`
* `GradientRamp`: `startX`, `startY`, `endX`, `endY`, `blend`
* `FractalNoise`: `scale`, `evolution`, `contrast`, `brightness`, `octaves`, `amount`, `offsetX`, `offsetY`
* `DisplacementMap`: `scaleX`, `scaleY`, `amount`, `offsetX`, `offsetY`
* `Posterize`: `levels`, `amount`
* `SliderControl`: `value`
* `AngleControl`: `angle`
* `CheckboxControl`: `value`
* `PointControl`: `x`, `y`

#### `getNumber(key: String)`
读取数值参数。

#### `setBool(key: String, value: Bool)`
设置布尔参数。
* `Mosaic`: `sharpColors`
* `FractalNoise`: `invert`
* `DisplacementMap`: `useLuma`
* `CheckboxControl`: `value`

#### `getBool(key: String)`
读取布尔参数。

#### `setColor(key: String, r: Number, g: Number, b: Number, [a: Number])`
设置颜色参数。
* `Tint`: `color`
* `Fill`: `color`
* `Grid`: `color`
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

#### `addNumberKeyframe(key: String, time: Number, value: Number, type: String, [weight: Number])`
为数值参数添加关键帧。

#### `setNumberKeyframe(key: String, time: Number, value: Number, type: String, [weight: Number])`
设置数值参数关键帧；同时间点存在则覆盖，否则新增。

#### `removeNumberKeyframe(key: String, time: Number)`
删除数值参数上的关键帧。

#### `clearNumberKeyframes(key: String)`
清空某个数值参数上的关键帧。

#### `getNumberKeyframeCount(key: String)`
返回某个数值参数的关键帧数量。

#### `getNumberKeyframeTime(key: String, index: Number)`
返回数值参数指定关键帧的时间。

#### `getNumberKeyframeValue(key: String, index: Number)`
返回数值参数指定关键帧的值。

#### `getNumberKeyframeType(key: String, index: Number)`
返回数值参数指定关键帧的插值类型。

#### `getNumberKeyframeWeight(key: String, index: Number)`
返回数值参数指定关键帧的贝塞尔权重。

#### `shiftNumberKeyframes(key: String, delta: Number)`
整体平移某个数值参数的全部关键帧时间。

#### `scaleNumberKeyframeTimes(key: String, factor: Number)`
整体缩放某个数值参数的全部关键帧时间。

#### `copyNumberKeyframesFrom(key: String, otherEffect: Effect, otherKey: String)`
把另一效果的数值参数关键帧复制到当前效果参数上。

#### `addColorKeyframe(key: String, time: Number, r: Number, g: Number, b: Number, a: Number, type: String, [weight: Number])`
为颜色参数添加关键帧。颜色会拆成 RGBA 四个通道分别动画。

#### `setColorKeyframe(key: String, time: Number, r: Number, g: Number, b: Number, a: Number, type: String, [weight: Number])`
设置颜色参数关键帧；同时间点存在则覆盖，否则新增。

#### `removeColorKeyframe(key: String, time: Number)`
删除颜色参数在指定时间点上的关键帧。

#### `clearColorKeyframes(key: String)`
清空某个颜色参数上的关键帧。

#### `getColorKeyframeCount(key: String)`
返回某个颜色参数的关键帧数量。

#### `getColorKeyframeTime(key: String, index: Number)`
返回颜色参数指定关键帧的时间。

#### `getColorKeyframeValue(key: String, index: Number, channel: String)`
返回颜色参数指定关键帧某个通道的值；`channel` 取 `r`、`g`、`b`、`a`。

#### `getColorKeyframeType(key: String, index: Number)`
返回颜色参数指定关键帧的插值类型。

#### `getColorKeyframeWeight(key: String, index: Number)`
返回颜色参数指定关键帧的贝塞尔权重。

#### `shiftColorKeyframes(key: String, delta: Number)`
整体平移某个颜色参数的全部关键帧时间。

#### `scaleColorKeyframeTimes(key: String, factor: Number)`
整体缩放某个颜色参数的全部关键帧时间。

#### `copyColorKeyframesFrom(key: String, otherEffect: Effect, otherKey: String)`
把另一效果的颜色参数关键帧复制到当前效果参数上。

### 示例
```luna
var tint = Tint(amount=0.8, color=[255, 120, 80, 255])
clipInst.addEffect(tint)
tint.setNumber("amount", 0.8)
tint.setColor("color", 255, 120, 80, 255)

var blur = Blur(radius=6)
clipInst.addEffect(blur)
blur.setNumber("radius", 6)

var fill = Fill(amount=0.4, color=[20, 140, 255, 255])
clipInst.addEffect(fill)
fill.setNumber("amount", 0.4)
fill.setColor("color", 20, 140, 255, 255)

var ramp = GradientRamp(startX=0, startY=0, endX=1, endY=1, startColor=[255, 0, 0, 255], endColor=[255, 255, 0, 255])
clipInst.addEffect(ramp)
ramp.setNumber("startX", 0)
ramp.setNumber("startY", 0)
ramp.setNumber("endX", 1)
ramp.setNumber("endY", 1)
ramp.setColor("startColor", 255, 0, 0, 255)
ramp.setColor("endColor", 255, 255, 0, 255)
ramp.addColorKeyframe("startColor", 0, 255, 0, 0, 255, "linear")
ramp.setColorKeyframe("startColor", 1, 0, 255, 0, 255, "linear")

var fractal = FractalNoise(scale=120, evolution=45, invert=false)
clipInst.addEffect(fractal)
fractal.setNumber("scale", 120)
fractal.setNumber("evolution", 45)
fractal.setBool("invert", false)

var disp = DisplacementMap(scaleX=20, scaleY=12, useLuma=true)
clipInst.addEffect(disp)
disp.setNumber("scaleX", 20)
disp.setNumber("scaleY", 12)
disp.setBool("useLuma", true)
disp.setSource(otherClipInst)

var posterize = Posterize(levels=6)
clipInst.addEffect(posterize)
posterize.setNumber("levels", 6)

var slider = SliderControl(value=10)
clipInst.addEffect(slider)
slider.addNumberKeyframe("value", 0, 10, "linear")
slider.setNumberKeyframe("value", 1, 20, "bezier", 0.2)
posterize.linkNumber("levels", slider, "value", 0.5, 1)

var colorCtl = ColorControl(color=[255, 0, 0, 255])
clipInst.addEffect(colorCtl)
colorCtl.addColorKeyframe("color", 0, 255, 0, 0, 255, "linear")
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
