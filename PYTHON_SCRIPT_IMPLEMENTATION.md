# Python 脚本引擎实现总结

## 完成的功能

已成功实现 Python 脚本引擎，允许用户编写 Python 脚本来监控终端输出、自动化交互和处理数据。

## 实现的组件

### 1. PythonScriptEngine 类 (`pythonscriptengine.h/cpp`)

核心脚本引擎类，负责：
- 启动和管理 Python 进程
- 通过 JSON 进行进程间通信
- 处理脚本的输入输出
- 自动查找 Python 可执行文件

**主要方法：**
- `loadScript(scriptPath)` - 加载并运行脚本
- `stopScript()` - 停止脚本
- `isRunning()` - 检查脚本是否运行中
- `sendLine(line)` - 发送终端输出行到脚本
- `sendEvent(eventType, data)` - 发送事件到脚本

**信号：**
- `sendCommand(command)` - 脚本请求发送命令
- `logMessage(level, message)` - 脚本日志消息
- `scriptStarted()` - 脚本启动
- `scriptStopped(exitCode)` - 脚本停止
- `scriptError(error)` - 脚本错误

### 2. Python API 模块 (`scripts/nterminal.py`)

Python 端的 API 库，提供：
- `Terminal` 类 - 主要接口类
- 装饰器风格的回调注册
- 正则表达式匹配支持
- `expect()` 功能（类似 expect 脚本）
- 日志记录功能

**主要 API：**
```python
@terminal.on_line          # 处理每一行
@terminal.on_pattern(r"")  # 正则匹配
@terminal.on_connected     # 连接事件
@terminal.on_disconnected  # 断开事件
terminal.send(data)        # 发送命令
terminal.expect(pattern)   # 等待模式
terminal.log(level, msg)   # 记录日志
terminal.exit()            # 退出脚本
terminal.run()             # 运行主循环
```

### 3. 示例脚本

提供了 4 个示例脚本：

1. **simple_example.py** - 简单示例
   - 演示基本的行处理
   - IP 地址检测
   - 自动按键

2. **auto_login.py** - 自动登录
   - 检测登录提示
   - 自动输入用户名和密码
   - 状态机管理

3. **log_monitor.py** - 日志监控
   - 监控错误和警告
   - 统计信息
   - 写入日志文件

4. **test_automation.py** - 自动化测试
   - 运行测试用例
   - 验证输出
   - 生成测试报告

### 4. 集成到 TerminalWidget

在 `TerminalWidget` 中集成了脚本引擎：
- 添加了 `m_scriptEngine` 成员
- 实现了脚本管理方法
- 连接了所有信号
- 自动将终端输出发送到脚本

**新增方法：**
- `loadScript(scriptPath)` - 加载脚本
- `stopScript()` - 停止脚本
- `isScriptRunning()` - 检查状态
- `currentScript()` - 获取当前脚本路径

**新增信号：**
- `scriptStarted()` - 脚本启动
- `scriptStopped(exitCode)` - 脚本停止
- `scriptError(error)` - 脚本错误
- `scriptLogMessage(level, message)` - 脚本日志

## 通信协议

使用 JSON 格式通过标准输入/输出通信：

### 终端 → 脚本

```json
{
  "type": "line",
  "data": "login: ",
  "timestamp": 1234567890
}

{
  "type": "connected",
  "host": "192.168.1.1",
  "port": 22
}

{
  "type": "disconnected"
}
```

### 脚本 → 终端

```json
{
  "type": "send",
  "data": "username\n"
}

{
  "type": "log",
  "level": "info",
  "message": "Detected login prompt"
}

{
  "type": "exit"
}
```

## 使用方法

### 1. 编写脚本

```python
#!/usr/bin/env python3
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
from nterminal import Terminal

terminal = Terminal()

@terminal.on_line
def handle_line(line):
    if "login:" in line:
        terminal.send("admin\n")

terminal.run()
```

### 2. 在 NTerminal 中加载

