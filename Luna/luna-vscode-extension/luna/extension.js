const vscode = require('vscode');

const keywordItems = [
    ['if', 'Keyword', '条件分支。'],
    ['else', 'Keyword', '条件分支的备用块。'],
    ['while', 'Keyword', 'while 循环。'],
    ['for', 'Keyword', 'for-in 循环。'],
    ['try', 'Keyword', '异常处理开始。'],
    ['except', 'Keyword', '异常处理分支。'],
    ['return', 'Keyword', '从函数返回值。'],
    ['break', 'Keyword', '跳出循环。'],
    ['continue', 'Keyword', '继续下一轮循环。'],
    ['class', 'Keyword', '定义类。'],
    ['fun', 'Keyword', '定义函数。'],
    ['lam', 'Keyword', '定义 lambda。'],
    ['print', 'Keyword', '输出到控制台。'],
    ['true', 'Literal', '布尔真。'],
    ['false', 'Literal', '布尔假。'],
    ['nil', 'Literal', '空值。'],
    ['this', 'Keyword', '当前实例。'],
    ['super', 'Keyword', '父类方法访问。']
];

const classItems = [
    ['Clip', 'Class: Clip(path)', '视频素材。'],
    ['Image', 'Class: Image(path)', '图片图层。'],
    ['Text', 'Class: Text(content)', '文字图层。'],
    ['Solid', 'Class: Solid(width, height, r, g, b[, a])', '纯色图层。'],
    ['Adjustment', 'Class: Adjustment(width, height)', '调整图层。'],
    ['Group', 'Class: Group(width, height, fps)', '分组图层。'],
    ['Precomp', 'Class: Precomp(width, height, fps)', '预合成图层。'],
    ['Timeline', 'Class: Timeline(width, height, fps)', '时间线。'],
    ['Project', 'Class: Project(width, height, fps)', '项目容器。'],
    ['AnimatedProperty', 'Class: AnimatedProperty', '动画属性句柄。']
];

const effectItems = [
    'Tint', 'Fill', 'BrightnessContrast', 'Blur', 'Mosaic', 'Grid',
    'GradientRamp', 'FractalNoise', 'DisplacementMap', 'Posterize',
    'SliderControl', 'AngleControl', 'CheckboxControl', 'PointControl', 'ColorControl'
].map(name => [name, `Effect: ${name}(...)`, '可直接构造并通过 clipInst.addEffect(effect) 挂载。']);

const functionItems = [
    ['len', 'Function: len(value)', '获取长度。'],
    ['push', 'Function: push(list, value)', '向 List 追加元素。'],
    ['pop', 'Function: pop(list)', '从 List 末尾弹出元素。'],
    ['get', 'Function: get(listOrDict, key)', '获取元素。'],
    ['set', 'Function: set(listOrDict, key, value)', '设置元素。'],
    ['clear', 'Function: clear(listOrDict)', '清空容器。'],
    ['range', 'Function: range(n)', '构造数字范围。'],
    ['addUserPreset', 'Function: addUserPreset(name, type, weight)', '注册用户关键帧预设。']
];

const methodItems = [
    ['setTimeline', 'Method: setTimeline(timeline)', '绑定时间线。'],
    ['preview', 'Method: preview([start, end])', '激活预览。'],
    ['add', 'Method: add(track, clip, start)', '向时间线添加图层。'],
    ['addEffect', 'Method: addEffect(effect)', '向 ClipInstance 挂载效果。'],
    ['setSource', 'Method: setSource(clipInst | nil)', '为支持外部源的效果设置 source 图层。'],
    ['linkNumber', 'Method: linkNumber(key, otherEffect, otherKey[, scale, offset])', '驱动数值参数。'],
    ['linkColor', 'Method: linkColor(key, otherEffect, otherKey)', '驱动颜色参数。'],
    ['setDuration', 'Method: setDuration(duration)', '设置实例时长。'],
    ['setVisible', 'Method: setVisible(flag)', '设置实例可见性。']
];

