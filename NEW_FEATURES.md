# NTerminal 新功能实现总结

## 已完成的功能

### 1. ✅ 右键复制终端tab会话功能

**实现位置：** `mainwindow.cpp` / `mainwindow.h`

**功能说明：**
- 在终端tab上右键点击，显示上下文菜单
- 对于非串口会话，显示"复制会话"选项
- 点击后创建新的tab，使用相同的连接设置
- 串口会话不显示复制选项（按需求排除）

**实现细节：**
- 添加了 `onTabContextMenu()` 槽函数处理右键菜单
- 检测会话类型：`if (settings.type != Common::ConnectionType::Serial)`
- 复制会话时自动添加 "(Copy)" 后缀
- 新会话自动应用当前主题设置

---

### 2. ✅ Tools 主菜单

**实现位置：** `mainwindow.cpp` / `mainwindow.h`

**功能说明：**
- 在主菜单栏添加了 "Tools" 菜单
- 包含四个子菜单项：
  1. Calculator - 计算器
  2. Port Scanner - 端口扫描（待实现）
  3. Network Services - 网络服务（待实现）
  4. ASCII Table - ASCII码表

**菜单结构：**
```
File | Edit | View | Settings | Transmit | Script | Tools | Plugin | Help
                                                      |
                                                      +-- Calculator
                                                      +-- Port Scanner
                                                      +-- Network Services
                                                      +-- ASCII Table
```

---

### 3. ✅ 计算器工具

**实现位置：** `calculatordialog.cpp` / `calculatordialog.h`

**功能特点：**
- **多进制支持：** HEX (16), DEC (10), OCT (8), BIN (2)
- **实时转换显示：** 同时显示所有进制的转换结果
- **基本运算：** 加、减、乘、除
- **程序员模式：**
  - 支持16进制字母输入 (A-F)
  - 根据当前进制自动启用/禁用按钮
  - 例如：二进制模式下只能输入0和1
- **用户界面：**
  - 大号显示屏
  - 进制选择下拉框
  - 转换结果实时显示区域
  - 数字键盘和运算符按钮
  - 清除(C)、退格(←)、等于(=)功能键

**使用示例：**
1. 选择进制（默认十进制）
2. 输入数字
3. 选择运算符 (+, -, *, /)
4. 输入第二个数字
5. 按 = 查看结果
6. 所有进制的转换结果实时显示

---

### 4. ✅ ASCII码表工具

**实现位置：** `asciitabledialog.cpp` / `asciitabledialog.h`

**功能特点：**
- **完整ASCII表：** 显示0-255所有字符
- **多列显示：**
  - DEC：十进制值
  - HEX：十六进制值（0x格式）
  - Char：字符显示
  - Description：字符说明
- **控制字符支持：**
  - 0-31和127显示为控制字符名称
  - 例如：`<NUL>`, `<CR>`, `<LF>`, `<ESC>`, `<DEL>`
  - 提供完整的控制字符说明
- **搜索功能：**
  - 支持十进制搜索（输入数字）
  - 支持十六进制搜索（输入0x开头或直接输入）
  - 自动高亮匹配行
  - 自动滚动到匹配位置
- **复制功能：**
  - 点击任意行复制字符到剪贴板
  - 可打印字符复制字符本身
  - 控制字符复制其十进制值
- **用户界面：**
  - 表格显示，交替行颜色
  - 可选择整行
  - 列宽自动调整

**控制字符列表：**
- 0: NUL (Null)
- 7: BEL (Bell)
- 8: BS (Backspace)
- 9: HT (Horizontal Tab)
- 10: LF (Line Feed)
- 13: CR (Carriage Return)
- 27: ESC (Escape)
- 127: DEL (Delete)
- 等等...

---

### 5. ✅ Plugin 主菜单

**实现位置：** `mainwindow.cpp` / `mainwindow.h`

**功能说明：**
- 在主菜单栏添加了 "Plugin" 菜单
- 为将来的Python脚本支持预留接口
- 目前菜单已创建，功能待实现

---

## 待实现的功能

### 1. ⏳ 端口扫描工具

**计划功能：**
- 输入IP地址
- 扫描常用端口（21, 22, 23, 80, 443, 3389等）
- 支持自定义端口范围
- 显示开放端口列表
- Ping功能检测主机连通性
- 多线程扫描提高速度

### 2. ⏳ 网络服务工具

**计划功能：**
- **TFTP服务器：** 简单文件传输
- **FTP服务器：** 文件传输服务
- **Web服务器：** HTTP文件浏览
- **Telnet服务器：** 远程终端服务
- 每个服务可独立启动/停止
- 可配置端口和根目录
- 显示服务状态和连接信息

### 3. ⏳ Python脚本插件系统

**计划功能：**
- 集成Python解释器
- 提供终端交互API
- 脚本可以：
  - 读取终端输出
  - 发送命令到终端
  - 自动化交互流程
  - 处理和分析数据
- 脚本管理界面
- 脚本编辑器
- 脚本库

---

## 代码结构

### 新增文件

```
calculatordialog.h          - 计算器对话框头文件
calculatordialog.cpp        - 计算器对话框实现
asciitabledialog.h          - ASCII码表对话框头文件
asciitabledialog.cpp        - ASCII码表对话框实现
```

### 修改文件

```
mainwindow.h                - 添加Tools和Plugin菜单，添加槽函数
mainwindow.cpp              - 实现菜单、右键复制会话功能
```

---

## 使用说明

### 右键复制会话
1. 在终端tab上右键点击
2. 选择"复制会话"（仅非串口会话显示）
3. 新tab自动创建并连接

### 计算器
1. 点击 Tools → Calculator
2. 选择进制（HEX/DEC/OCT/BIN）
3. 输入数字和运算符
4. 查看所有进制的转换结果

### ASCII码表
1. 点击 Tools → ASCII Table
2. 浏览完整ASCII表
3. 在搜索框输入数字查找
4. 点击行复制字符到剪贴板

---

## 技术要点

### 计算器实现
- 使用 `QString::toLongLong()` 进行进制转换
- 动态启用/禁用按钮根据当前进制
- 实时更新所有进制显示

### ASCII表实现
- 使用 `QTableWidget` 显示256个字符
- `QMap` 存储控制字符名称
- 搜索支持十进制和十六进制输入
- 高亮显示使用 `setBackground()`

### 右键菜单实现
- 使用 `QTabBar::customContextMenuRequested` 信号
- 检测会话类型排除串口
- 复制连接设置创建新会话

---

## 下一步计划

1. 实现端口扫描工具
2. 实现网络服务工具
3. 集成Python解释器
4. 实现脚本API和管理界面
5. 添加更多实用工具

---

## 总结

本次更新成功实现了：
- ✅ 右键复制会话功能（排除串口）
- ✅ Tools主菜单框架
- ✅ 功能完整的计算器（16进制转换）
- ✅ 功能完整的ASCII码表
- ✅ Plugin菜单框架

这些功能增强了NTerminal的实用性，特别是计算器的16进制转换功能和ASCII码表对于网络工程师和程序员非常实用。
