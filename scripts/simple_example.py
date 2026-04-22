#!/usr/bin/env python3
"""
简单示例脚本
演示基本的 Python 脚本功能
"""

import sys
import os

sys.path.insert(0, os.path.dirname(__file__))

from nterminal import Terminal

terminal = Terminal()


@terminal.on_line
def echo_line(line):
    """回显每一行输出"""
    # 在这里可以处理每一行数据
    # 例如：过滤、记录、分析等

    # 如果检测到特定内容，可以自动发送命令
    if "Press any key to continue" in line:
        terminal.send("\n")
        terminal.log("info", "Auto-pressed key to continue")

    # 如果检测到错误，可以记录
    if "error" in line.lower():
        terminal.log("warning", f"Error detected: {line}")


@terminal.on_pattern(r"(\d+\.\d+\.\d+\.\d+)")
def detect_ip(match):
    """检测 IP 地址"""
    ip = match.group(1)
    terminal.log("info", f"Detected IP address: {ip}")


@terminal.on_connected
def on_connect():
    """连接建立时"""
    terminal.log("info", "Connected! Script is now monitoring output...")


if __name__ == "__main__":
    terminal.log("info", "Simple example script started")
    terminal.run()
