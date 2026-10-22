# Python 脚本引擎设计文档

## 概述

Python 脚本引擎允许用户编写 Python 脚本来监控终端输出、自动化交互和处理数据。类似于 expect 脚本，但使用 Python 语言。

## 架构设计

### 1. 核心组件

```
┌─────────────────────────────────────────┐
│         TerminalWidget/Proxy            │
│  ┌───────────────────────────────────┐  │
│  │   PythonScriptEngine              │  │
│  │  ┌─────────────────────────────┐  │  │
│  │  │  Python Interpreter         │  │  │
│  │  │  - 嵌入式 Python 3.x        │  │  │
│  │  └─────────────────────────────┘  │  │
│  │  ┌─────────────────────────────┐  │  │
│  │  │  API Bindings               │  │  │
│  │  │  - terminal.send()          │  │  │
│  │  │  - terminal.on_line()       │  │  │
│  │  │  - terminal.expect()        │  │  │
│  │  └─────────────────────────────┘  │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

### 2. 实现方案

由于嵌入 Python 解释器比较复杂，我们采用**进程间通信**的方式：

```
┌──────────────────┐         ┌──────────────────┐
│  NTerminal       │  JSON   │  Python Process  │
│  (Qt/C++)        │ ◄─────► │  (script.py)     │
│                  │  IPC    │                  │
│  - 捕获输出      │         │  - 处理数据      │
│  - 发送命令      │         │  - 自动化逻辑    │
└──────────────────┘         └──────────────────┘
```

### 3. 通信协议

使用 JSON 格式通过标准输入/输出通信：

#### 从终端到脚本的消息：

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

#### 从脚本到终端的消息：

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

## Python API 设计

### 基础 API

```python
import nterminal

# 连接到终端
terminal = nterminal.Terminal()

# 注册行处理回调
@terminal.on_line
def handle_line(line):
    """处理每一行输出"""
    if "login:" in line:
        terminal.send("admin\n")
    elif "Password:" in line:
        terminal.send("password123\n")

# 等待特定模式
terminal.expect("login:", timeout=10)
terminal.send("admin\n")

# 等待多个模式之一
index = terminal.expect_list([
    "login:",
    "Password:",
    "Connection refused"
], timeout=30)

if index == 0:
    terminal.send("admin\n")
elif index == 1:
    terminal.send("password123\n")
elif index == 2:
    print("Connection failed!")

# 运行主循环
terminal.run()
```

### 高级 API

```python
import nterminal
import re

terminal = nterminal.Terminal()

# 使用正则表达式匹配
@terminal.on_pattern(r"(\d+) packets transmitted")
def handle_ping_result(match):
    count = match.group(1)
    print(f"Ping sent {count} packets")

# 状态机模式
class LoginStateMachine:
    def __init__(self, terminal):
        self.terminal = terminal
        self.state = "init"

    @terminal.on_line
    def process(self, line):
        if self.state == "init" and "login:" in line:
            self.terminal.send("admin\n")
            self.state = "sent_username"
        elif self.state == "sent_username" and "Password:" in line:
            self.terminal.send("password123\n")
            self.state = "sent_password"
        elif self.state == "sent_password" and "$" in line:
            print("Login successful!")
            self.state = "logged_in"

# 超时处理
@terminal.on_timeout(30)
def handle_timeout():
    print("No response for 30 seconds")
    terminal.exit()

# 记录日志
terminal.log("info", "Script started")
terminal.log("debug", f"Current state: {state}")

# 访问终端信息
info = terminal.get_info()
print(f"Connected to: {info['host']}:{info['port']}")
print(f"Connection type: {info['type']}")  # ssh, telnet, serial
```

## 使用场景示例

### 1. 自动登录脚本

```python
#!/usr/bin/env python3
import nterminal

terminal = nterminal.Terminal()

@terminal.on_line
def auto_login(line):
    if "login:" in line.lower():
        terminal.send("admin\n")
        terminal.log("info", "Sent username")
    elif "password:" in line.lower():
        terminal.send("mypassword\n")
        terminal.log("info", "Sent password")
    elif "$" in line or "#" in line:
        terminal.log("info", "Login successful!")
        # 登录成功后执行命令
        terminal.send("show version\n")

terminal.run()
```

### 2. 日志监控和告警

```python
#!/usr/bin/env python3
import nterminal
import re

