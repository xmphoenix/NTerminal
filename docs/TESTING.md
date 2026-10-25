# NTerminal 测试规范

## 1. 快速开始

```powershell
# 环境（按实际安装路径调整）
$env:PATH = "C:\Qt\6.10.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH

# 构建 + 运行全部单元测试（Windows/MinGW 示例）
mkdir build-tests; cd build-tests
qmake ../tests/tests.pro
mingw32-make -j8
mingw32-make check        # 或逐个运行 tst_*.exe
```

当前基线：**14 个测试程序、283 个用例，全部通过**。其中 `tests/integration`
的 5 个真实设备用例只在设置 `NTERMINAL_TEST_*` 环境变量后执行（详见第 7 节）。

## 2. 测试矩阵（模块 → 被测对象 → 覆盖点）

| 测试目录 | 被测对象 | 覆盖点 |
|---|---|---|
| `tests/app` | `src/app/launchoptions` | 三种进程模式分流、短/长参数、会话优先级、缺值容错 |
| `tests/common` | `src/common` | IPC 帧字节布局（大端）、头解码边界；连接设置默认值、7 类协议字符串契约、串口枚举、JSON 往返、缺省字段、**历史 sessions.json 格式兼容** |
| `tests/integration` | 真实 SSH/SFTP/SCP + 串口 | 工厂分发；串口不存在时失败、真实控制台登录与命令执行；SSH 登录/回显/终端尺寸/资源统计、错误密码拒绝；SFTP 目录/上传/下载/改名/删除/进度取消；SCP 上传/下载（默认跳过，见第 7 节） |
| `tests/ipc` | `src/ipc/ipcprotocol` | 编码重载（字节/JSON/字符串）、完整帧、半包等待、粘包多帧切分、空载荷、垃圾数据安全 |
| `tests/nfs` | `src/services/nfsserver` | RPC 记录标记/XDR、MOUNTv3 与 NFSv3 全部已实现过程、目录分页、cookie 校验、文件句柄错误、协议/版本拒绝、分片与异常帧 |
| `tests/parser` | `src/terminal/terminalparser` | 文本/UTF-8、控制字符、CSI 光标/擦除/编辑/制表位、SGR、私有模式、OSC 标题与剪贴板、字符集、设备查询、跨块序列、DCS 恢复、未知序列安全 |
| `tests/proxy` | `src/terminal/terminalproxy` | 屏幕模型：初始状态、文本写入/CR 覆盖/LF、CUP 语义、ED 清屏、SGR→Cell 属性（16 色/256 色）、备用屏、宽字符、`processTerminalData` 信号 |
| `tests/session` | `src/session/connectionhandler` | 抽象接口契约：生命周期、数据吞吐、信号发射、默认 setTerminalSize 安全性、QObject 所有权 |
| `tests/sessionlogger` | `src/session/sessionlogger` | ANSI 控制序列过滤、日志文件写入/追加、会话日志边界 |
| `tests/sessionmanager` | `src/services/sessionmanager` | `sessions.json` 保存/加载、排序、损坏文件与旧格式容错 |
| `tests/services` | `src/services` | HTTP（方法/目录/穿越/错误码）、FTP（登录/被动主动/上下传/管理命令/只读）、TFTP（块与选项/上传/错误）、Telnet（问候/命令/cd/exit）、服务生命周期与持久化 |
| `tests/theme` | `src/theme/thememanager` | 主题目录、明暗配对、QSS token、字体/主题切换 |
| `tests/transfer` | `src/transfer` | XMODEM/YMODEM/ZMODEM/Kermit 状态机与回环传输、校验、取消、超时 |
| `tests/update` | `src/update/updatemanager` | 版本比较、更新清单解析、下载/校验状态与边界 |

## 3. 目录与命名规范

```
tests/
├── tests.pro                 # TEMPLATE=subdirs
└── <module>/                 # 每个被测模块一个目录（与 src/ 模块对应）
    ├── <module>.pro          # 测试工程（qmake subdirs 按目录名自动发现）
    └── tst_<unit>.cpp        # 测试实现，TARGET = tst_<unit>
```

规则：
1. 测试工程名必须为 `<目录名>.pro`（qmake subdirs 的发现约定）。
2. 一个测试可执行文件聚焦一个被测单元；共享配置直接写在各自 .pro
   （`QT += testlib`、`CONFIG += console testcase`）。
3. 被测源码以相对路径直接编入测试工程（`../../src/...`），**不链接产品二进制**，
   保证编译期即可发现接口问题。
4. 依赖 libssh 的单元（`ssshandler`/`sftphandler`/工厂分发）由
   `tests/integration` 链接 bundled libssh 覆盖；该工程设置了
   `QMAKE_POST_LINK` 把运行时 DLL 复制到输出目录。

## 4. 测试编写约定

