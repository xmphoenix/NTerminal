# NTerminal

跨平台多协议终端工具（Qt 6 / C++17）。支持 Serial、Telnet、SSH、Rlogin、SFTP、FTP、TFTP 七种连接协议，内置 MCP（Model Context Protocol）服务，可让 Claude Desktop / Claude Code 等 AI 客户端直接读取终端、发送命令、管理文件传输与内置网络服务；同时提供自动更新、会话持久化、VT100~VT320/xterm 兼容性测试器与开发者工具箱。

## 构建与测试

- 完整产品构建（需要 libssh，Windows 下缺失时自动触发 `build_libssh.ps1`）：`qmake NTerminal.pro && mingw32-make -j8`
- 单元测试（78 个用例，无需 libssh）：`cd tests && qmake ../tests/tests.pro && mingw32-make check`
- Windows libssh 构建指南：[docs/LIBSSH_BUILD_GUIDE.md](docs/LIBSSH_BUILD_GUIDE.md)
- 单元测试规范：[docs/TESTING.md](docs/TESTING.md)

## MCP（AI 控制终端）

NTerminal 内置 MCP 服务，把终端能力暴露为工具（会话列表、读屏、发送文本、按键、等待输出、执行命令等）。在 GUI 的「MCP」菜单中可启用服务、复制客户端配置、设置 AI 权限并查看审计日志。

Claude Code：

```bash
claude mcp add --transport stdio nterminal -- "C:\Path\NTerminal.exe" --mcp-stdio
```

Claude Desktop：在「MCP → 复制 Claude Desktop 配置」后粘贴到 `claude_desktop_config.json`。桥接器会连接正在运行的 NTerminal，请先启动 GUI 并保持 MCP 服务启用。详细说明见 [docs/MCP.md](docs/MCP.md)。

## 自动更新

「帮助 → 检查更新...」读取远程 `update.json`，比较版本、下载 zip、校验 SHA-256，退出后由独立更新脚本替换程序文件并自动重启；用户配置不会被覆盖。「设置 → 更新设置...」可填写更新源地址与启动时自动检查。

打包与发布流程见 [docs/AUTO_UPDATE.md](docs/AUTO_UPDATE.md)，打包脚本为 [tools/package_update.ps1](tools/package_update.ps1)。

## 界面语言

界面缺省为中文（源码字符串即中文），可在「设置 → 语言」中即时切换到 English，选择写入配置（`QSettings` 的 `UI/Language`），下次启动沿用。英文翻译源文件为 [translations/NTerminal_en_US.ts](translations/NTerminal_en_US.ts)，构建时由 `lrelease` 生成 `.qm` 并内嵌到 `:/i18n/`。修改界面文案后重新生成翻译：`lupdate NTerminal.pro -no-obsolete -locations none`。

## 目录结构

- `src/app` — 入口与进程模式（GUI / `--session` 会话进程 / `--test` 测试窗口 / `--mcp-stdio` MCP 桥）
- `src/common` — 跨进程契约：IPC 消息格式、ConnectionSettings（含稳定 JSON 契约）
- `src/ipc` — 命名管道通信：帧协议、客户端、服务端
- `src/session` — 会话进程：协议处理器抽象/工厂 + 7 种协议实现 + 会话日志
- `src/terminal` — 终端核心：VT/ANSI 解析器、屏幕模型（TerminalProxy）、主题结构
- `src/mcp` — MCP 服务（工具/资源/权限/审计）与 stdio 桥接器
- `src/services` — 会话持久化（SessionManager）
- `src/theme` — 主题与字体管理
- `src/ui` — 主窗口与全部对话框
- `tests/` — 单元测试（Qt Test，模块对应 src/ 布局）
- `docs/` — 架构与专项文档

架构与依赖规则见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
