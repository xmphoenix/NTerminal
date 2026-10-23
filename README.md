# NTerminal

跨平台多协议终端工具（Qt 6 / C++17）。支持 Serial、Telnet、SSH、Rlogin、SFTP、FTP、TFTP 七种连接协议，内置 Python 脚本引擎、会话持久化、VT100~VT320/xterm 兼容性测试器与开发者工具箱。

## 构建与测试

- 完整产品构建（需要 libssh，Windows 下缺失时自动触发 `build_libssh.ps1`）：`qmake NTerminal.pro && mingw32-make -j8`
- 单元测试（78 个用例，无需 libssh）：`cd tests && qmake ../tests/tests.pro && mingw32-make check`
- Windows libssh 构建指南：[docs/LIBSSH_BUILD_GUIDE.md](docs/LIBSSH_BUILD_GUIDE.md)
- 单元测试规范：[docs/TESTING.md](docs/TESTING.md)

## 界面语言

界面缺省为中文（源码字符串即中文），可在「设置 → 语言」中即时切换到 English，选择写入配置（`QSettings` 的 `UI/Language`），下次启动沿用。英文翻译源文件为 [translations/NTerminal_en_US.ts](translations/NTerminal_en_US.ts)，构建时由 `lrelease` 生成 `.qm` 并内嵌到 `:/i18n/`。修改界面文案后重新生成翻译：`lupdate NTerminal.pro -no-obsolete -locations none`。

## 目录结构

- `src/app` — 入口与进程模式（GUI / `--session` 会话进程 / `--test` 测试窗口）
- `src/common` — 跨进程契约：IPC 消息格式、ConnectionSettings（含稳定 JSON 契约）
- `src/ipc` — 命名管道通信：帧协议、客户端、服务端
- `src/session` — 会话进程：协议处理器抽象/工厂 + 7 种协议实现 + 会话日志
- `src/terminal` — 终端核心：VT/ANSI 解析器、屏幕模型（TerminalProxy）、主题结构
- `src/script` — Python 脚本引擎宿主
- `src/services` — 会话持久化（SessionManager）
- `src/theme` — 主题与字体管理
- `src/ui` — 主窗口与全部对话框
- `tests/` — 单元测试（Qt Test，模块对应 src/ 布局）
- `docs/` — 架构与专项文档

架构与依赖规则见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