const animatedPropertyItems = [
    ['keyframes', 'AnimatedProperty.keyframes(entries)', '批量重建关键帧。'],
    ['add', 'AnimatedProperty.add(time, value[, type[, weight]])', '追加关键帧。'],
    ['set', 'AnimatedProperty.set(time, value[, type[, weight]])', '设置关键帧。'],
    ['withPreset', 'AnimatedProperty.withPreset(time, value, preset)', '用预设添加关键帧。'],
    ['remove', 'AnimatedProperty.remove(time)', '删除关键帧。'],
    ['clear', 'AnimatedProperty.clear()', '清空关键帧。'],
    ['count', 'AnimatedProperty.count()', '关键帧数量。'],
    ['time', 'AnimatedProperty.time(index)', '关键帧时间。'],
    ['value', 'AnimatedProperty.value(index)', '关键帧值。'],
    ['type', 'AnimatedProperty.type(index)', '关键帧插值类型。'],
    ['weight', 'AnimatedProperty.weight(index)', '关键帧贝塞尔权重。'],
    ['shift', 'AnimatedProperty.shift(delta)', '整体平移关键帧时间。'],
    ['scaleTimes', 'AnimatedProperty.scaleTimes(factor)', '整体缩放关键帧时间。'],
    ['copyFrom', 'AnimatedProperty.copyFrom(otherProperty)', '复制另一属性的关键帧。']
];

const allItems = [
    ...keywordItems,
    ...classItems,
    ...effectItems,
    ...functionItems,
    ...methodItems,
    ...animatedPropertyItems
];

const lunaDocs = Object.fromEntries(
    allItems.map(([name, detail, documentation]) => [name, { detail, documentation }])
);

function createCompletionItem(name, detail, documentation, kind) {
    const item = new vscode.CompletionItem(name, kind);
    item.detail = detail;
    item.documentation = new vscode.MarkdownString(documentation);
    return item;
}

function activate(context) {
    console.log('Luna extension is now active!');

    const hoverProvider = vscode.languages.registerHoverProvider('luna', {
        provideHover(document, position) {
            const range = document.getWordRangeAtPosition(position);
            if (!range) return;
            const word = document.getText(range);
            const item = lunaDocs[word];
            if (!item) return;

            const markdown = new vscode.MarkdownString();
            markdown.appendCodeblock(item.detail, 'luna');
            markdown.appendMarkdown('---\n');
            markdown.appendMarkdown(item.documentation);
            return new vscode.Hover(markdown);
        }
    });

    const completionProvider = vscode.languages.registerCompletionItemProvider(
        'luna',
        {
            provideCompletionItems(document, position) {
                const linePrefix = document.lineAt(position).text.slice(0, position.character);
                const afterDot = /\.\s*[A-Za-z_0-9]*$/.test(linePrefix);

                if (afterDot) {
                    return [
                        ...animatedPropertyItems.map(([n, d, doc]) => createCompletionItem(n, d, doc, vscode.CompletionItemKind.Method)),
                        ...methodItems.map(([n, d, doc]) => createCompletionItem(n, d, doc, vscode.CompletionItemKind.Method))
                    ];
                }

                return [
                    ...keywordItems.map(([n, d, doc]) => createCompletionItem(n, d, doc, vscode.CompletionItemKind.Keyword)),
                    ...classItems.map(([n, d, doc]) => createCompletionItem(n, d, doc, vscode.CompletionItemKind.Class)),
                    ...effectItems.map(([n, d, doc]) => createCompletionItem(n, d, doc, vscode.CompletionItemKind.Constructor)),
                    ...functionItems.map(([n, d, doc]) => createCompletionItem(n, d, doc, vscode.CompletionItemKind.Function))
                ];
            }
        },
        '.'
    );

    context.subscriptions.push(hoverProvider, completionProvider);
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};
