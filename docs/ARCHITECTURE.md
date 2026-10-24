# NTerminal 架构说明

> 本文描述 2026-10 分层重构后的目标架构。重构原则：**功能不变、模块边界清晰、可测试性优先**。

## 1. 顶层布局

```
NTerminal/
├── NTerminal.pro          # 产品工程（qmake，GUI + 会话进程 + 测试窗口）
├── src/                   # 产品源码（按层分模块）
│   ├── app/               # 应用入口与进程模式分发
│   ├── common/            # 跨进程公共契约（纯头文件，无状态）
│   ├── ipc/               # 进程间通信（命名管道帧协议）
│   ├── session/           # 会话工作进程侧（协议处理器）
│   ├── terminal/          # 终端核心（解析器/屏幕模型/代理）
│   ├── mcp/               # MCP 服务（工具/资源/权限/审计）+ stdio 桥
│   ├── files/             # SFTP/SCP/FTP 文件客户端与工作线程
│   ├── update/            # 自动更新（清单/下载/校验/独立更新脚本）
│   ├── services/          # 应用服务（会话持久化）
│   ├── theme/             # 外观管理（主题/字体单例）
│   └── ui/                # Qt 界面（主窗口与全部对话框）
├── tests/                 # 单元测试（Qt Test，每模块一目录）
├── docs/                  # 项目文档
├── resources/             # qrc 资源（字体/图标/翻译）
└── build_libssh.ps1       # Windows 下 libssh 源码构建链
```

## 2. 运行形态（单可执行文件，三种进程）

```
main(argc, argv)
  └─ App::LaunchOptions::parse()            [src/app/launchoptions]
       ├─ -s/--session <id> → 会话进程：QCoreApplication + SessionProcess
       ├─ -t/--test         → 测试窗口：QApplication + TestWindow
       ├─ --mcp-stdio       → MCP 桥：QCoreApplication + McpBridge（转发到 GUI 的本地管道）
       └─ 默认              → GUI 主程序：MainWindow + McpServer
```

## 3. 模块职责与依赖规则

依赖方向**自上而下、单向**（上层可依赖下层，反之禁止）：

```
ui ──────────────┐
theme            │
update           │
files            │
services ────────┼──► terminal ──► ipc ──► common
mcp ─────────────┤        │
session ─────────┘        └─（mcp：MCP 工具层，直接操作 TerminalProxy）
```

| 模块 | 职责 | 允许依赖 | 关键接口 |
|---|---|---|---|
| `common` | IPC 消息契约（`Ipc::MessageType`/`encodeMessage`）、连接设置（`Common::ConnectionSettings`，唯一权威定义 + 稳定 JSON 契约） | 仅 QtCore | header-only |
| `ipc` | 帧编解码与粘包缓冲（`IpcProtocol`）、主进程侧客户端（`IpcClient`）、会话进程侧服务端（`IpcServer`） | common | `processData`/`sendMessage`/`messageReceived` |
| `terminal` | VT/ANSI 状态机（`TerminalParser`，纯逻辑 50+ 信号）；屏幕模型与渲染（`TerminalProxy`）；主题数据结构（`TerminalTheme`） | common, ipc | `processData` / `processTerminalData` / 只读状态查询（`screenText`/`historyText`/`cellAt`/`cursorRow`...） |
| `session` | 会话进程总控（`SessionProcess`）、会话日志（`SessionLogger`）、连接处理器抽象（`ConnectionHandler`）与工厂（`ConnectionHandlerFactory`）、7 种协议实现 | common, ipc | `open/close/sendData` + 4 信号 |
| `mcp` | MCP 服务：本地命名管道上的 JSON-RPC（双协议时代）、工具/资源、AI 权限与审计；`McpBridge` 负责 stdio ↔ 管道 | common, terminal, services, QtNetwork/Widgets（仅确认弹窗） | `start/stop`、`registerTerminal`、工具调用 |
| `files` | 图形化文件管理器后台：SFTP/SCP（libssh）与 FTP（QtNetwork）客户端，工作线程执行阻塞 I/O | common, QtNetwork, libssh | `RemoteFileSession`（list/upload/download/remove/rename） |
| `update` | 自动更新：清单解析、语义化版本比较、下载、SHA-256 校验、生成独立更新脚本 | common, QtNetwork | `checkForUpdates`/`downloadUpdate`/`applyUpdate`/`applyPendingUpdate` |
| `services` | 会话持久化（`SessionManager` → `~/.nterminal/sessions.json`） | common（**不依赖 UI**） | `loadSessions`/`saveSessions`/增删改查 |
| `theme` | `ThemeManager`/`FontManager` 单例：主题生产、QSettings 持久化 | terminal（仅 TerminalTheme 结构） | `currentTerminalTheme()` + 变更信号 |
| `ui` | 主窗口、连接对话框、工具对话框、终端测试器；RDP ActiveX 页面（Windows，Qt ActiveQt） | 全部下层 | — |
| `app` | 进程模式分发 | 全部下层 | `LaunchOptions::parse`（纯函数） |

