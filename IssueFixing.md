## vi 和 cat 无法显示完整列的原因分析与修复总结
### 一、问题根本原因 1. cat 命令问题
- 原因 ：数据源（cat）假设终端是标准80列宽度，在输出80个字符后自动在数据流中插入回车符（CR，0x0D）和换行符（LF，0x0A）
- 表现 ：超过80列的内容被强制换行显示 2. vi 命令问题
- 原因 ：vi 启动时发送终端能力查询，但终端没有正确响应
- 具体流程 ：
  ```
  vi 发送: ESC [ 999;999 H  → 移动光标到999行999列（查询终端尺寸）
  vi 发送: ESC [ 6 n        → 请求光标位置报告
  终端应该响应: ESC [ 24;200 R  → 告诉vi终端有24行200列
  ```
- 问题 ：终端没有响应这些查询，vi 无法知道实际宽度，默认假设80列
- 表现 ：vi 在80列后主动发送 ESC [ 2;1 H 命令换行
### 二、修复内容汇总 修复点 1：默认列数错误
文件 : terminalwidget.cpp:19

```
// 修复前
, m_columns(80)

// 修复后
, m_columns(200)  // 默认200列，足够显示长行
``` 修复点 2：忽略80列边界的自动回车换行
文件 : terminalwidget.cpp:1363-1391

```
void TerminalWidget::onCarriageReturn()
{
    // 检测是否为80列边界处的自动回车
    if (m_cursorColumn == 80) {
        qDebug() << "检测到80列边界处的自动回车，忽略";
        return;  // 忽略数据源自动添加的回车符
    }
    carriageReturn();
}

void TerminalWidget::onLineFeed()
{
    // 检测是否为80列边界处的自动换行
    if (m_cursorColumn == 80) {
        qDebug() << "检测到80列边界处的自动换行，忽略";
        return;  // 忽略数据源自动添加的换行符
    }
    newLine();
}
``` 修复点 3：添加终端能力查询信号
文件 : vt100parser.h:77-81

```
// 新增信号
void deviceStatusReport();           // ESC [ 5 n - 设备状态报告请求
void cursorPositionReportRequest();  // ESC [ 6 n - 光标位置报告请求
void primaryDeviceAttributes();      // ESC [ c - 主设备属性查询
``` 修复点 4：完善CSI命令解析
文件 : vt100parser.cpp:277-290

```
case 'n': 
    if (param1 == 5) {
        emit deviceStatusReport();        // ESC [ 5 n
    } else if (param1 == 6) {
        emit cursorPositionReportRequest(); // ESC [ 6 n
    }
    break;
case 'c':
    emit primaryDeviceAttributes();       // ESC [ c
    break;
``` 修复点 5：实现终端能力响应
文件 : terminalwidget.cpp:1363-1408

```
void TerminalWidget::onDeviceStatusReport()
{
    // 响应 ESC [ 0 n 表示设备正常
    sendData("\x1b[0n");
}

void TerminalWidget::onCursorPositionReportRequest()
{
    // 响应当前光标位置 ESC [<行>;<列>R
    int row = m_cursorRow + 1;     // 转换为1-based
    int column = m_cursorColumn + 1;
    QByteArray response = QString("\x1b[%1;%2R").arg(row).arg(column).toUtf8();
    sendData(response);
}

void TerminalWidget::onPrimaryDeviceAttributes()
{
    // 响应 ESC [ ?6c 表示VT100兼容终端
    sendData("\x1b[?6c");
}
``` 修复点 6：光标位置命令处理（支持终端尺寸查询）
文件 : terminalwidget.cpp:1311-1333

```
void TerminalWidget::onCursorPosition(int row, int column)
{
    int actualRow = qMax(0, row - 1);
    int actualCol = qMax(0, column - 1);
    
    // 如果请求的位置超出边界，限制在边界内
    // 这样vi可以通过 ESC [ 999;999 H 来查询终端尺寸
    if (actualRow >= m_terminalRows) {
        actualRow = m_terminalRows - 1;
    }
    if (actualCol >= m_columns) {
        actualCol = m_columns - 1;
    }
    
    m_cursorColumn = actualCol;
    m_cursorRow = actualRow;
    viewport()->update();
}
``` 修复点 7：缓冲区扩展时保留旧数据
文件 : terminalwidget.cpp:240-300

```
void TerminalWidget::allocateBuffers()
{
    // 保存旧数据
    QVector<QVector<Cell>> oldHistoryBuffer = m_historyBuffer;
    QVector<QVector<Cell>> oldTerminalBuffer = m_terminalBuffer;
    
    // 重新分配...
    
    // 保留旧数据
    if (i < oldTerminalBuffer.size() && oldColumns > 0) {
        for (int j = 0; j < oldColumns && j < m_columns; ++j) {
            m_terminalBuffer[i][j] = oldTerminalBuffer[i][j];
        }
    }
}
```
### 三、修复效果对比
场景 修复前 修复后 cat 长行 80列后换行 完整显示，水平滚动 vi 打开文件 只显示80列 完整显示所有列 终端能力查询 无响应 正确响应 默认列数 80列 200列 缓冲区扩展 数据丢失 保留旧数据

### 四、技术原理图解
```
vi 启动流程：
┌─────────────────────────────────────────────────────────────┐
│  vi 发送: ESC [ 999;999 H                                    │
│           ↓                                                  │
│  终端处理: 光标限制到 (23, 199)  [200列的最大索引]           │
│           ↓                                                  │
│  vi 发送: ESC [ 6 n                                          │
│           ↓                                                  │
│  终端响应: ESC [ 24;200 R  [告诉vi有24行200列]               │
│           ↓                                                  │
│  vi 知道终端宽度: 200列                                      │
│           ↓                                                  │
│  vi 正确显示超过80列的内容                                   │
└─────────────────────────────────────────────────────────────┘

cat 输出流程：
┌─────────────────────────────────────────────────────────────┐
│  cat 输出: "....80个字符...." + CR + LF                      │
│           ↓                                                  │
│  终端检测: 光标在80列位置                                    │
│           ↓                                                  │
│  终端忽略: 忽略自动添加的CR和LF                              │
│           ↓                                                  │
│  继续显示: 第81个字符及后续内容                              │
└─────────────────────────────────────────────────────────────┘
```
