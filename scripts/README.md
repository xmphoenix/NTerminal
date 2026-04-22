# Python 脚本使用指南

## 概述

NTerminal 支持使用 Python 脚本来监控终端输出、自动化交互和处理数据。

## 快速开始

### 1. 安装 Python

确保系统已安装 Python 3.x：

```bash
python --version
# 或
python3 --version
```

### 2. 编写脚本

创建一个 Python 脚本文件（例如 `my_script.py`）：

```python
#!/usr/bin/env python3
import sys
import os

# 导入 nterminal 模块
sys.path.insert(0, os.path.dirname(__file__))
from nterminal import Terminal

terminal = Terminal()

@terminal.on_line
def handle_line(line):
    """处理每一行输出"""
    print(f"Received: {line}")

    # 检测特定内容并响应
    if "login:" in line:
        terminal.send("admin\n")

terminal.run()
```

### 3. 在 NTerminal 中加载脚本

在 NTerminal 中：
1. 连接到服务器
2. 加载 Python 脚本
3. 脚本将自动开始监控终端输出

## API 参考

### Terminal 类

#### 方法

##### `on_line(func)`
注册行处理回调函数

```python
@terminal.on_line
def handle_line(line):
    print(line)
```

##### `on_pattern(pattern)`
注册正则表达式匹配回调

```python
@terminal.on_pattern(r"(\d+) packets")
def handle_match(match):
    count = match.group(1)
    print(f"Count: {count}")
```

##### `on_connected(func)`
连接建立时的回调

```python
@terminal.on_connected
def on_connect():
    print("Connected!")
```

##### `on_disconnected(func)`
连接断开时的回调

```python
@terminal.on_disconnected
def on_disconnect():
    print("Disconnected!")
```

##### `send(data)`
发送数据到终端

```python
terminal.send("ls -la\n")
```

##### `expect(pattern, timeout=30)`
等待特定模式出现

```python
if terminal.expect("login:", timeout=10):
    terminal.send("admin\n")
```

##### `expect_list(patterns, timeout=30)`
等待多个模式之一出现

```python
index = terminal.expect_list([
    "login:",
    "Password:",
    "Connection refused"
], timeout=30)

if index == 0:
    terminal.send("admin\n")
```

##### `log(level, message)`
记录日志

```python
terminal.log("info", "Script started")
terminal.log("error", "Login failed")
```

日志级别：`debug`, `info`, `warning`, `error`

##### `exit()`
退出脚本

```python
terminal.exit()
```

##### `run()`
运行主循环（阻塞）

```python
terminal.run()
```

## 示例脚本

### 自动登录

```python
#!/usr/bin/env python3
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
from nterminal import Terminal

terminal = Terminal()

USERNAME = "admin"
PASSWORD = "password123"

@terminal.on_line
def auto_login(line):
    if "login:" in line.lower():
        terminal.send(f"{USERNAME}\n")
    elif "password:" in line.lower():
        terminal.send(f"{PASSWORD}\n")

terminal.run()
```

### 日志监控

```python
#!/usr/bin/env python3
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
from nterminal import Terminal

terminal = Terminal()

@terminal.on_pattern(r"ERROR|CRITICAL")
def handle_error(match):
    line = match.string
    terminal.log("error", f"Error detected: {line}")
    # 可以发送告警、记录日志等

terminal.run()
```

### 自动化测试

```python
#!/usr/bin/env python3
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
from nterminal import Terminal

terminal = Terminal()

tests = [
    ("uptime\n", "load average"),
    ("df -h\n", "Filesystem"),
]

current_test = 0

@terminal.on_line
def run_tests(line):
    global current_test

    if current_test < len(tests):
        command, expected = tests[current_test]

        if expected in line:
            terminal.log("info", f"Test {current_test + 1} PASS")
            current_test += 1

            if current_test < len(tests):
                terminal.send(tests[current_test][0])
            else:
                terminal.log("info", "All tests completed")
                terminal.exit()

@terminal.on_connected
def start_tests():
    terminal.expect("$", timeout=30)
    terminal.send(tests[0][0])

terminal.run()
```

