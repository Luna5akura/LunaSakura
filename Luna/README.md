- **优先级考虑**：先完善核心语言特性（方向1），因为它们是脚本编写的基础，能提升开发效率和用户体验。然后逐步实现应用层功能（方向2），如预览、音频和动画。这些功能依赖于语言特性（例如，异常处理用于错误恢复，lambda用于简洁动画表达式）。
- **阶段划分**：分为5个阶段，每个阶段聚焦3-5个关键任务。每个任务包括：
  - **描述**：简要说明功能和益处。
  - **优先级**：高/中/低（基于对整体项目的贡献）。
  - **涉及文件**：最小化参考列表，聚焦核心修改点。
  - **预计时间**：假设中等经验开发者，单位为“人日”（可根据实际情况调整）。
  - **依赖**：前置条件。
- **总体时间线**：假设全职开发，预计总时长8-12周。每个阶段后进行测试和重构。
- **风险与建议**：关注垃圾回收（GC）和性能优化（如在动画系统中避免热路径瓶颈）。使用DEBUG宏测试新特性。优先实现MVP（最小可用产品），然后迭代。

### **总体开发原则**
- **测试策略**：每个阶段结束时，编写单元测试脚本（使用新语言特性）和集成测试（例如，预览一个简单视频）。
- **版本控制**：每个阶段提交一个里程碑分支。
- **工具**：使用Valgrind检查内存泄漏，GDB调试虚拟机。
- **文档**：更新README，添加新API示例。

---

### **阶段1: 核心语言增强 (Core Language Improvements)**
**目标**：实现方向1的关键特性，提升脚本表达力（如简化动画表达式）。为后续Roadmap铺路。
**预计时长**：10-15人日。
1. **添加Lambda/匿名函数**（方向1: Lambda）。
   - 描述：支持`lambda x: x+1`，通过ObjClosure实现。益处：简洁回调（如动画缓动函数）。
   - 优先级：高。
   - 涉及文件：`src/vm/compiler.c` (添加语法规则), `src/vm/scanner.c` (关键字), `src/vm/vm.c` (闭包执行)。
   - 预计时间：3人日。
   - 依赖：阶段0。

2. **三元运算符**（方向1: 三元运算符）。
   - 描述：支持`a if cond else b`。益处：简化条件表达式。
   - 优先级：中。
   - 涉及文件：`src/vm/compiler.c` (parsePrecedence添加规则), `src/vm/chunk.h` (新OP码如OP_TERNARY)。
   - 预计时间：2人日。
   - 依赖：阶段0。

3. **基本异常处理 (Try/Except)**（方向1: Try/Except）。
   - 描述：实现简单`try: ... except: ...`，捕获runtimeError。益处：视频加载失败时优雅处理。
   - 优先级：高（提升鲁棒性）。
   - 涉及文件：`src/vm/compiler.c` (新语句解析), `src/vm/vm.c` (异常栈帧处理), `src/vm/chunk.h` (OP_TRY/OP_THROW)。
   - 预计时间：4人日。
   - 依赖：阶段0。

4. **默认/关键字参数**（方向1: 默认参数、关键字参数）。
   - 描述：函数定义支持`def foo(x=1)`，调用支持`foo(x=2)`。
   - 优先级：中。
   - 涉及文件：`src/vm/compiler.c` (function参数解析), `src/vm/vm.c` (callValue处理)。
   - 预计时间：3人日。
   - 依赖：Lambda实现。

---

### **阶段2: 预览体验增强 (Preview Enhancements)**
**目标**：实现方向2的阶段一。整合语言特性（如用lambda定义自定义预览逻辑）。
**预计时长**：7-10人日。
1. **预览时间范围选择**（方向2: Loop Range）。
   - 描述：API `Project.preview(start, end)`。
   - 优先级：高。
   - 涉及文件：`src/engine/timeline.h` (结构体字段), `src/binding/bind_video.c` (nativeSetPreviewRange), `src/main.c` (主循环边界检查)。
   - 预计时间：2人日。
   - 依赖：阶段1（异常处理用于无效范围）。

