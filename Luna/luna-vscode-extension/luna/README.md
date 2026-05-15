# LunaCode

LunaCode 是 Luna 语言的 VS Code 扩展。当前版本已经和仓库里的新语法/API 对齐，重点覆盖：

- 现代 Luna 关键字：`fun`、`lam`、`try`、`except`、`class`、`super`、`this`
- 当前内建类与效果类：`Project`、`Timeline`、`Clip`、`Image`、`Solid`、`Text`、`Adjustment`、`FractalNoise`、`DisplacementMap` 等
- 属性式动画 API：`prop.keyframes(...)`、`prop.withPreset(...)`、`prop.copyFrom(...)`
- 效果对象模型：`clipInst.addEffect(FractalNoise(...))`
- Hover 文档与基础 completion
- 常用 snippets

## 当前能力

- 语法高亮：
  - 关键字、字符串、数字、注释
  - 类定义、函数定义、方法定义
  - 内建类、效果类、内建函数
  - 点访问下的常用方法和动画属性方法
- 编辑器行为：
  - `#` 行注释
  - 缩进式代码块自动缩进
  - 括号/引号自动补全
  - `#region` / `#endregion` folding marker
- 编辑辅助：
  - hover 文档
  - completion
  - snippets

## 示例

```luna
width = 640
height = 360
fps = 24

proj = Project(width, height, fps)
tl = Timeline(width, height, fps)
proj.setTimeline(tl)

base = Solid(width, height, 255, 255, 255, 255)
inst = tl.add(0, base, 0)
inst.setDuration(6)

ramp = inst.addEffect(GradientRamp(
    startX=0.0,
    startY=0.0,
    endX=1.0,
    endY=1.0,
    blend=1.0
))

ramp.startColor.keyframes([
    [0.0, [255, 0, 0, 255]],
    [2.0, [0, 255, 0, 255]],
    [4.0, [0, 0, 255, 255]]
])

proj.preview(0, 6)
```

## 安装

1. 在 VS Code 中打开扩展视图。
2. 选择 `Install from VSIX...`
3. 安装当前目录下的 `.vsix` 包，或先重新打包后安装。

## Snippets

当前内置了这些快捷片段：

- `project`
- `keyframes`
- `colorkf`
- `effect`
- `fun`
- `class`
- `try`
