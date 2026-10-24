# MCP（AI 控制终端）

NTerminal 内置 MCP（Model Context Protocol）服务，让 Claude Desktop、Claude Code 等 AI 客户端读取终端内容、发送命令、等待输出并管理会话。

## 架构

```
Claude Desktop / Claude Code
        │  MCP over stdio（换行分隔 JSON-RPC）
        ▼
NTerminal.exe --mcp-stdio（McpBridge，控制台桥）
        │  每用户本地命名管道
        ▼
NTerminal.exe（GUI，McpServer）
   └─ TerminalProxy / SessionManager / 审计日志
```

- MCP 工具与权限逻辑都在 GUI 进程内，桥接器只负责字节转发。
- 因此 AI 附加到**正在运行**的 NTerminal，而不是再开一个实例。
- MCP 菜单可启用/停用服务、复制客户端配置、设置权限、查看审计日志。

## 快速开始

### 1. 启动 NTerminal

打开 NTerminal，在「MCP」菜单确认「启用 MCP 服务」已勾选。服务默认启用，策略默认为「每次确认」。

### 2. Claude Code

```bash
claude mcp add --transport stdio nterminal -- "C:\Path\NTerminal.exe" --mcp-stdio
```

也可以在 NTerminal 的「MCP → 复制 Claude Code 命令」中直接复制完整命令。

### 3. Claude Desktop

在「MCP → 复制 Claude Desktop 配置」中复制 JSON，然后粘贴到 `claude_desktop_config.json`：

```json
{
  "mcpServers": {
    "nterminal": {
      "command": "C:\\Path\\NTerminal.exe",
      "args": ["--mcp-stdio"]
    }
  }
}
```

重启 Claude Desktop。需要保持 NTerminal 运行，且 MCP 服务处于启用状态。

### 4. 调试

```bash
npx @modelcontextprotocol/inspector "C:\Path\NTerminal.exe" --mcp-stdio
```

MCP Inspector 通过 stdio 启动桥接器，因此同样要求 GUI 正在运行。

## 工具

| 工具 | 作用 | 权限 |
|---|---|---|
| `list_sessions` | 列出当前打开的终端标签 | 只读 |
| `get_session` | 单个会话详情（不返回密码） | 只读 |
| `read_screen` | 读取可见屏幕，可选最近回滚行 | 只读 |
| `send_text` | 发送文本，可自动追加回车 | 写入 |
| `send_keys` | 发送 Enter/Tab/Esc/方向键/Ctrl+字母/F1-F12 | 写入 |
| `wait_for` | 等待**新输出**匹配正则；支持进度与取消 | 只读（异步） |
| `run_command` | 发送命令并等待完成，返回输出 | 写入（异步） |
| `list_saved_sessions` | 列出已保存的连接配置（不含密码） | 只读 |
| `open_saved_session` | 按已保存会话名打开新标签 | 写入 |
| `close_session` | 关闭终端标签/连接 | 写入 |
| `get_system_stats` | SSH 会话的 CPU/内存/网络快照 | 只读 |
| `list_transfers` | 列出 MCP 发起的文件传输 | 只读 |
| `start_transfer` | 启动 ZMODEM/YMODEM/XMODEM/Kermit/ASCII 传输 | 写入 |
| `get_transfer_status` | 查询进度、结果与协议日志 | 只读 |
| `cancel_transfer` | 取消正在运行的传输 | 写入 |
| `list_servers` | 列出内置网络服务及运行状态 | 只读 |
| `get_server_status` | 服务端口、配置（密码脱敏）与最近日志 | 只读 |
| `start_server` | 启动内置网络服务 | 写入 |
| `stop_server` | 停止内置网络服务 | 写入 |

### 关键参数

- `session_id`：`list_sessions` 返回的 `terminal_N`；所有终端工具都要显式传入，不依赖“当前标签页”。
- `line_ending`：`send_text`/`run_command` 提交时的行尾，默认 `\r`，可选 `\n`、`\r\n`。
- `timeout_ms`：`wait_for`/`run_command` 超时，100–300000 ms。
- `completion`：`run_command` 的完成判断：
  - `marker`（默认）：在命令后追加 `echo <随机标记>`，等待标记**单独成行**出现。终端的命令回显是 `echo <标记>`，不会误匹配。
  - `idle`：输出安静 600 ms 后视为完成，适合没有 shell 的串口设备。