terminal = nterminal.Terminal()

error_pattern = re.compile(r"ERROR|CRITICAL|FATAL", re.IGNORECASE)
warning_pattern = re.compile(r"WARNING|WARN", re.IGNORECASE)

@terminal.on_line
def monitor_logs(line):
    if error_pattern.search(line):
        terminal.log("error", f"Error detected: {line}")
        # 可以发送邮件、webhook 等
        send_alert("ERROR", line)
    elif warning_pattern.search(line):
        terminal.log("warning", f"Warning detected: {line}")

def send_alert(level, message):
    # 发送告警通知
    pass

terminal.run()
```

### 3. 自动化测试脚本

```python
#!/usr/bin/env python3
import nterminal
import time

terminal = nterminal.Terminal()

test_results = []

def run_test(command, expected_output):
    """运行测试命令并验证输出"""
    terminal.send(f"{command}\n")
    time.sleep(1)

    # 等待预期输出
    try:
        terminal.expect(expected_output, timeout=10)
        test_results.append({"command": command, "status": "PASS"})
        terminal.log("info", f"Test PASS: {command}")
    except TimeoutError:
        test_results.append({"command": command, "status": "FAIL"})
        terminal.log("error", f"Test FAIL: {command}")

@terminal.on_connected
def start_tests():
    # 等待登录完成
    terminal.expect("$", timeout=30)

    # 运行测试
    run_test("ping -c 3 8.8.8.8", "3 packets transmitted")
    run_test("df -h", "Filesystem")
    run_test("uptime", "load average")

    # 输出测试结果
    print("\n=== Test Results ===")
    for result in test_results:
        print(f"{result['command']}: {result['status']}")

    terminal.exit()

terminal.run()
```

### 4. 数据采集脚本

```python
#!/usr/bin/env python3
import nterminal
import json
import re

terminal = nterminal.Terminal()

collected_data = {
    "cpu_usage": [],
    "memory_usage": [],
    "disk_usage": []
}

@terminal.on_line
def collect_metrics(line):
    # 解析 CPU 使用率
    cpu_match = re.search(r"CPU:\s+(\d+\.?\d*)%", line)
    if cpu_match:
        collected_data["cpu_usage"].append(float(cpu_match.group(1)))

    # 解析内存使用率
    mem_match = re.search(r"Memory:\s+(\d+\.?\d*)%", line)
    if mem_match:
        collected_data["memory_usage"].append(float(mem_match.group(1)))

    # 解析磁盘使用率
    disk_match = re.search(r"Disk:\s+(\d+\.?\d*)%", line)
    if disk_match:
        collected_data["disk_usage"].append(float(disk_match.group(1)))

@terminal.on_connected
def start_monitoring():
    terminal.expect("$", timeout=30)

    # 每 5 秒采集一次数据
    for i in range(12):  # 采集 1 分钟
        terminal.send("top -bn1 | grep 'Cpu(s)'\n")
        time.sleep(5)

    # 保存数据
    with open("metrics.json", "w") as f:
        json.dump(collected_data, f, indent=2)

    terminal.log("info", "Data collection completed")
    terminal.exit()

terminal.run()
```

## 实现细节

### 1. NTerminal 端实现

```cpp
class PythonScriptEngine : public QObject
{
    Q_OBJECT

public:
    PythonScriptEngine(QObject *parent = nullptr);
    ~PythonScriptEngine();

    // 加载并运行脚本
    bool loadScript(const QString &scriptPath);
    void stopScript();
    bool isRunning() const;

    // 发送数据到脚本
    void sendLine(const QString &line);
    void sendEvent(const QString &eventType, const QJsonObject &data);

signals:
    // 从脚本接收的命令
    void sendCommand(const QString &command);
    void logMessage(const QString &level, const QString &message);
    void scriptExited(int exitCode);

private slots:
    void onProcessReadyRead();
    void onProcessFinished(int exitCode);
    void onProcessError(QProcess::ProcessError error);

private:
    void sendMessage(const QJsonObject &message);
    void processMessage(const QJsonObject &message);

    QProcess *m_process;
    QString m_scriptPath;
    QByteArray m_buffer;
};
```

### 2. Python 端实现

```python
# nterminal.py - Python 模块

import sys
import json
import threading
import queue
import re
from typing import Callable, List, Optional

