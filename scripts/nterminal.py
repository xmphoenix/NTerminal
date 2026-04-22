#!/usr/bin/env python3
"""
NTerminal Python API
用于编写终端自动化脚本
"""

import sys
import json
import threading
import queue
import re
import time
from typing import Callable, List, Optional, Pattern, Match
from datetime import datetime


class Terminal:
    """终端接口类"""

    def __init__(self):
        self._line_handlers = []
        self._pattern_handlers = []
        self._event_handlers = {}
        self._running = False
        self._input_queue = queue.Queue()
        self._expect_buffer = []
        self._expect_event = threading.Event()

    def on_line(self, func: Callable[[str], None]):
        """
        注册行处理回调

        Args:
            func: 回调函数，接收一个字符串参数（行内容）

        Example:
            @terminal.on_line
            def handle_line(line):
                if "login:" in line:
                    terminal.send("admin\\n")
        """
        self._line_handlers.append(func)
        return func

    def on_pattern(self, pattern: str):
        """
        注册正则表达式匹配回调

        Args:
            pattern: 正则表达式字符串

        Example:
            @terminal.on_pattern(r"(\d+) packets transmitted")
            def handle_ping(match):
                count = match.group(1)
                print(f"Ping sent {count} packets")
        """
        def decorator(func):
            compiled_pattern = re.compile(pattern)
            self._pattern_handlers.append((compiled_pattern, func))
            return func
        return decorator

    def on_connected(self, func: Callable[[], None]):
        """
        连接建立时的回调

        Example:
            @terminal.on_connected
            def on_connect():
                print("Connected to server")
        """
        self._event_handlers['connected'] = func
        return func

    def on_disconnected(self, func: Callable[[], None]):
        """
        连接断开时的回调

        Example:
            @terminal.on_disconnected
            def on_disconnect():
                print("Disconnected from server")
        """
        self._event_handlers['disconnected'] = func
        return func

    def send(self, data: str):
        """
        发送数据到终端

        Args:
            data: 要发送的字符串（通常以 \\n 结尾）

        Example:
            terminal.send("ls -la\\n")
        """
        self._send_message({
            "type": "send",
            "data": data
        })

    def expect(self, pattern: str, timeout: int = 30) -> Optional[Match]:
        """
        等待特定模式出现

        Args:
            pattern: 要匹配的字符串或正则表达式
            timeout: 超时时间（秒）

        Returns:
            匹配对象，如果超时则返回 None

        Example:
            if terminal.expect("login:", timeout=10):
                terminal.send("admin\\n")
        """
        compiled_pattern = re.compile(pattern)
        start_time = time.time()

        while time.time() - start_time < timeout:
            # 检查缓冲区中的所有行
            for line in self._expect_buffer:
                match = compiled_pattern.search(line)
                if match:
                    return match

            # 等待新数据
            self._expect_event.wait(timeout=0.1)
            self._expect_event.clear()

        return None

    def expect_list(self, patterns: List[str], timeout: int = 30) -> int:
        """
        等待多个模式之一出现

        Args:
            patterns: 模式列表
            timeout: 超时时间（秒）

        Returns:
            匹配的模式索引（0-based），如果超时则返回 -1

        Example:
            index = terminal.expect_list([
                "login:",
                "Password:",
                "Connection refused"
            ], timeout=30)

            if index == 0:
                terminal.send("admin\\n")
            elif index == 1:
                terminal.send("password\\n")
        """
        compiled_patterns = [re.compile(p) for p in patterns]
        start_time = time.time()

        while time.time() - start_time < timeout:
            for line in self._expect_buffer:
                for i, pattern in enumerate(compiled_patterns):
                    if pattern.search(line):
                        return i

            self._expect_event.wait(timeout=0.1)
            self._expect_event.clear()

        return -1

    def log(self, level: str, message: str):
        """
        记录日志

        Args:
            level: 日志级别 (debug, info, warning, error)
            message: 日志消息

        Example:
            terminal.log("info", "Script started")
            terminal.log("error", "Login failed")
        """
        self._send_message({
            "type": "log",
            "level": level,
            "message": message
        })

    def exit(self):
        """
        退出脚本

        Example:
            terminal.exit()
        """
        self._send_message({"type": "exit"})
        self._running = False

    def run(self):
        """
        运行主循环

        这个方法会阻塞，直到脚本退出

        Example:
            terminal = Terminal()

            @terminal.on_line
            def handle(line):
                print(line)

            terminal.run()
        """
        self._running = True

        # 启动输入读取线程
        input_thread = threading.Thread(target=self._read_input, daemon=True)
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
            line = line.strip()
            if not line:
                continue

            try:
                message = json.loads(line)
                self._input_queue.put(message)
            except json.JSONDecodeError as e:
                self.log("error", f"JSON decode error: {e}")

    def _process_message(self, message: dict):
        """处理接收到的消息"""
        msg_type = message.get("type")

        if msg_type == "line":
            line = message.get("data", "")

            # 添加到 expect 缓冲区
            self._expect_buffer.append(line)
            if len(self._expect_buffer) > 1000:  # 限制缓冲区大小
                self._expect_buffer.pop(0)
            self._expect_event.set()

            # 调用行处理器
            for handler in self._line_handlers:
                try:
                    handler(line)
                except Exception as e:
                    self.log("error", f"Error in line handler: {e}")

            # 调用模式匹配处理器
            for pattern, handler in self._pattern_handlers:
                try:
                    match = pattern.search(line)
                    if match:
                        handler(match)
                except Exception as e:
                    self.log("error", f"Error in pattern handler: {e}")

        elif msg_type == "connected":
            handler = self._event_handlers.get('connected')
            if handler:
                try:
                    handler()
                except Exception as e:
                    self.log("error", f"Error in connected handler: {e}")

        elif msg_type == "disconnected":
            handler = self._event_handlers.get('disconnected')
            if handler:
                try:
                    handler()
                except Exception as e:
                    self.log("error", f"Error in disconnected handler: {e}")

        elif msg_type == "exit":
            self._running = False

    def _send_message(self, message: dict):
        """发送消息到标准输出"""
        try:
            print(json.dumps(message), flush=True)
        except Exception as e:
            sys.stderr.write(f"Error sending message: {e}\n")


# 便捷函数
def create_terminal() -> Terminal:
    """创建终端实例"""
    return Terminal()