- 槽函数名描述行为：`carriageReturnOverwritesLine`、`partialFrameWaitsForMoreData`。
- 数据驱动优先用 `_data()` 槽 + `QTest::newRow`；同类多组值也可用表驱动数组。
- 信号断言用 `QSignalSpy`；对时序敏感逻辑避免真实等待，直接驱动纯逻辑入口。
- 涉及稳定契约（JSON 格式、帧格式）的测试必须**锁定字节级布局**，
  防止无意识破坏向后兼容。
- 每个公共接口至少一条正向断言 + 一条边界/容错断言（越界、空值、缺字段）。

## 5. 可测试性接口（测试基础设施）

| 接口 | 位置 | 用途 |
|---|---|---|
| `TerminalProxy::processTerminalData(data)` | terminal | 将数据直接注入解析管线（等价远端数据到达），测试不依赖 IPC/真实连接 |
| `TerminalProxy::screenText()/cellAt(r,c)/cursorRow()/cursorColumn()/...` | terminal | 只读状态查询，供断言屏幕内容与光标 |
| `TerminalProxy` 构造不访问单例 | terminal | 主题/字体由 `setTheme`/`setFont` 注入，单测无需初始化 ThemeManager |
| `App::LaunchOptions::parse(args)` | app | 纯函数，直接对参数列表断言 |
| `ConnectionHandler` 接口与工厂分离 | session | 接口可脱离 libssh 单独编译测试 |

## 6. GUI 内置测试工具（TerminalTester）

`-t` 启动或菜单 "Terminal Compatibility Test" 打开的仿真测试器，用于人工验证
VT100/102/220/320、Linux、xterm、SCO-ANSI、ANSI 八种标准的兼容性：

- 序列通过 `processTerminalData` **本地注入**（不污染真实连接）；
- verify 层基于 TerminalProxy 只读状态接口做真实断言（0 基坐标，
  `verifyScrollRegion` 为 1 基的 DECSTBM 语义）；
- 无终端连接时验证返回跳过（PASS），与历史行为一致。

已知差距（测试如实暴露、待功能开发）：`horizontalTab`/`verticalTab`/`formFeed`
信号当前未连接到 TerminalProxy 屏幕模型，对应测试项在有终端时会显示 FAIL。

## 7. 真实环境功能测试（tests/integration）

默认构建即包含该工程，但所有真实设备用例会 `QSKIP`，不会产生外部连接。
需要实机验证时通过环境变量提供目标（凭据不要写进脚本或提交到仓库）：

```powershell
# SSH / SFTP / SCP
$env:NTERMINAL_TEST_SSH_HOST = "<host>"
$env:NTERMINAL_TEST_SSH_PORT = "22"          # 可省略，默认 22
$env:NTERMINAL_TEST_SSH_USER = "<user>"
$env:NTERMINAL_TEST_SSH_PASSWORD = "<password>"

# 串口控制台；波特率可省略，默认 115200
$env:NTERMINAL_TEST_SERIAL_PORT = "COM3"
$env:NTERMINAL_TEST_SERIAL_BAUD = "115200"
$env:NTERMINAL_TEST_SERIAL_USER = "<user>"
$env:NTERMINAL_TEST_SERIAL_PASSWORD = "<password>"

cd build-tests\integration\release
.\tst_integration.exe -v1
```

覆盖内容：

- 工厂为 7 种已实现协议创建处理器，SCP/RDP 明确返回 `nullptr`；
- 不存在的串口返回失败并发出 `errorOccurred`；
- 串口控制台自动处理 `login:` / `password:` 提示（已登录状态也可），
  用算术展开 `echo` 确认命令真正执行，而不是只看到回显；
- SSH 登录、回显、`setTerminalSize`、`systemStatsUpdated`、关闭信号，
  以及错误密码必须被拒绝；
- SFTP 临时目录的创建/上传/下载/列表/改名/删除、字节级内容比对、
  进度回调、下载取消清理、上传前取消不破坏远端已有文件；
- SCP 上传/下载与 SFTP 浏览交叉验证；测试结束由守卫对象清理远端目录，
  中途失败也不会遗留测试文件。

注意：SFTP 用例要求在远端家目录有写权限；SCP 用例要求服务器提供 `scp`。
SSH 用例依赖服务器开放 SFTP 子系统（浏览目录始终使用 SFTP）。

## 8. 无 libssh 环境的编译冒烟（build-verify/）

`build-verify/verify.pro` 以 stub 工厂替换 libssh 依赖单元，可在无 libssh 的机器上
验证其余全部源码的编译链接正确性：

```powershell
cd build-verify; qmake verify.pro; mingw32-make -j8
```

产出 `release/nterminal-verify.exe` 即通过。libssh 就绪后请使用产品工程
`NTerminal.pro` 完整构建。
