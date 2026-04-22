#!/usr/bin/env python3
"""
自动化测试脚本示例
运行一系列测试命令并验证输出
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))

from nterminal import Terminal

terminal = Terminal()

# 测试用例
test_cases = [
    {
        "name": "Check system uptime",
        "command": "uptime\n",
        "expected": "load average",
        "timeout": 10
    },
    {
        "name": "Check disk space",
        "command": "df -h\n",
        "expected": "Filesystem",
        "timeout": 10
    },
    {
        "name": "Check memory usage",
        "command": "free -h\n",
        "expected": "Mem:",
        "timeout": 10
    },
    {
        "name": "Ping test",
        "command": "ping -c 3 8.8.8.8\n",
        "expected": "3 packets transmitted",
        "timeout": 15
    },
]

# 测试结果
test_results = []
current_test = {"index": -1, "started": False}


def run_next_test():
    """运行下一个测试"""
    current_test["index"] += 1

    if current_test["index"] >= len(test_cases):
        # 所有测试完成
        print_test_results()
        terminal.exit()
        return

    test = test_cases[current_test["index"]]
    terminal.log("info", f"Running test: {test['name']}")

    # 发送测试命令
    terminal.send(test["command"])
    current_test["started"] = True
    current_test["start_time"] = time.time()


def print_test_results():
    """打印测试结果"""
    terminal.log("info", "\n=== Test Results ===")

    passed = sum(1 for r in test_results if r["status"] == "PASS")
    failed = sum(1 for r in test_results if r["status"] == "FAIL")

    for result in test_results:
        status_symbol = "✓" if result["status"] == "PASS" else "✗"
        terminal.log("info",
                     f"{status_symbol} {result['name']}: {result['status']}")

    terminal.log("info",
                 f"\nTotal: {len(test_results)}, "
                 f"Passed: {passed}, Failed: {failed}")


@terminal.on_line
def check_test_output(line):
    """检查测试输出"""
    if not current_test["started"]:
        return

    test = test_cases[current_test["index"]]

    # 检查是否包含预期输出
    if test["expected"] in line:
        test_results.append({
            "name": test["name"],
            "status": "PASS"
        })
        terminal.log("info", f"Test PASS: {test['name']}")

        current_test["started"] = False
        time.sleep(0.5)  # 等待命令完成
        run_next_test()

    # 检查超时
    elif time.time() - current_test["start_time"] > test["timeout"]:
        test_results.append({
            "name": test["name"],
            "status": "FAIL",
            "reason": "Timeout"
        })
        terminal.log("error", f"Test FAIL (timeout): {test['name']}")

        current_test["started"] = False
        run_next_test()


@terminal.on_connected
def on_connect():
    """连接建立时"""
    terminal.log("info", "Connected, waiting for shell prompt...")

    # 等待 shell 提示符
    match = terminal.expect(r"[$#>]", timeout=30)
    if match:
        terminal.log("info", "Shell prompt detected, starting tests...")
        run_next_test()
    else:
        terminal.log("error", "Failed to detect shell prompt")
        terminal.exit()


if __name__ == "__main__":
    terminal.log("info", "Test automation script started")
    terminal.run()
