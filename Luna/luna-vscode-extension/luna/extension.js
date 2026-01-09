const vscode = require('vscode');

// 这里定义 Luna 语言的文档数据
const lunaDocs = {
    // 类
    "Clip": {
        detail: "Class: Clip(path: String)",
        documentation: "创建一个新的素材实例并加载视频元数据。\n\n**参数**:\n- `path`: 视频文件路径\n\n**示例**:\n`var c = Clip(\"video.mp4\")`"
    },
    "Timeline": {
        detail: "Class: Timeline(width, height, fps)",
        documentation: "创建一个新的时间线，用于管理轨道和素材合成。\n\n**参数**:\n- `width`: 画布宽度\n- `height`: 画布高度\n- `fps`: 帧率"
    },
    "Project": {
        detail: "Class: Project(width, height, fps)",
        documentation: "最高层级的项目容器，负责全局配置和预览。\n\n**方法**:\n- `setTimeline(tl)`\n- `preview()`"
    },

    // Clip 方法
    "trim": {
        detail: "Method: trim(start, duration)",
        documentation: "裁剪素材。\n\n**参数**:\n- `start`: 入点时间(秒)\n- `duration`: 持续时长(秒)"
    },
    "setScale": {
        detail: "Method: setScale(sx, [sy])",
        documentation: "设置素材缩放比例。\n\n**示例**:\n`clip.setScale(0.5)`"
    },
    "setPos": {
        detail: "Method: setPos(x, y)",
        documentation: "设置素材在画布上的坐标(像素)。"
    },
    "setOpacity": {
        detail: "Method: setOpacity(opacity)",
        documentation: "设置不透明度 (0.0 - 1.0)。"
    },
    "export": {
        detail: "Method: export(filename)",
        documentation: "导出当前素材为文件。"
    },

    // Timeline 方法
    "add": {
        detail: "Method: add(trackId, clip, startTime)",
        documentation: "将素材添加到指定轨道。\n\n**参数**:\n- `trackId`: 轨道索引(0, 1...)\n- `clip`: Clip对象\n- `startTime`: 时间线上的开始时间(秒)"
    },

    // Project 方法
    "setTimeline": {
        detail: "Method: setTimeline(timeline)",
        documentation: "将时间线绑定到项目。"
    },
    "preview": {
        detail: "Method: preview()",
        documentation: "启动图形窗口进行实时预览。"
    },

    // 属性 (通用或特定)
    "width": { detail: "Property: width (Number)", documentation: "宽度 (只读)" },
    "height": { detail: "Property: height (Number)", documentation: "高度 (只读)" },
    "fps": { detail: "Property: fps (Number)", documentation: "帧率 (只读)" },
    "duration": { detail: "Property: duration (Number)", documentation: "持续时间 (秒)" },
    "in_point": { detail: "Property: in_point (Number)", documentation: "素材入点偏移量" },
    
    // 内置函数
    "print": {
        detail: "Keyword: print",
        documentation: "将内容输出到控制台。"
    },
    "var": {
        detail: "Keyword: var",
        documentation: "声明一个变量。"
    }
};

/**
 * 插件激活时调用此函数
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
    console.log('Luna extension is now active!');

    // 注册悬停提示提供者 (Hover Provider)
    const hoverProvider = vscode.languages.registerHoverProvider('luna', {
        provideHover(document, position, token) {
            // 1. 获取当前鼠标位置的单词范围
            const range = document.getWordRangeAtPosition(position);
            if (!range) return;

            // 2. 获取单词内容
            const word = document.getText(range);

            // 3. 在文档库中查找
            if (lunaDocs[word]) {
                const item = lunaDocs[word];
                
                // 4. 构建 Markdown 格式的提示内容
                const markdown = new vscode.MarkdownString();
                markdown.appendCodeblock(item.detail, 'luna'); // 显示函数签名
                markdown.appendMarkdown("---\n");
                markdown.appendMarkdown(item.documentation);   // 显示详细文档

                return new vscode.Hover(markdown);
            }
        }
    });

    context.subscriptions.push(hoverProvider);
}

/**
 * 插件停用时调用
 */
function deactivate() {}

module.exports = {
    activate,
    deactivate
};