2. **预览视口控制**（方向2: Viewport Control）。
   - 描述：API `setPreviewScale(factor)`。
   - 优先级：高。
   - 涉及文件：`src/main.c` (窗口大小逻辑), `src/engine/compositor.c` (glViewport调整)。
   - 预计时间：2人日。
   - 依赖：阶段1。

3. **集成语言特性测试**（整合方向1）
   - 描述：用新特性编写预览脚本示例（如lambda过滤时间范围）。
   - 优先级：中。
   - 涉及文件：无主要修改；编写测试脚本。
   - 预计时间：2人日。
   - 依赖：本阶段其他任务。

4. **运算符重载初步**（方向1: 运算符重载）。
   - 描述：类支持`__add`等方法。益处：自定义时间线运算。
   - 优先级：中（为动画铺路）。
   - 涉及文件：`src/vm/vm.c` (binary操作检查方法), `src/vm/compiler.c` (方法绑定)。
   - 预计时间：2人日。
   - 依赖：阶段1。

---

### **阶段3: 音频系统 (Audio System)**
**目标**：实现方向2的阶段二。添加音频相关语言特性（如异常捕获解码错误）。
**预计时长**：10-14人日。
1. **基础混音**（方向2: Basic Mixing）。
   - 描述：解码并叠加音频流。
   - 优先级：高。
   - 涉及文件：`src/engine/timeline.h` (音频索引), `src/engine/compositor.c` (mix_audio函数), `src/main.c` (SDL音频初始化)。
   - 预计时间：4人日。
   - 依赖：阶段2。

2. **音频相关API**（扩展）。
   - 描述：`setVolume(clip, level)` 等。
   - 优先级：中。
   - 涉及文件：`src/binding/bind_video.c` (新native函数), `src/engine/video.h` (扩展VideoMeta)。
   - 预计时间：2人日。
   - 依赖：本阶段混音。

3. **推导式初步**（方向1: 推导式）。
   - 描述：支持列表推导`[x*2 for x in lst]`。益处：快速生成音频样本列表。
   - 优先级：中。
   - 涉及文件：`src/vm/compiler.c` (新语法规则), `src/vm/scanner.c` (for/in支持)。
   - 预计时间：3人日。
   - 依赖：阶段1（for循环）。

4. **字符串插值**（方向1: 字符串插值）。
   - 描述：支持f-strings。益处：动态音频路径生成。
   - 优先级：低。
   - 涉及文件：`src/vm/scanner.c` (f"..."语法), `src/vm/compiler.c` (表达式插值)。
   - 预计时间：2人日。
   - 依赖：阶段1。

---

### **阶段4: 动画系统与其他增强 (Animation System & Polish)**
**目标**：实现方向2的阶段三，并完成剩余高优先级语言特性。最终优化。
**预计时长**：12-18人日。
1. **属性关键帧**（方向2: Property Keyframes）。
   - 描述：API `animate(clip, prop, from, to, start, duration)`。
   - 优先级：高。
   - 涉及文件：`src/engine/timeline.h` (KeyframeTrack结构), `src/engine/timeline.c` (插入/查找), `src/engine/compositor.c` (渲染时计算值)。
   - 预计时间：5人日。
   - 依赖：阶段3。

2. **动画缓动函数**（扩展）。
   - 描述：支持lambda作为缓动函数。
   - 优先级：中。
   - 涉及文件：`src/engine/timeline.c` (get_property_value调用lambda)。
   - 预计时间：2人日。
   - 依赖：本阶段关键帧 + 阶段1 Lambda。

3. **模块导入**（方向1: 导入语句）。
   - 描述：支持`import mod`。益处：分离动画脚本。
   - 优先级：高。
   - 涉及文件：`src/vm/compiler.c` (import解析), `src/vm/vm.c` (模块加载执行)。
   - 预计时间：4人日。
   - 依赖：阶段1。

4. **内置方法扩展**（方向1: 内置方法）。
   - 描述：为列表/字符串添加`sort`、`split`等。
   - 优先级：中。
   - 涉及文件：`src/binding/bind_std.c` (新native函数), `src/vm/object.c` (方法绑定)。
   - 预计时间：2人日。
   - 依赖：阶段3推导式。

