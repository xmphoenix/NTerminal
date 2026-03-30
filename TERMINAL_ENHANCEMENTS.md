# 终端模拟器功能增强

## 概述

根据 `TERMINAL_PARSER_COMPARISON.md` 中的对比分析，我们补足了当前终端模拟器相对于 PuTTY 的功能差距。

## 已实现的功能

### 1. 字符编辑命令 (ICH/DCH/ECH)

#### 实现的命令：
- **ICH (Insert Character)** - `ESC [ @ ` 或 `ESC [ n @`
  - 在光标位置插入 n 个空白字符
  - 将光标右侧的字符向右移动

- **DCH (Delete Character)** - `ESC [ P` 或 `ESC [ n P`
  - 删除光标位置的 n 个字符
  - 将光标右侧的字符向左移动

- **ECH (Erase Character)** - `ESC [ X` 或 `ESC [ n X`
  - 擦除光标位置的 n 个字符（用空格替换）
  - 不移动其他字符

#### 实现位置：
- `TerminalParser`: 已在 `processCsiCommand()` 中解析
- `TerminalWidget`: 新增 `onInsertChars()`, `onDeleteChars()`, `onEraseChars()`
- `TerminalProxy`: 新增对应的槽函数实现

### 2. 光标定位命令 (CNL/CPL/CHA/VPA)

#### 实现的命令：
- **CNL (Cursor Next Line)** - `ESC [ E` 或 `ESC [ n E`
  - 移动到下 n 行的开头

- **CPL (Cursor Previous Line)** - `ESC [ F` 或 `ESC [ n F`
  - 移动到上 n 行的开头

- **CHA (Cursor Horizontal Absolute)** - `ESC [ G` 或 `ESC [ n G`
  - 移动到当前行的第 n 列

- **VPA (Vertical Position Absolute)** - `ESC [ d` 或 `ESC [ n d`
  - 移动到第 n 行（保持列不变）

#### 实现位置：
- `TerminalParser`: 已在 `processCsiCommand()` 中解析
- `TerminalWidget`: 新增对应的槽函数
- `TerminalProxy`: 新增对应的槽函数实现

### 3. 制表符处理

#### 实现的功能：
- **HTS (Horizontal Tab Set)** - `ESC H`
  - 在当前光标位置设置制表位

- **TBC (Tab Clear)** - `ESC [ g` 或 `ESC [ n g`
  - `n=0`: 清除当前列的制表位
  - `n=3`: 清除所有制表位

- **CHT (Cursor Forward Tab)** - `ESC [ I` 或 `ESC [ n I`
  - 前进到下 n 个制表位

- **CBT (Cursor Backward Tab)** - `ESC [ Z` 或 `ESC [ n Z`
  - 后退到上 n 个制表位

#### 实现细节：
- 默认制表位宽度：8 列
- 使用 `QVector<bool> m_tabStops` 管理制表位
- 在构造函数中初始化默认制表位（每 8 列一个）

#### 实现位置：
- `TerminalParser`: 已在 `processCsiCommand()` 和 `processEscapeCommand()` 中解析
- `TerminalWidget`: 新增制表位管理和相关槽函数
- `TerminalProxy`: 新增制表位管理和相关槽函数

### 4. DEC 图形字符集支持

#### 实现的功能：
- 支持 DEC 特殊图形字符集（用于绘制表格边框）
- 字符集切换命令：
  - `ESC ( B`: 设置 G0 为 ASCII
  - `ESC ( 0`: 设置 G0 为 DEC 图形字符集
  - `ESC ) B`: 设置 G1 为 ASCII
  - `ESC ) 0`: 设置 G1 为 DEC 图形字符集