## 内置示例脚本

NTerminal 提供了以下示例脚本：

1. **simple_example.py** - 简单示例，演示基本功能
2. **auto_login.py** - 自动登录脚本
3. **log_monitor.py** - 日志监控脚本
4. **test_automation.py** - 自动化测试脚本

这些脚本位于 `scripts/` 目录下。

## 最佳实践

### 1. 错误处理

在回调函数中添加错误处理：

```python
@terminal.on_line
def handle_line(line):
    try:
        # 处理逻辑
        pass
    except Exception as e:
        terminal.log("error", f"Error: {e}")
```

### 2. 状态管理

使用字典或类来管理状态：

```python
state = {"stage": "init", "attempts": 0}

@terminal.on_line
def handle_line(line):
    if state["stage"] == "init":
        # 处理初始状态
        pass
```

### 3. 超时处理

使用 `expect()` 时设置合理的超时：

```python
if not terminal.expect("login:", timeout=10):
    terminal.log("error", "Login prompt not detected")
    terminal.exit()
```

### 4. 日志记录

充分使用日志功能：

```python
terminal.log("info", "Starting process...")
terminal.log("debug", f"Current state: {state}")
terminal.log("warning", "Unexpected response")
terminal.log("error", "Operation failed")
```

## 调试技巧

### 1. 打印调试信息

```python
@terminal.on_line
def debug_line(line):
    print(f"DEBUG: {line}", file=sys.stderr)
```

### 2. 记录所有输出

```python
with open("debug.log", "w") as f:
    @terminal.on_line
    def log_all(line):
        f.write(f"{line}\n")
        f.flush()
```

### 3. 使用正则表达式测试

在脚本外测试正则表达式：

```python
import re
pattern = re.compile(r"(\d+) packets")
match = pattern.search("3 packets transmitted")
if match:
    print(match.group(1))  # 输出: 3
```

## 常见问题

### Q: 脚本没有响应？

A: 检查：
1. Python 是否正确安装
2. 脚本路径是否正确
3. 是否调用了 `terminal.run()`
4. 查看 NTerminal 的日志输出

### Q: 如何发送特殊字符？

A: 使用转义序列：

```python
terminal.send("password\n")  # 换行
terminal.send("\t")          # Tab
terminal.send("\x03")        # Ctrl+C
```

### Q: 如何处理中文？

A: 确保脚本文件使用 UTF-8 编码：

```python
# -*- coding: utf-8 -*-
```

### Q: 脚本如何访问外部资源？

A: 可以使用任何 Python 标准库或第三方库：

```python
import requests
import json

@terminal.on_pattern(r"ERROR")
def send_alert(match):
    # 发送 HTTP 请求
    requests.post("https://api.example.com/alert",
                  json={"message": match.string})
```

## 进阶用法

### 多线程

```python
import threading

def background_task():
    while True:
        # 后台任务
        time.sleep(60)

thread = threading.Thread(target=background_task, daemon=True)
thread.start()

terminal.run()
```

### 数据持久化

```python
import json

data = {"count": 0}

@terminal.on_line
def collect_data(line):
    data["count"] += 1

    # 定期保存
    if data["count"] % 100 == 0:
        with open("data.json", "w") as f:
            json.dump(data, f)
```

### 与其他工具集成

```python
import subprocess

@terminal.on_pattern(r"CRITICAL")
def send_notification(match):
    # 使用系统通知
    subprocess.run(["notify-send", "Alert", match.string])
```

## 参考资源

- Python 正则表达式文档: https://docs.python.org/3/library/re.html
- Python JSON 文档: https://docs.python.org/3/library/json.html
- Python 线程文档: https://docs.python.org/3/library/threading.html