5. **最终优化与测试**（通用）。
   - 描述：性能调优、文档更新。
   - 优先级：高。
   - 涉及文件：所有（重点`src/vm/memory.c` GC）。
   - 预计时间：3人日。
   - 依赖：所有阶段。

---

**总结与调整建议**：
- **总预计时长**：42-62人日（约2-3个月）。
- **里程碑**：阶段1后发布v0.1（语言稳定）；阶段4后发布v1.0（完整功能）。
- **潜在风险**：动画系统的热路径性能瓶颈——在阶段4前基准测试渲染循环。
- **灵活性**：如果资源有限，优先高优先级任务。方向1的低优先级（如枚举）可推迟到维护阶段。
- **下一步**：从阶段0开始，设置Git仓库，并编写一个简单测试脚本验证每个阶段。

如果需要更详细的任务分解、代码片段或调整优先级，请提供反馈！


这是一个非常好的问题。你的代码架构分层非常清晰（Scanner -> Compiler -> Bytecode -> VM -> Memory/Object），这意味着修改通常会遵循特定的模式。

以下针对**“添加列表切片功能”**和**“加入一个新的内置定义类（比如 Color 类）”**这两个具体场景，详细列举需要修改的文件和位置。

---

### 场景一：为列表加入切片功能 (List Slicing)
**目标语法**：`list[start:end]`

这涉及到**语法解析**（识别冒号）和**运行时执行**（创建新列表）。

#### 1. 编译器前端 (Compiler)
*   **文件**: `src/core/compiler/compiler_expr.c`
*   **任务**: 修改下标访问的解析逻辑。
    *   目前你的代码中 `TOKEN_LEFT_BRACKET` (`[`) 在 `rules` 中只有 `listLiteral` (前缀)。你需要添加 `infix` 处理函数来实现索引或切片。
    *   **修改点**: 在 `rules` 数组中，为 `TOKEN_LEFT_BRACKET` 添加 `infix` 函数（例如命名为 `subscript`）。
    *   **代码逻辑**:
        ```c
        static void subscript(bool canAssign) {
            // 此时栈顶是 list 变量
            expression(); // 解析 start 索引
            
            if (match(TOKEN_COLON)) { // 这是一个切片！
                expression(); // 解析 end 索引
                consume(TOKEN_RIGHT_BRACKET, "Expect ']' after slice.");
                emitByte(OP_SLICE); // 发射切片指令
            } else { // 这是普通索引
                consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
                if (canAssign && match(TOKEN_EQUAL)) {
                    expression();
                    emitByte(OP_SET_SUBSCRIPT); // 你可能还需要实现这个
                } else {
                    emitByte(OP_GET_SUBSCRIPT); // 你可能还需要实现这个
                }
            }
        }
        ```

#### 2. 字节码定义 (Chunk)
*   **文件**: `src/core/chunk.h`
*   **任务**: 定义新的操作码。
    *   在 `OpCode` 枚举中添加 `OP_SLICE`。如果还没有普通索引，可能还需要 `OP_GET_SUBSCRIPT`。

#### 3. 虚拟机执行 (VM Handler)
*   **文件**: `src/core/vm/vm_handler.h`
*   **任务**: 实现指令逻辑。
    *   **修改点**: 添加 `op_slice` 函数。
    *   **逻辑**:
        1.  弹出 `end` (Stack Top)。
        2.  弹出 `start` (Stack Top - 1)。
        3.  弹出 `list` (Stack Top - 2)。
        4.  验证类型（必须是数字和列表）。
        5.  处理负数索引（Python 风格）或越界检查。
        6.  调用 `object.c` 中的辅助函数创建新列表。
        7.  `PUSH` 新列表。
*   **文件**: `src/core/vm/vm.c`
*   **任务**: 在 `switch` 或跳转表中注册 `OP_SLICE`。

#### 4. 对象操作 (Object)
*   **文件**: `src/core/object.h` / `src/core/object.c`
*   **任务**: 实现实际的内存拷贝逻辑。
    *   **新增函数**: `ObjList* copyListRange(VM* vm, ObjList* list, int start, int end);`
    *   **注意**: 必须使用 `ALLOCATE` 宏来确保 GC 能够追踪内存。

