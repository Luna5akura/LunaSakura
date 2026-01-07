# LunaCode - Syntax Highlighter for Luna Language

[![VS Code Marketplace Version](https://img.shields.io/vscode-marketplace/v/your-publisher.luna.svg)](https://marketplace.visualstudio.com/items?itemName=your-publisher.luna)
[![VS Code Marketplace Downloads](https://img.shields.io/vscode-marketplace/d/your-publisher.luna.svg)](https://marketplace.visualstudio.com/items?itemName=your-publisher.luna)
[![License](https://img.shields.io/github/license/your-username/luna-vscode-extension.svg)](https://github.com/your-username/luna-vscode-extension/blob/main/LICENSE)

LunaCode 是一个 VS Code 扩展，提供 Luna 编程语言的语法高亮支持。Luna 是一种类似于 Python 的脚本语言，支持关键字、字符串、数字、注释和运算符的高亮显示。

## 功能特性
- 支持 Luna 核心关键字（如 `if`、`class`、`fun`、`var` 等）的突出显示。
- 自动识别字符串、数字、注释（`#` 开头）和运算符。
- 括号匹配和自动缩进规则，适应 Luna 的 Python-style 缩进语法。
- 兼容 VS Code 的亮/暗主题。

## 安装
1. 在 VS Code 中打开扩展视图（Ctrl+Shift+X）。
2. 搜索 “LunaCode” 并安装。
3. 或者，从 [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=your-publisher.luna) 下载 .vsix 文件，手动安装（Extensions > ... > Install from VSIX...）。

## 使用示例
1. 创建一个 `.luna` 文件，例如 `example.luna`：
   ```luna
   # 这是一个注释
   var message = "Hello, Luna!"
   print message  # 输出字符串

   if true:
       fun greet(name):
           return "Welcome, " + name