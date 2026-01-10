# Luna 标准库和语言特性文档

本文档描述了 Luna 语言的核心特性、基本语法以及内置的标准库，包括 `List` 和 `Dict` 类，以及相关工具函数。这些特性基于项目代码中的虚拟机（VM）、编译器和对象系统实现，提供了一个类似于 Python 的脚本语言环境，支持缩进式语法、面向对象编程和异常处理。Luna 使用 NaN Boxing 值表示，支持数字、布尔、字符串、列表、字典、函数和类等类型。语言设计强调类型安全（如同质列表）和高效内存管理（通过垃圾回收）。

## 1. Luna 语言概述

Luna 是一种动态类型脚本语言，适用于脚本编写、数据处理和简单应用开发。它支持 Python-like 的缩进语法（使用缩进和换行分隔代码块），并内置了垃圾回收机制。核心特性包括：

- **值类型**：数字（Number）、布尔（Bool）、空值（nil）、字符串（String）。
- **对象类型**：列表（List，同质）、字典（Dict）、函数（Function，包括闭包和本地函数）、类（Class，支持继承）、实例（Instance）。
- **控制流**：条件判断（if/else）、循环（while/for/continue/break）、异常处理（try/except）。
- **函数和 Lambda**：支持命名函数（fun）、匿名函数（lam）和关键字参数。
- **类和 OOP**：支持类定义（class）、继承（< superclass）、方法（包括 init 初始化）和 super 调用。
- **其他**：打印（print）、返回（return）、变量声明（var）、逻辑运算（and/or）。

Luna 的编译器将源代码转换为字节码，由 VM 执行。语言使用开放寻址哈希表实现字符串驻留和字典，支持 NaN Boxing 优化值表示以提高性能。

### 基本语法规则

- **缩进敏感**：使用空格或制表符缩进定义代码块（类似 Python）。缩进增加表示进入块（INDENT），减少表示退出（DEDENT）。
- **语句结束**：大多数语句以换行（NEWLINE）结束；分号（;）可选但不推荐。
- **注释**：以 `#` 开头，到行尾。
- **字面量**：
  - 数字：`123` 或 `3.14`。
  - 字符串：`"hello"`（支持转义）。
  - 布尔：`true` / `false`。
  - 空值：`nil`。
  - 列表：`[1, 2, 3]`。
  - 字典：`{"key": "value"}`。

 **运算符**：支持 `+ - * / == != > >= < <= ! -` 等。
- **变量**：使用 `var` 声明，如 `var x = 10`。
- **函数定义**：
  ```
  fun add(a, b=0):  # 默认参数 b=0
      return a + b
  ```
- **Lambda**：`lam a, b: a + b`（简短匿名函数）。
- **类定义**：
  ```
  class MyClass < BaseClass:
      fun init():
          this.value = 0
      
      fun method():
          return this.value
  ```
- **循环**：
  ```
  while condition:
      print "looping"
  
  # 遍历列表
  for item in list:
      print item

  # 遍历数字范围
  for i in range(5):
      print i
  ```

- **列表推导式**：`[x * 2 for x in list]`（快速构建列表）。


- **异常**：
  ```
  try:
      # code
  except:
      print "error"
  ```
- **类型安全**：列表元素必须同类型；尝试混合类型会抛出运行时错误。

### 运算符重载

Luna 允许类通过定义特定名称的方法（魔术方法）来重载标准运算符。这使得自定义对象（如向量、时间对象）可以像内置类型一样参与运算。当操作数是对象实例时，VM 会自动调用相应的方法。

*   **算术运算符**:
    *   `+` (加法): 对应方法 `__add(other)`
    *   `-` (减法): 对应方法 `__sub(other)`
    *   `*` (乘法): 对应方法 `__mul(other)`
    *   `/` (除法): 对应方法 `__div(other)`
    *   `-` (一元负号): 对应方法 `__neg()` (例如 `-x`)
*   **比较运算符**:
    *   `<` (小于): 对应方法 `__lt(other)`
    *   `>` (大于): 对应方法 `__gt(other)`
    *   `<=` (小于等于): 对应方法 `__le(other)`
    *   `>=` (大于等于): 对应方法 `__ge(other)`

### 基本使用示例

以下是一个简单脚本，展示变量、函数、类和控制流：

```
var greeting = "Hello, Luna!"

fun sayHello(name):
    print greeting + " " + name

class Person:
    fun init(name):
        this.name = name
    
    fun greet():
        sayHello(this.name)

var p = Person("User")
p.greet()  # 输出: Hello, Luna! User

var numbers = [1, 2, 3]
for n in numbers:
    if n > 1:
        print n
    else:
        continue

try:
    var x = 1 / 0
except:
    print "Division error"
```

