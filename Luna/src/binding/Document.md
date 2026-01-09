这里为您整理了一份基于 `src/binding/bind_video.c` 源码的 Luna 语言视频引擎 API 使用文档。

---

# Luna 视频引擎 API 文档

本文档描述了 Luna 语言内置的视频处理核心类：`Clip`、`Timeline` 和 `Project`。这些类由底层 C 语言实现，提供了高性能的视频加载、合成与渲染功能。

## 1. Clip 类 (素材)

`Clip` 类用于加载和操作单个视频文件。它是时间线上的基本元素。

### 构造函数

#### `Clip(path: String)`
创建一个新的素材实例并加载视频元数据。

*   **参数**:
    *   `path`: 视频文件的路径 (字符串)。
*   **返回值**: `Clip` 实例。
*   **注意**: 如果文件加载失败，会在控制台打印错误，建议检查文件路径是否正确。

### 属性 (只读/同步)

这些属性在初始化或调用特定方法后会自动更新：

*   `width`: 视频原始宽度 (Number)。
*   `height`: 视频原始高度 (Number)。
*   `fps`: 视频帧率 (Number)。
*   `duration`: 素材的持续时间（秒）(Number)。
*   `in_point`: 素材的入点时间（从视频文件开头的偏移秒数）(Number)。
*   `default_x`: X 轴坐标 (Number)。
*   `default_y`: Y 轴坐标 (Number)。
*   `default_scale_x`: X 轴缩放比例 (Number)。
*   `default_scale_y`: Y 轴缩放比例 (Number)。
*   `default_opacity`: 不透明度 (0.0 - 1.0) (Number)。

### 方法

#### `trim(start: Number, duration: Number)`
裁剪素材。

*   **参数**:
    *   `start`: 入点时间（秒）。表示从视频文件的第几秒开始播放。
    *   `duration`: 持续时间（秒）。表示裁剪后素材在时间线上占据的时长。
*   **示例**: `clip.trim(10, 5)` // 从第10秒开始，截取5秒长的片段。

#### `setScale(sx: Number, [sy: Number])`
设置素材的缩放比例。

*   **参数**:
    *   `sx`: X 轴缩放比例 (1.0 为原始大小)。
    *   `sy`: (可选) Y 轴缩放比例。如果省略，则 `sy` 等于 `sx` (保持等比缩放)。
*   **示例**:
    *   `clip.setScale(0.5)` // 缩小一半
    *   `clip.setScale(1.0, 0.5)` // 压扁

#### `setPos(x: Number, y: Number)`
设置素材在画布上的位置。

*   **参数**:
    *   `x`: X 轴坐标 (像素)。
    *   `y`: Y 轴坐标 (像素)。
*   **示例**: `clip.setPos(100, 200)`

#### `setOpacity(opacity: Number)`
设置素材的不透明度。

*   **参数**:
    *   `opacity`: 0.0 (全透明) 到 1.0 (不透明)。
*   **示例**: `clip.setOpacity(0.8)`

#### `export(filename: Number)`
将当前素材（包含裁剪设置）导出为新文件。

*   **参数**:
    *   `filename`: 导出文件的路径。

---

## 2. Timeline 类 (时间线)

`Timeline` 用于管理多个轨道和素材，负责将素材组合在一起。

### 构造函数

#### `Timeline(width: Number, height: Number, fps: Number)`
创建一个新的时间线。

*   **参数**:
    *   `width`: 时间线画布宽度。
    *   `height`: 时间线画布高度。
    *   `fps`: 时间线帧率。

### 属性

*   `duration`: 当前时间线的总时长（秒），会根据添加的素材自动增长 (Number)。

### 方法

#### `add(trackId: Number, clip: Clip, startTime: Number)`
将素材添加到指定轨道。

*   **参数**:
    *   `trackId`: 轨道索引 (整数，从 0 开始)。如果轨道不存在会自动创建。
    *   `clip`: 要添加的 `Clip` 实例。
    *   `startTime`: 素材在时间线上的起始时间（秒）。
*   **注意**: 该操作会根据素材的结束时间自动更新时间线的 `duration` 属性。

---

## 3. Project 类 (项目)

`Project` 是最高层级的容器，用于配置全局参数并连接渲染引擎。

### 构造函数

#### `Project(width: Number, height: Number, fps: Number)`
创建一个新项目。

*   **参数**:
    *   `width`: 项目宽度。
    *   `height`: 项目高度。
    *   `fps`: 项目帧率。

### 属性

*   `width`: 项目宽度 (Number)。
*   `height`: 项目高度 (Number)。
*   `fps`: 项目帧率 (Number)。

### 方法

#### `setTimeline(timeline: Timeline)`
将时间线绑定到项目。

*   **参数**:
    *   `timeline`: 一个 `Timeline` 实例。
*   **注意**: 必须绑定时间线后，项目才能进行预览或渲染。

#### `preview()`
激活当前项目进行实时预览。

*   **描述**: 告诉宿主程序（Luna Host）将当前项目设置为活跃项目，开始图形渲染循环。

---

## 4. 完整示例代码

```javascript
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

    # 4. 添加到时间线
    # 轨道0，从时间线第0秒开始播放
    tl.add(0, clip, 0)

    # 5. 添加第二个素材
    var clip2 = Clip("test.mp4")
    clip2.trim(5, 5) # 截取5秒
    clip2.setPos(500, 500)
    # 轨道1 (上层)，从时间线第2秒开始播放
    tl.add(1, clip2, 2)

    print "时间线总时长: "
    print tl.duration

    # 6. 启动预览
    proj.preview()
```