```cpp
// 在 TerminalWidget 中
terminalWidget->loadScript("/path/to/script.py");

// 检查状态
if (terminalWidget->isScriptRunning()) {
    qDebug() << "Script is running:" << terminalWidget->currentScript();
}

// 停止脚本
terminalWidget->stopScript();
```

### 3. 监听脚本事件

```cpp
connect(terminalWidget, &TerminalWidget::scriptStarted,
        this, []() {
            qDebug() << "Script started";
        });

connect(terminalWidget, &TerminalWidget::scriptLogMessage,
        this, [](const QString &level, const QString &message) {
            qDebug() << "[Script]" << level << ":" << message;
        });

connect(terminalWidget, &TerminalWidget::scriptError,
        this, [](const QString &error) {
            qDebug() << "Script error:" << error;
        });
```

## 特性

### ✅ 已实现

1. **数据捕获**
   - 捕获终端的每一行输出
   - 实时发送到 Python 脚本
   - 支持按行处理

2. **模式匹配**
   - 正则表达式匹配
   - 多模式匹配（expect_list）
   - 超时支持

3. **自动化交互**
   - 发送命令到终端
   - 状态机支持
   - 事件驱动

4. **日志记录**
   - 多级别日志（debug, info, warning, error）
   - 日志转发到终端
   - 文件日志支持

5. **进程管理**
   - 自动查找 Python 可执行文件
   - 进程生命周期管理
   - 错误处理

6. **安全性**
   - 进程隔离
   - JSON 消息验证
   - 缓冲区限制

### 🔄 可扩展功能

1. **脚本管理界面**
   - 脚本列表
   - 一键启动/停止
   - 脚本编辑器

2. **调试功能**
   - 断点支持
   - 变量查看
   - 单步执行

3. **脚本库**
   - 内置脚本模板
   - 社区脚本分享
   - 一键导入

4. **性能优化**
   - 批量消息处理
   - 异步 I/O
   - 消息队列

## 文档

提供了完整的文档：

1. **PYTHON_SCRIPT_ENGINE.md** - 架构设计文档
2. **scripts/README.md** - 使用指南和 API 参考
3. **示例脚本** - 4 个实用示例

## 测试建议

### 1. 基本功能测试

```bash
# 测试简单示例
python scripts/simple_example.py

# 测试自动登录
python scripts/auto_login.py

# 测试日志监控
python scripts/log_monitor.py
```

### 2. 集成测试

1. 在 NTerminal 中连接到服务器
2. 加载 `auto_login.py` 脚本
3. 观察自动登录过程
4. 检查日志输出

### 3. 错误处理测试

1. 加载不存在的脚本
2. 加载有语法错误的脚本
3. 脚本运行时断开连接
4. 脚本运行时强制停止

## 已知限制

1. **Python 版本**
   - 需要 Python 3.x
   - 不支持 Python 2.x

2. **性能**
   - 每行数据都会发送到脚本
   - 大量数据时可能有延迟

3. **平台**
   - Windows 需要正确安装 Python
   - 需要在 PATH 中或标准位置

## 后续改进

1. **UI 集成**
   - 添加脚本管理菜单
   - 脚本状态指示器
   - 日志查看器

2. **性能优化**
   - 批量消息处理
   - 消息过滤
   - 异步处理

3. **功能增强**
   - 支持多个脚本同时运行
   - 脚本间通信
   - 热重载

4. **调试工具**
   - 脚本调试器
   - 性能分析
   - 错误追踪

## 总结

Python 脚本引擎已完全实现，提供了：
- ✅ 完整的 Python API
- ✅ 进程间通信机制
- ✅ 示例脚本和文档
- ✅ 集成到 TerminalWidget
- ✅ 错误处理和日志记录

用户现在可以编写 Python 脚本来：
- 监控终端输出的每一行数据
- 根据匹配的内容自动处理
- 实现终端自动化交互
- 记录日志和数据采集
- 自动化测试和运维任务