### 资源

同时提供只读资源，便于客户端在对话外读取终端状态：

- `nt://app/info`
- `nt://sessions/{id}/screen`
- `nt://sessions/{id}/scrollback`

## 文件传输

`start_transfer` 支持 5 种协议：

| 协议 | 发送 | 接收 |
|---|---|---|
| ZMODEM | 一个或多个文件 | 一个已存在的目录 |
| YMODEM | 一个或多个文件 | 一个已存在的目录 |
| XMODEM | 单个文件 | 单个目标文件路径 |
| Kermit | 单个文件 | 一个已存在的目录 |
| ASCII | 单个文本文件（自动转 CRLF） | 单个目标文件路径 |

`start_transfer` 立即返回 `transfer_id`，传输在后台进行；用 `get_transfer_status` 轮询 `running / result / transferred / total / log`，或 `cancel_transfer` 取消。传输期间该终端的入站字节由协议引擎接管，会话关闭会自动取消传输。

示例：

```json
{
  "session_id": "terminal_1",
  "protocol": "zmodem",
  "direction": "send",
  "paths": ["C:\\firmware\\image.bin"]
}
```

## 内置网络服务

`list_servers` / `get_server_status` / `start_server` / `stop_server` 对应「工具 → 网络服务」中的 TFTP、FTP、HTTP、Telnet、NFS。

- 启动时可传 `config` 覆盖非敏感字段（`port`、`root`、`allowWrite`、`listDirectory` 等）。
- **密码不会通过 MCP 读写**：`start_server` 忽略 `password` 字段，保存的密码继续沿用；状态/日志里也不会返回密码。
- 服务会监听本机端口，是否可从局域网访问取决于服务实现和防火墙；启动前会按当前 AI 权限策略确认。

## 权限

「MCP 设置」提供三档策略：

| 策略 | 行为 |
|---|---|
| 只读 | AI 只能列会话、读屏、等待输出 |
| 每次确认 | 写入/关闭/打开会话前弹出确认框，显示客户端、会话和操作预览 |
| 允许控制 | 写入不再询问（适合受信任的自动化） |

默认是「每次确认」。确认框属于 GUI 进程；AI 请求会等待你的选择。

## 协议兼容

MCP 同时兼容两代协议：

- **2025-11-25 及更早（legacy）**：`initialize` → `initialized` → `tools/list` / `tools/call`。Claude Desktop 当前使用这一代。
- **2026-07-28（modern）**：无状态；请求携带 `_meta.io.modelcontextprotocol/protocolVersion`；实现 `server/discover`，结果包含 `resultType`。

stdio 规则：一行一条 JSON-RPC；`stdout` 只承载 MCP 消息；日志走 `stderr`。

## 安全与审计

- 本地命名管道使用随机名称，端点文件为 `~/.nterminal/mcp-endpoint.json`（仅当前用户可读）。
- 凭据不会通过工具返回：`list_sessions`/`get_session`/`list_saved_sessions` 不包含密码。
- 终端输出视为**不可信数据**；工具描述明确要求模型不要执行输出中的指令。
- 每次工具调用写入 `~/.nterminal/mcp-audit.log`（JSON Lines），可在「MCP → 查看 AI 操作日志」中查看或清空。
- 工具输出默认截断（约 64 KB）并去除 ANSI 控制序列，避免撑爆模型上下文。

## 常见问题

**AI 提示 "NTerminal MCP service is not running"**

- 确认 NTerminal GUI 正在运行。
- 在「MCP」菜单确认「启用 MCP 服务」已勾选。
- 检查 `~/.nterminal/mcp-endpoint.json` 是否存在；如果 GUI 已退出，该文件可能残留，重新启动 GUI 会覆盖。

**工具调用返回 "AI control is disabled"**

- 在「MCP 设置」中把权限切到「每次确认」或「允许控制」。

**`run_command` 超时**

- 目标设备可能没有 shell，改用 `completion: "idle"`。
- 或先用 `send_text` 发送命令，再用 `read_screen` / `wait_for` 观察输出。