class Terminal:
    def __init__(self):
        self._line_handlers = []
        self._pattern_handlers = []
        self._event_handlers = {}
        self._running = False
        self._input_queue = queue.Queue()
        self._expect_queue = queue.Queue()

    def on_line(self, func: Callable[[str], None]):
        """注册行处理回调"""
        self._line_handlers.append(func)
        return func

    def on_pattern(self, pattern: str):
        """注册正则表达式匹配回调"""
        def decorator(func):
            self._pattern_handlers.append((re.compile(pattern), func))
            return func
        return decorator

    def on_connected(self, func: Callable[[], None]):
        """连接建立时的回调"""
        self._event_handlers['connected'] = func
        return func

    def send(self, data: str):
        """发送数据到终端"""
        self._send_message({
            "type": "send",
            "data": data
        })

    def expect(self, pattern: str, timeout: int = 30):
        """等待特定模式出现"""
        # 实现等待逻辑
        pass

    def log(self, level: str, message: str):
        """记录日志"""
        self._send_message({
            "type": "log",
            "level": level,
            "message": message
        })

    def exit(self):
        """退出脚本"""
        self._send_message({"type": "exit"})
        self._running = False

    def run(self):
        """运行主循环"""
        self._running = True

        # 启动输入读取线程
        input_thread = threading.Thread(target=self._read_input)
        input_thread.daemon = True
        input_thread.start()

        # 处理消息
        while self._running:
            try:
                message = self._input_queue.get(timeout=1)
                self._process_message(message)
            except queue.Empty:
                continue

    def _read_input(self):
        """从标准输入读取消息"""
        for line in sys.stdin:
            try:
                message = json.loads(line)
                self._input_queue.put(message)
            except json.JSONDecodeError:
                pass

    def _process_message(self, message: dict):
        """处理接收到的消息"""
        msg_type = message.get("type")

        if msg_type == "line":
            line = message.get("data", "")

            # 调用行处理器
            for handler in self._line_handlers:
                handler(line)

            # 调用模式匹配处理器
            for pattern, handler in self._pattern_handlers:
                match = pattern.search(line)
                if match:
                    handler(match)

        elif msg_type == "connected":
            handler = self._event_handlers.get('connected')
            if handler:
                handler()

        elif msg_type == "disconnected":
            handler = self._event_handlers.get('disconnected')
            if handler:
                handler()

    def _send_message(self, message: dict):
        """发送消息到标准输出"""
        print(json.dumps(message), flush=True)
```

## 配置和使用

### 1. 在 NTerminal 中启用脚本

```cpp
// 在 TerminalWidget 中
PythonScriptEngine *m_scriptEngine;

// 加载脚本
m_scriptEngine->loadScript("/path/to/script.py");

// 连接信号
connect(m_scriptEngine, &PythonScriptEngine::sendCommand,
        this, &TerminalWidget::sendText);

// 发送终端输出到脚本
connect(parser, &TerminalParser::textReceived,
        [this](const QString &text) {
            if (m_scriptEngine->isRunning()) {
                m_scriptEngine->sendLine(text);
            }
        });
```

### 2. 脚本文件结构

```
scripts/
├── auto_login.py       # 自动登录脚本
├── log_monitor.py      # 日志监控脚本
├── test_suite.py       # 自动化测试脚本
└── nterminal.py        # Python 模块（库文件）
```

## 安全考虑

1. **脚本权限**：脚本运行在独立进程中，无法直接访问终端进程内存
2. **输入验证**：验证所有从脚本接收的 JSON 消息
3. **资源限制**：限制脚本进程的 CPU 和内存使用
4. **超时保护**：脚本无响应时自动终止

## 扩展功能

### 1. 脚本管理界面

- 脚本列表
- 启动/停止脚本
- 查看脚本日志
- 编辑脚本

### 2. 内置脚本库

- 常用自动化脚本模板
- 一键导入和使用

### 3. 调试功能

- 脚本调试模式
- 断点支持
- 变量查看

## 总结

这个设计提供了：
- ✅ 捕获每一行终端输出
- ✅ 灵活的数据处理（Python 脚本）
- ✅ 自动化交互能力
- ✅ 简单易用的 API
- ✅ 安全的进程隔离
- ✅ 丰富的使用场景支持