#### DEC 图形字符映射：
```
j → ┘  (右下角)
k → ┐  (右上角)
l → ┌  (左上角)
m → └  (左下角)
n → ┼  (十字)
q → ─  (水平线)
t → ├  (左T)
u → ┤  (右T)
v → ┴  (下T)
w → ┬  (上T)
x → │  (垂直线)
a → ▒  (方块)
` → ◆  (菱形)
~ → ·  (中点)
```

#### 实现位置：
- `TerminalParser`: 已在 `escapeState()` 和 `processCharsetCommand()` 中解析
- `TerminalWidget`: 在 `writeChar()` 中添加字符集转换
- `TerminalProxy`: 在 `onParserTextReceived()` 中添加字符集转换

### 5. OSC 52 剪贴板集成

#### 实现的功能：
- **剪贴板读取** - `ESC ] 52 ; c ; ? BEL`
  - 读取系统剪贴板内容
  - 返回 base64 编码的内容：`ESC ] 52 ; c ; <base64> BEL`

- **剪贴板写入** - `ESC ] 52 ; c ; <base64> BEL`
  - 将 base64 编码的内容写入系统剪贴板
  - 自动解码并更新剪贴板

#### 实现位置：
- `TerminalParser`: 已在 `processOscCommand()` 中解析
- `TerminalWidget`: 新增 `onClipboardRead()` 和 `onClipboardWrite()`
- `TerminalProxy`: 新增对应的槽函数实现

## 代码修改总结

### 修改的文件：

1. **terminalparser.h**
   - 已有所有必要的信号声明

2. **terminalparser.cpp**
   - 已实现所有命令的解析逻辑

3. **terminalwidget.h**
   - 新增槽函数声明
   - 新增制表位和字符集相关成员变量

4. **terminalwidget.cpp**
   - 实现所有新增的槽函数
   - 在构造函数中初始化制表位
   - 在 `writeChar()` 中添加 DEC 图形字符集支持

5. **terminalproxy.h**
   - 新增槽函数声明
   - 新增制表位和字符集相关成员变量

6. **terminalproxy.cpp**
   - 在构造函数中连接所有新信号
   - 在构造函数中初始化制表位和字符集变量
   - 实现所有新增的槽函数
   - 在 `onParserTextReceived()` 中添加 DEC 图形字符集支持

## 功能覆盖率

根据 `TERMINAL_PARSER_COMPARISON.md` 的优先级建议：

### 高优先级 ✅ (已完成)
1. ✅ 备用屏幕缓冲区检测 - 已在日志记录中实现
2. ✅ ICH/DCH/ECH 命令 - 本次实现
3. ✅ 制表符处理 - 本次实现
4. ✅ 字符集切换基础支持 - 本次实现

### 中优先级 ✅ (已完成)
5. ✅ OSC 52 剪贴板集成 - 本次实现
6. ⚠️ 鼠标报告模式 - 部分实现（TerminalProxy 中已有基础支持）
7. ⚠️ 光标显示/隐藏 - 需要在 TerminalWidget 中实现
8. ✅ DEC 图形字符集 - 本次实现

### 低优先级 (未实现)
9. ❌ DCS 支持 - 状态机已定义，但未实现具体功能
10. ❌ 性能优化 - 未实现批量处理
11. ❌ 输入验证和超时 - 部分实现（有长度限制）

## 测试建议

### 1. 字符编辑命令测试
```bash
# 测试 ICH
echo -e "Hello\e[3@World"  # 应该在 "Hel" 后插入 3 个空格

# 测试 DCH
echo -e "Hello\e[2PWorld"  # 应该删除 "He" 后的 2 个字符

# 测试 ECH
echo -e "Hello\e[3XWorld"  # 应该擦除 "Hel" 后的 3 个字符
```

### 2. 光标定位测试
```bash
# 测试 CNL
echo -e "Line1\e[2ELine3"  # 应该跳到下 2 行的开头

# 测试 CHA
echo -e "Hello\e[10GWorld"  # 应该移动到第 10 列
```

### 3. 制表符测试
```bash
# 测试制表符
echo -e "Col1\tCol2\tCol3"  # 应该按制表位对齐

# 测试 CHT
echo -e "Start\e[2IEnd"  # 应该前进 2 个制表位
```

### 4. DEC 图形字符集测试
```bash
# 测试表格绘制
echo -e "\e(0lqqqk\e(B"  # 应该显示 ┌───┐
echo -e "\e(0x   x\e(B"  # 应该显示 │   │
echo -e "\e(0mqqqqj\e(B"  # 应该显示 └────┘
```

### 5. 剪贴板测试
```bash
# 测试剪贴板写入
echo -e "\e]52;c;$(echo -n "Hello" | base64)\a"

# 测试剪贴板读取
echo -e "\e]52;c;?\a"
```

## 兼容性说明

- 所有实现都遵循 VT100/VT220/xterm 标准
- DEC 图形字符集使用 Unicode 字符映射
- 剪贴板操作使用 Qt 的 QClipboard API
- 制表位管理与标准终端行为一致

## 后续改进建议

1. **性能优化**
   - 实现批量字符处理
   - 优化屏幕更新机制
   - 添加脏区域跟踪

2. **完善鼠标支持**
   - 实现完整的鼠标报告模式
   - 支持鼠标滚轮事件
   - 支持鼠标拖拽选择

3. **增强 DCS 支持**
   - 实现 SIXEL 图形显示
   - 支持用户定义键

4. **安全性增强**
   - 添加输入超时机制
   - 增强输入验证
   - 防止恶意序列攻击