### 依赖规则（评审红线）

1. `common` 不依赖任何项目内模块。
2. `services` 不得 include `ui`（历史反例：SessionManager 曾依赖 connectiondialog.h，已消除）。
3. `terminal` 不得访问全局单例（`TerminalProxy` 主题/字体均为注入式：`setTheme`/`setFont`）。
4. `session` 内部协议实现（含 libssh）不得反向泄漏到接口头文件
   （工厂 `ConnectionHandlerFactory` 独立编译单元承载全部具体协议 include）。
5. `mcp` 的协议/工具层不得 include `ui`；打开/关闭会话通过
   `setOpenSessionHandler`/`setCloseSessionHandler` 回调注入。
6. `update` 不依赖 `ui`/`terminal`/`session`，只负责清单、下载、校验与更新脚本。
7. `files` 不依赖 `ui`；阻塞网络操作只能在工作线程中执行，GUI 通过信号接收结果。
8. 跨模块 include 一律带模块前缀（`#include "terminal/terminalproxy.h"`），
   模块内部用同目录相对名。

## 4. 关键数据流

```
[远端] → 协议 Handler → SessionProcess → SessionLogger（旁路日志）
                              └→ IpcServer → 命名管道 → IpcClient
                                 → TerminalProxy::handleDataReceived
                                 → TerminalParser（状态机）→ 50+ 信号
                                 → TerminalProxy 屏幕缓冲（Cell 矩阵）→ 绘制

键盘 → TerminalProxy::keyPressEvent → sendData → IPC → Handler → [远端]

AI → MCP 客户端 → NTerminal.exe --mcp-stdio → 本地命名管道 → McpServer
   → 工具（list_sessions/read_screen/send_text/run_command...）
   → TerminalProxy / SessionManager → 终端标签与远端连接
   → 审计日志 ~/.nterminal/mcp-audit.log

主窗口文件管理器标签页（FileManagerDialog）→ RemoteFileSession（工作线程）
  → SshFileClient（SFTP/SCP）或 FtpFileClient（PASV）→ 远端目录 / 文件；
  结果经信号回到 GUI 双栏表格；本地/远端条目可直接拖拽触发上传/下载
```

- 连接建立：`MainWindow::openConnectionFromDialog` → `TerminalProxy::openConnection`
  （QProcess 启动自身 `--session <id>`）→ IPC 握手 → `ConnectionHandlerFactory::create`
  → `Handler::open()`。
- 测试/回放：`TerminalProxy::processTerminalData()` 将数据直接注入解析管线，
  等价于远端数据到达，不经过 IPC（供 TerminalTester 与单元测试使用）。

## 5. 与重构前的差异（行为兼容性说明）

- `sessions.json`：保存格式升级为稳定契约（dataBits 整数、parity/flowControl 短名），
  读取双向兼容历史格式（详见 `common/connectionsettings.h` 注释与 tests/common）。
- `TerminalTester`：verify 层由"恒真桩"改为真实断言（基于 TerminalProxy 只读状态接口），
  测试序列改为本地注入（不再发往真实连接，避免污染会话）；
  无终端连接时保持跳过（PASS）语义与历史一致。
- ESC 7/8（DECSC/DECRC）：修复了解析器分发区间遗漏导致命令不可达的缺陷
  （原实现中该分支为死代码，序列被静默丢弃）。
- 删除的死代码（不影响任何运行功能）：`TerminalWidget`（未参与编译）、
  `SshClient`/`Logger`/`ScriptRecorder`/`ScriptStatusWidget`（编译但零使用者）。