---

### 场景二：新加入一个内置类 (例如 `Color`)
**目标**: 能够使用 `Color(255, 0, 0)`，并且对象类型为 `OBJ_COLOR`。

这涉及到**类型系统**、**内存管理**和**原生函数绑定**。

#### 1. 类型定义 (Type Definition)
*   **文件**: `src/core/object.h`
*   **任务**: 定义数据结构。
    *   在 `ObjType` 枚举中添加 `OBJ_COLOR`。
    *   定义结构体：
        ```c
        typedef struct {
            Obj obj; // 头部必须是 Obj
            u8 r, g, b, a;
        } ObjColor;
        ```
    *   添加宏：`IS_COLOR`, `AS_COLOR`。
*   **文件**: `src/core/value.h`
*   **任务**: 如果使用了 NaN Boxing 且想做特定优化，可能需要在这里调整，但通常只需依赖 `OBJ_VAL` 即可。

#### 2. 内存管理 (Memory Management)
这是最容易被忽略但最导致 Crash 的地方。
*   **文件**: `src/core/memory.c`
*   **任务**: 告诉 GC 如何处理这个新对象。
    *   **函数 `freeBody` (或 `freeObject`)**: 添加 `case OBJ_COLOR:`，执行 `FREE(vm, ObjColor, object)`。
    *   **函数 `blackenObject`**: 如果 `Color` 里面包含其他对象引用（比如 `ObjString* name`），你需要在这里 `markObject`。如果是纯数字（r,g,b），则不需要修改此函数。

#### 3. 对象构造与打印 (Object Implementation)
*   **文件**: `src/core/object.c`
*   **任务**:
    *   **新增构造函数**: `ObjColor* newColor(VM* vm, u8 r, u8 g, u8 b);`
    *   **修改 `printObject`**: 添加 `case OBJ_COLOR:`，输出 `"<Color 255, 0, 0>"`。

#### 4. 虚拟机绑定 (Native Binding)
*   **文件**: `src/core/vm/vm.c` (或单独的 `core/stdlib/std_color.c`)
*   **任务**: 将 C 代码暴露给脚本。
    *   你需要在 `initVM` 或专门的 `initNativeClasses` 中创建一个 `ObjClass`。
    *   **构造函数**: 定义一个 `NativeFn` 类型的函数 `colorInit`，在其中调用 `newColor` 并返回实例。
    *   **注册**:
        ```c
        ObjString* name = copyString(vm, "Color", 5);
        ObjClass* colorClass = newClass(vm, name);
        // ... 绑定 __init__ 方法 ...
        tableSet(vm, &vm->globals, OBJ_VAL(name), OBJ_VAL(colorClass));
        ```

---

### 汇总检查清单 (Checklist)

当你修改代码时，请按以下顺序检查，防止遗漏：

1.  **Header (`object.h`)**: 结构体定义了吗？枚举加了吗？
2.  **Memory (`memory.c`)**: `freeObject` 处理释放了吗？`blackenObject` 处理引用遍历了吗？
3.  **Alloc (`object.c`)**: 有对应的 `newXxx` 函数吗？初始化所有字段了吗（尤其是指针字段设为 NULL）？
4.  **Print (`object.c`)**: `printObject` 能打印它吗？
5.  **VM (`vm_handler.h` / `vm.c`)**:
    *   如果是语法特性：OpCode 实现正确吗？栈平衡（Push/Pop 次数）对吗？
    *   如果是新类型：原生方法绑定了吗？
6.  **Compiler (`compiler_expr.c`)**: 只有涉及新语法（如切片符号）时才需要动这里。

### 特别提醒

在你的代码中，**`src/core/memory.c`** 是最关键的。如果你加了一个新对象类型（比如 `OBJ_COLOR`），但在 `freeObject` 中忘记加 `case`，VM 销毁时会**内存泄漏**。如果你在 `Color` 对象里放了一个 `ObjString*` 但忘记在 `blackenObject` 中标记它，GC 会在运行中**回收掉正在使用的字符串**，导致难以调试的 Crash。