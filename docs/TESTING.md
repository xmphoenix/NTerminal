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

当前基线：**6 个模块、78 个用例、全部通过**。

## 2. 测试矩阵（模块 → 被测对象 → 覆盖点）

| 测试目录 | 被测对象 | 覆盖点 |
|---|---|---|
| `tests/app` | `src/app/launchoptions` | 三种进程模式分流、短/长参数、会话优先级、缺值容错 |
| `tests/common` | `src/common` | IPC 帧字节布局（大端）、头解码边界；连接设置默认值、7 类协议字符串契约、串口枚举、JSON 往返、缺省字段、**历史 sessions.json 格式兼容** |
| `tests/ipc` | `src/ipc/ipcprotocol` | 编码重载（字节/JSON/字符串）、完整帧、半包等待、粘包多帧切分、空载荷、垃圾数据安全 |
| `tests/parser` | `src/terminal/terminalparser` | 文本/UTF-8、控制字符、CSI 光标/擦除/编辑/制表位、SGR、私有模式、OSC 标题与剪贴板、字符集、设备查询、跨块序列、DCS 恢复、未知序列安全 |
| `tests/proxy` | `src/terminal/terminalproxy` | 屏幕模型：初始状态、文本写入/CR 覆盖/LF、CUP 语义、ED 清屏、SGR→Cell 属性（16 色/256 色）、备用屏、宽字符、`processTerminalData` 信号 |
| `tests/session` | `src/session/connectionhandler` | 抽象接口契约：生命周期、数据吞吐、信号发射、默认 setTerminalSize 安全性、QObject 所有权 |

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
4. 依赖 libssh 的单元（`ssshandler`/`sftphandler`/工厂分发）不做单元链接，
   由产品完整构建与联调覆盖。

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

## 7. 无 libssh 环境的编译冒烟（build-verify/）

`build-verify/verify.pro` 以 stub 工厂替换 libssh 依赖单元，可在无 libssh 的机器上
验证其余全部源码的编译链接正确性：

```powershell
cd build-verify; qmake verify.pro; mingw32-make -j8
```

产出 `release/nterminal-verify.exe` 即通过。libssh 就绪后请使用产品工程
`NTerminal.pro` 完整构建。