## 2. List 类 (列表)

`List` 类用于创建和操作动态数组，支持元素追加、弹出、访问和修改。Luna 的列表是同质的（所有元素必须相同类型），以确保类型安全。

### 构造函数

#### `List()`

创建一个空的列表实例。

*   **参数**: 无。
*   **返回值**: `List` 实例。
*   **注意**: 新列表初始为空（长度为 0）。

### 属性 (只读/同步)

*   无内置属性暴露，但可以通过 `len(list)` 获取长度。

### 方法

#### `push(list, item)`

向列表末尾追加一个元素。

*   **参数**:
    *   `list`: `List` 实例。
    *   `item`: 要追加的元素（必须与列表中现有元素类型匹配）。
*   **返回值**: 无（返回 nil）。
*   **注意**: 如果列表非空且 `item` 类型不匹配，会抛出运行时错误（"List is homogeneous. Cannot mix types."）。
*   **示例**: `push(myList, 42)` // 追加数字 42。

#### `pop(list)`

从列表末尾弹出并返回最后一个元素。

*   **参数**:
    *   `list`: `List` 实例。
*   **返回值**: 弹出的元素（如果列表为空，返回 nil）。
*   **示例**: `var item = pop(myList)`

#### `get(list, index)`

获取列表指定索引处的元素。

*   **参数**:
    *   `list`: `List` 实例。
    *   `index`: 索引（数字，从 0 开始）。
*   **返回值**: 指定索引处的元素（如果索引越界，返回 nil 并打印错误）。
*   **注意**: 索引必须是整数，且在 0 到列表长度-1 范围内；否则抛出运行时错误（"List index out of bounds."）。
*   **示例**: `var value = get(myList, 0)`

#### `set(list, index, value)`

设置列表指定索引处的元素。

*   **参数**:
    *   `list`: `List` 实例。
    *   `index`: 索引（数字，从 0 开始）。
    *   `value`: 新值（必须与列表中现有元素类型匹配）。
*   **返回值**: 无（返回 nil）。
*   **注意**: 如果索引越界或类型不匹配，会抛出运行时错误（"Type mismatch in homogeneous list." 或返回 nil）。
*   **示例**: `set(myList, 0, 100)`

#### `clear(list)`

清空列表。

*   **参数**:
    *   `list`: `List` 实例。
*   **返回值**: 无（返回 nil）。
*   **示例**: `clear(myList)`

### 列表推导式 (List Comprehension)
Luna 支持使用简洁的语法从一个可迭代对象（如 List、String 或 range）快速创建新的列表。

*   **语法**: `[ expression for variable in iterable ]`
*   **说明**: 
    1.  遍历 `iterable` 中的每个元素。
    2.  将元素赋值给 `variable`。
    3.  计算 `expression` 的结果。
    4.  将结果自动追加到新列表中。
*   **示例**:
    ```python
    # 传统写法
    var list = []
    for i in range(5):
        push(list, i * 2)
    
    # 推导式写法 (等价且更高效)
    var list = [i * 2 for i in range(5)]
    ```
*   **注意**: 推导式内部的变量作用域是隔离的，不会污染外部同名变量。

---



## 3. Dict 类 (字典)

`Dict` 类用于创建和操作键值对映射，支持任意键（但通常为字符串或数字）和值。

### 构造函数

#### `Dict()`

创建一个空的字典实例。

*   **参数**: 无。
*   **返回值**: `Dict` 实例。
*   **注意**: 新字典初始为空（长度为 0）。

### 属性 (只读/同步)

*   无内置属性暴露，但可以通过 `len(dict)` 获取键值对数量。

### 方法

#### `dict_put(dict, key, value)`

向字典中插入或更新一个键值对。

*   **参数**:
    *   `dict`: `Dict` 实例。
    *   `key`: 键（任意类型，通常为字符串或数字）。
    *   `value`: 值（任意类型）。
*   **返回值**: 无（返回 nil）。
*   **示例**: `dict_put(myDict, "name", "Luna")`

#### `dict_get(dict, key)`

获取字典中指定键的值。

*   **参数**:
    *   `dict`: `Dict` 实例。
    *   `key`: 键。
*   **返回值**: 对应的值（如果键不存在，返回 nil）。
*   **示例**: `var value = dict_get(myDict, "name")`

#### `dict_remove(dict, key)`

移除字典中指定键的值，并返回被移除的值。

*   **参数**:
    *   `dict`: `Dict` 实例。
    *   `key`: 键。
*   **返回值**: 被移除的值（如果键不存在，返回 nil）。
*   **示例**: `var removed = dict_remove(myDict, "name")`

#### `dict_has(dict, key)`

