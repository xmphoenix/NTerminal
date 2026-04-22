#!/usr/bin/env python3
"""
自动登录脚本示例
自动检测登录提示并输入用户名和密码
"""

import sys
import os

# 添加 nterminal 模块路径
sys.path.insert(0, os.path.dirname(__file__))

from nterminal import Terminal

# 配置
USERNAME = "admin"
PASSWORD = "password123"

terminal = Terminal()

# 状态跟踪
login_state = {"stage": "init"}


@terminal.on_line
def auto_login(line):
    """自动登录处理"""
    line_lower = line.lower().strip()

    if login_state["stage"] == "init":
        # 检测登录提示
        if "login:" in line_lower or "username:" in line_lower:
            terminal.log("info", f"Detected login prompt: {line}")
            terminal.send(f"{USERNAME}\n")
            login_state["stage"] = "sent_username"

    elif login_state["stage"] == "sent_username":
        # 检测密码提示
        if "password:" in line_lower:
            terminal.log("info", f"Detected password prompt: {line}")
            terminal.send(f"{PASSWORD}\n")
            login_state["stage"] = "sent_password"

    elif login_state["stage"] == "sent_password":
        # 检测登录成功
        if "$" in line or "#" in line or ">" in line:
            terminal.log("info", "Login successful!")
            login_state["stage"] = "logged_in"

            # 登录成功后可以执行命令
            # terminal.send("ls -la\n")

        # 检测登录失败
        elif "incorrect" in line_lower or "failed" in line_lower or "denied" in line_lower:
            terminal.log("error", f"Login failed: {line}")
            terminal.exit()


@terminal.on_connected
def on_connect():
    """连接建立时"""
    terminal.log("info", "Connected to server, waiting for login prompt...")


@terminal.on_disconnected
def on_disconnect():
    """连接断开时"""
    terminal.log("info", "Disconnected from server")


if __name__ == "__main__":
    terminal.log("info", "Auto-login script started")
    terminal.run()
