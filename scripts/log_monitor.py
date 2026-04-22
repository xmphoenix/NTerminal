#!/usr/bin/env python3
"""
日志监控脚本示例
监控终端输出中的错误和警告信息
"""

import sys
import os
import re
from datetime import datetime

sys.path.insert(0, os.path.dirname(__file__))

from nterminal import Terminal

terminal = Terminal()

# 统计信息
stats = {
    "errors": 0,
    "warnings": 0,
    "criticals": 0
}

# 日志文件
log_file = None


def init_log_file():
    """初始化日志文件"""
    global log_file
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"monitor_log_{timestamp}.txt"
    log_file = open(filename, "w", encoding="utf-8")
    terminal.log("info", f"Log file created: {filename}")


def write_log(level, line):
    """写入日志文件"""
    if log_file:
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log_file.write(f"[{timestamp}] [{level}] {line}\n")
        log_file.flush()


@terminal.on_pattern(r"ERROR|CRITICAL|FATAL")
def handle_error(match):
    """处理错误信息"""
    line = match.string
    stats["errors"] += 1

    terminal.log("error", f"Error detected: {line}")
    write_log("ERROR", line)

    # 可以在这里添加告警逻辑
    # send_alert("ERROR", line)


@terminal.on_pattern(r"WARNING|WARN")
def handle_warning(match):
    """处理警告信息"""
    line = match.string
    stats["warnings"] += 1

    terminal.log("warning", f"Warning detected: {line}")
    write_log("WARNING", line)


@terminal.on_pattern(r"CRITICAL")
def handle_critical(match):
    """处理严重错误"""
    line = match.string
    stats["criticals"] += 1

    terminal.log("error", f"Critical error detected: {line}")
    write_log("CRITICAL", line)

    # 严重错误可能需要立即告警
    # send_urgent_alert("CRITICAL", line)


@terminal.on_line
def monitor_all(line):
    """监控所有输出"""
    # 记录所有行到日志文件
    write_log("INFO", line)

    # 每 100 行输出一次统计
    total = stats["errors"] + stats["warnings"] + stats["criticals"]
    if total > 0 and total % 100 == 0:
        terminal.log("info",
                     f"Stats - Errors: {stats['errors']}, "
                     f"Warnings: {stats['warnings']}, "
                     f"Criticals: {stats['criticals']}")


@terminal.on_connected
def on_connect():
    """连接建立时"""
    init_log_file()
    terminal.log("info", "Log monitoring started")


@terminal.on_disconnected
def on_disconnect():
    """连接断开时"""
    terminal.log("info",
                 f"Monitoring stopped - Final stats: "
                 f"Errors: {stats['errors']}, "
                 f"Warnings: {stats['warnings']}, "
                 f"Criticals: {stats['criticals']}")

    if log_file:
        log_file.close()


if __name__ == "__main__":
    terminal.log("info", "Log monitor script started")
    try:
        terminal.run()
    except KeyboardInterrupt:
        terminal.log("info", "Script interrupted by user")
    finally:
        if log_file:
            log_file.close()