检查字典中是否包含指定键。

*   **参数**:
    *   `dict`: `Dict` 实例。
    *   `key`: 键。
*   **返回值**: 布尔值（true/false）。
*   **示例**: `if dict_has(myDict, "name"): print "Exists"`

#### `dict_keys(dict)`

获取字典的所有键作为列表。

*   **参数**:
    *   `dict`: `Dict` 实例。
*   **返回值**: 包含所有键的 `List` 实例。
*   **示例**: `var keys = dict_keys(myDict)`

#### `dict_values(dict)`

获取字典的所有值作为列表。

*   **参数**:
    *   `dict`: `Dict` 实例。
*   **返回值**: 包含所有值的 `List` 实例。
*   **示例**: `var values = dict_values(myDict)`

#### `clear(dict)`

清空字典。

*   **参数**:
    *   `dict`: `Dict` 实例。
*   **返回值**: 无（返回 nil）。
*   **示例**: `clear(myDict)`

## 4. 工具函数

#### `len(obj)`

获取列表或字典的长度。

*   **参数**:
    *   `obj`: `List` 或 `Dict` 实例。
*   **返回值**: 长度（数字）。
*   **示例**: `var size = len(myList)`

#### `range(start, stop, step)`

生成一个包含算术级数数字的列表。支持 1 至 3 个参数。

*   **参数**:
    *   `start`: （可选）起始数值（包含）。如果只传入一个参数，该参数被视为 `stop`，且 `start` 默认为 0。
    *   `stop`: 结束数值（不包含）。
    *   `step`: （可选）步长。默认为 1。不能为 0。
*   **返回值**: 一个包含生成数字的 `List`。
*   **示例**:
    *   `range(5)` 返回 `[0, 1, 2, 3, 4]`
    *   `range(2, 5)` 返回 `[2, 3, 4]`
    *   `range(0, 10, 2)` 返回 `[0, 2, 4, 6, 8]`

## 5. 完整示例代码

```python

# 1. 创建列表并操作
var myList = List()
push(myList, 1)
push(myList, 2)
push(myList, 3)

print "列表长度: "
print len(myList)  # 输出 3

# 访问和修改
print get(myList, 0)  # 输出 1
set(myList, 0, 10)
print get(myList, 0)  # 输出 10

# 弹出和清空
var popped = pop(myList)
print popped  # 输出 3
clear(myList)
print len(myList)  # 输出 0

# 2. 创建字典并操作
var myDict = Dict()
dict_put(myDict, "name", "Luna")
dict_put(myDict, "age", 42)
dict_put(myDict, "active", true)

print "字典长度: "
print len(myDict)  # 输出 3

# 检查和获取
if dict_has(myDict, "name"):
    print dict_get(myDict, "name")  # 输出 Luna

# 移除
var removed = dict_remove(myDict, "age")
print removed  # 输出 42
print len(myDict)  # 输出 2

# 获取键和值
var keys = dict_keys(myDict)
print keys  # 输出 [ "name", "active" ] (顺序不保证)

var values = dict_values(myDict)
print values  # 输出 [ "Luna", true ] (顺序不保证)

# 清空
clear(myDict)
print len(myDict)  # 输出 0

# 3. 使用语言特性：函数和异常
fun divide(a, b):
    try:
        return a / b
    except:
        print "Division error"
        return nil

print divide(10, 2)  # 输出 5
print divide(10, 0)  # 输出 Division error 和 nil

# Lambda 示例
var add = lam a, b: a + b
print add(3, 4)  # 输出 7

# Range 与 循环控制流示例
# 打印 0 到 9 中的奇数
for i in range(10):
    if i % 2 == 0:
        continue  # 跳过偶数
    print i

# 4. 运算符重载示例
class Vector:
    fun init(x, y):
        this.x = x
        this.y = y
    
    # 重载加法 (+)
    fun __add(other):
        return Vector(this.x + other.x, this.y + other.y)
    
    # 重载小于 (<) 用于比较
    fun __lt(other):
        return (this.x + this.y) < (other.x + other.y)
        
    fun toString():
        return "(" + this.x + ", " + this.y + ")"

var v1 = Vector(2, 3)
var v2 = Vector(4, 5)

var v3 = v1 + v2     # 自动调用 v1.__add(v2)
print v3.x           # 输出 6
print v3.y           # 输出 8

if v1 < v2:          # 自动调用 v1.__lt(v2)
    print "v1 is smaller"

# 列表推导式示例
var squares = [x * x for x in range(10)]
print squares  # 输出 [0, 1, 4, 9, ..., 81]

# 结合字符串
var codes = [c for c in "ABC"]
print codes    # 输出 ["A", "B", "C"]
```