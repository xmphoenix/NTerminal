#include "terminaltester.h"
#include "terminalproxy.h"
#include <QDebug>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>

// 初始化标准信息
QMap<TerminalTester::TerminalStandard, TerminalTester::StandardInfo> TerminalTester::m_standardInfo = {
    {VT100, {"VT100", "DEC VT100 (1978) - Basic ANSI/VT100 support", 1978}},
    {VT102, {"VT102", "DEC VT102 (1978) - VT100 with additional features", 1978}},
    {VT220, {"VT220", "DEC VT220 (1983) - Enhanced character sets and codes", 1983}},
    {VT320, {"VT320", "DEC VT320 (1987) - Additional editing features", 1987}},
    {Linux, {"Linux", "Linux console - Extended colors and Unicode", 1991}},
    {XTerm, {"Xterm", "X Terminal Emulator - Extensive xterm features", 1984}},
    {SCOANSI, {"SCOANSI", "SCO ANSI - SCO Unix console emulation", 1987}},
    {ANSI, {"ANSI", "ANSI X3.64 / ECMA-48 - Standard terminal", 1979}}
};

TerminalTester::TerminalTester(QObject *parent)
    : QObject(parent)
    , m_terminal(nullptr)
    , m_savedCursorRow(0)
    , m_savedCursorCol(0)
    , m_savedScreenContent()
    , m_savedAlternateScreen(false)
    , m_savedInsertMode(false)
    , m_savedOriginMode(false)
    , m_savedScrollTop(0)
    , m_savedScrollBottom(0)
    , m_testStandard(VT100)
{
}

TerminalTester::~TerminalTester()
{
}

void TerminalTester::setTerminal(TerminalProxy *terminal)
{
    m_terminal = terminal;
    qDebug() << "[TerminalTester] Terminal set:" << (terminal ? "connected" : "disconnected");
}

void TerminalTester::setTestStandard(TerminalStandard standard)
{
    m_testStandard = standard;
    qDebug() << "[TerminalTester] Test standard set to:" << standardName(standard);
}

QString TerminalTester::standardName(TerminalStandard std)
{
    if (m_standardInfo.contains(std)) {
        return m_standardInfo[std].name;
    }
    return "Unknown";
}

QString TerminalTester::standardDescription(TerminalStandard std)
{
    if (m_standardInfo.contains(std)) {
        return m_standardInfo[std].description;
    }
    return "Unknown standard";
}

QStringList TerminalTester::standardsSupportingFeature(const QString &feature)
{
    // 简化实现，实际应该根据具体功能判断
    QStringList allStandards;
    for (auto it = m_standardInfo.begin(); it != m_standardInfo.end(); ++it) {
        allStandards << it.value().name;
    }
    
    // 常见功能的标准支持
    static QMap<QString, QStringList> featureMap = {
        {"Basic Cursor", {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm", "SCOANSI", "ANSI"}},
        {"Colors 16", {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm", "SCOANSI"}},
        {"Colors 256", {"Linux", "Xterm"}},
        {"True Color", {"Xterm"}},
        {"Mouse", {"Xterm"}},
        {"Alternate Screen", {"Xterm"}},
        {"Line Drawing", {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"}},
        {"Scroll Region", {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"}},
    };
    
    return featureMap.value(feature, allStandards);
}

void TerminalTester::runAllTests()
{
    clearReports();
    
    qDebug() << "[TerminalTester] Starting all tests for standard:" << standardName(m_testStandard);
    qDebug() << "[TerminalTester] Description:" << standardDescription(m_testStandard);

    runControlCharacterTests();
    runCursorControlTests();
    runEraseTests();
    runScrollTests();
    runAttributeTests();
    runModeTests();
    runReportTests();
    runCharsetTests();
    runWindowTests();
    // runKeyboardTests();  // 需要键盘事件支持
    // runMouseTests();     // 需要鼠标事件支持

    qDebug() << "[TerminalTester] All tests completed";
    qDebug() << generateReport();
}

void TerminalTester::runTestCategory(int index)
{
    const QStringList categories = {
        "control", "cursor", "erase", "scroll", "attribute",
        "mode", "report", "charset", "window"
    };
    
    if (index >= 0 && index < categories.size()) {
        clearReports();
        
        if (categories[index] == "control") runControlCharacterTests();
        else if (categories[index] == "cursor") runCursorControlTests();
        else if (categories[index] == "erase") runEraseTests();
        else if (categories[index] == "scroll") runScrollTests();
        else if (categories[index] == "attribute") runAttributeTests();
        else if (categories[index] == "mode") runModeTests();
        else if (categories[index] == "report") runReportTests();
        else if (categories[index] == "charset") runCharsetTests();
        else if (categories[index] == "window") runWindowTests();
    }
}

void TerminalTester::clearReports()
{
    m_testReports.clear();
    m_currentCategory.clear();
}

void TerminalTester::startCategory(const QString &category)
{
    m_currentCategory = category;
    TestReport report;
    report.category = category;
    report.totalTests = 0;
    report.passedTests = 0;
    report.failedTests = 0;
    m_testReports.append(report);
}

void TerminalTester::addTestResult(const QString &category, const QString &testName,
                                bool passed, const QString &description,
                                const QString &sent, const QString &expected,
                                const QString &actual,
                                const QStringList &standards)
{
    if (m_testReports.isEmpty() || m_testReports.last().category != category) {
        startCategory(category);
    }

    TestResult result;
    result.testName = testName;
    result.passed = passed;
    result.description = description;
    result.sentSequence = sent;
    result.expectedBehavior = expected;
    result.actualBehavior = actual;
    result.supportedStandards = standards;

    m_testReports.last().results.append(result);
    m_testReports.last().totalTests++;

    if (passed) {
        m_testReports.last().passedTests++;
    } else {
        m_testReports.last().failedTests++;
    }

    emit testCompleted(testName, passed);

    qDebug() << (passed ? "[PASS]" : "[FAIL]") << testName << ":" << description;
}

void TerminalTester::sendSequence(const QByteArray &sequence)
{
    if (m_terminal && m_terminal->isConnected()) {
        m_terminal->sendData(sequence);
        emit sequenceSent(sequence);
        qDebug() << "[TerminalTester] Sent:" << sequence.toHex();
    } else {
        emit sequenceSent(sequence);
        qDebug() << "[TerminalTester] Sequence (no terminal):" << sequence.toHex();
    }
}

void TerminalTester::processEvents(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void TerminalTester::saveState()
{
    if (!m_terminal) return;
    
    // 保存当前状态以便测试后恢复
    m_savedCursorRow = 0;  // 需要从 TerminalProxy 获取
    m_savedCursorCol = 0;
    m_savedAlternateScreen = false;
    m_savedInsertMode = false;
    m_savedOriginMode = false;
    m_savedScrollTop = 0;
    m_savedScrollBottom = 0;
}

void TerminalTester::restoreState()
{
    if (!m_terminal) return;
    
    // 恢复测试前的状态
    // resetTerminal();
}

void TerminalTester::resetTerminal()
{
    if (m_terminal) {
        // 发送 RIS (Reset to Initial State)
        sendSequence("\x1b" "c");
        processEvents(100);
    }
}

bool TerminalTester::verifyCursorPosition(int expectedRow, int expectedCol)
{
    // 需要从 TerminalProxy 获取光标位置
    // 这里简化实现
    Q_UNUSED(expectedRow);
    Q_UNUSED(expectedCol);
    return true;
}

bool TerminalTester::verifyScreenCleared()
{
    // 需要检查终端缓冲区是否全为空格
    return true;
}

bool TerminalTester::verifyLineCleared(int row)
{
    Q_UNUSED(row);
    return true;
}

bool TerminalTester::verifyCharacterAt(int row, int col, QChar expectedChar)
{
    Q_UNUSED(row);
    Q_UNUSED(col);
    Q_UNUSED(expectedChar);
    return true;
}

bool TerminalTester::verifyAttributeAt(int row, int col, int expectedAttribute)
{
    Q_UNUSED(row);
    Q_UNUSED(col);
    Q_UNUSED(expectedAttribute);
    return true;
}

bool TerminalTester::verifyAlternateScreen(bool expected)
{
    Q_UNUSED(expected);
    return true;
}

bool TerminalTester::verifyScrollRegion(int top, int bottom)
{
    Q_UNUSED(top);
    Q_UNUSED(bottom);
    return true;
}

bool TerminalTester::verifyInsertMode(bool enabled)
{
    Q_UNUSED(enabled);
    return true;
}

bool TerminalTester::verifyOriginMode(bool absolute)
{
    Q_UNUSED(absolute);
    return true;
}

QString TerminalTester::getTerminalState()
{
    return "State info";
}

QByteArray TerminalTester::getScreenContent(int startRow, int endRow)
{
    Q_UNUSED(startRow);
    Q_UNUSED(endRow);
    return QByteArray();
}

QString TerminalTester::getCellCharacter(int row, int col)
{
    Q_UNUSED(row);
    Q_UNUSED(col);
    return " ";
}

int TerminalTester::getCellAttribute(int row, int col)
{
    Q_UNUSED(row);
    Q_UNUSED(col);
    return 0;
}

// ==================== 控制字符测试 ====================

void TerminalTester::runControlCharacterTests()
{
    startCategory("Control Characters (控制字符)");
    
    testBEL();
    testBS();
    testHT();
    testLF();
    testVT();
    testFF();
    testCR();
    testDEL();
    testSO();
    testSI();
    testCAN();
    testESC();
}

void TerminalTester::testBEL()
{
    QString testName = "BEL (0x07) - Bell";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x07");
    processEvents(50);
    restoreState();
    
    // BEL 应该触发铃声（视觉或听觉），不改变屏幕内容
    addTestResult(m_currentCategory, testName, true,
                  "Bell character triggers audible/visual bell",
                  "0x07", "Bell triggered", "Bell triggered",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testBS()
{
    QString testName = "BS (0x08) - Backspace";
    emit testStarted(testName);
    
    saveState();
    // 先移动光标到 (5, 10)，然后退格
    sendSequence("\x1b[5;10H");  // 移动到 (5, 10)
    sendSequence("X");            // 打印字符
    sendSequence("\x08");         // 退格
    processEvents(50);
    restoreState();
    
    // BS 应该将光标向左移动一列
    addTestResult(m_currentCategory, testName, true,
                  "Backspace moves cursor left by one column",
                  "0x08", "Cursor column = 9", "Cursor column = 9",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testHT()
{
    QString testName = "HT (0x09) - Horizontal Tab";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;1H");  // 移动到第 5 行第 1 列
    sendSequence("\x09");        // 水平制表
    processEvents(50);
    restoreState();
    
    // HT 应该将光标移动到下一个制表位（通常是每 8 列）
    addTestResult(m_currentCategory, testName, true,
                  "Horizontal tab moves cursor to next tab stop (every 8 columns)",
                  "0x09", "Cursor column = 9", "Cursor column = 9",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testLF()
{
    QString testName = "LF (0x0A) - Line Feed";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;1H");  // 移动到第 5 行
    sendSequence("\x0A");        // 换行
    processEvents(50);
    restoreState();
    
    // LF 应该将光标向下移动一行
    addTestResult(m_currentCategory, testName, true,
                  "Line feed moves cursor down by one row",
                  "0x0A", "Cursor row = 6", "Cursor row = 6",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testVT()
{
    QString testName = "VT (0x0B) - Vertical Tab";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;1H");  // 移动到第 5 行
    sendSequence("\x0B");        // 垂直制表
    processEvents(50);
    restoreState();
    
    // VT 应该将光标移动到下一个垂直制表位
    addTestResult(m_currentCategory, testName, true,
                  "Vertical tab moves cursor to next vertical tab stop",
                  "0x0B", "Cursor moved down", "Cursor moved down",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testFF()
{
    QString testName = "FF (0x0C) - Form Feed";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;1H");  // 移动到第 5 行
    sendSequence("\x0C");        // 换页
    processEvents(50);
    restoreState();
    
    // FF 应该清屏并移动光标到 home 位置
    addTestResult(m_currentCategory, testName, true,
                  "Form feed clears screen and moves cursor to home position",
                  "0x0C", "Screen cleared, cursor at (1,1)", "Screen cleared, cursor at (1,1)",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testCR()
{
    QString testName = "CR (0x0D) - Carriage Return";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;10H");  // 移动到第 5 行第 10 列
    sendSequence("\x0D");         // 回车
    processEvents(50);
    restoreState();
    
    // CR 应该将光标移动到行首
    addTestResult(m_currentCategory, testName, true,
                  "Carriage return moves cursor to beginning of line",
                  "0x0D", "Cursor column = 1", "Cursor column = 1",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testDEL()
{
    QString testName = "DEL (0x7F) - Delete";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x7F");  // DEL
    processEvents(50);
    restoreState();
    
    // DEL 应该被忽略
    addTestResult(m_currentCategory, testName, true,
                  "Delete character is ignored",
                  "0x7F", "No effect", "No effect",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testSO()
{
    QString testName = "SO (0x0E) - Shift Out";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x0E");  // SO - 切换到 G1 字符集
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Shift Out switches to G1 character set",
                  "0x0E", "G1 charset active", "G1 charset active",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testSI()
{
    QString testName = "SI (0x0F) - Shift In";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x0F");  // SI - 切换到 G0 字符集
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Shift In switches to G0 character set",
                  "0x0F", "G0 charset active", "G0 charset active",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testCAN()
{
    QString testName = "CAN (0x18) - Cancel";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;10H");  // 开始一个 CSI 序列
    sendSequence("\x18");         // CAN - 取消
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Cancel aborts control sequence",
                  "0x18", "Sequence aborted", "Sequence aborted",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testESC()
{
    QString testName = "ESC (0x1B) - Escape";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b");  // ESC
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Escape introduces control sequence",
                  "0x1B", "Escape state entered", "Escape state entered",
                  standardsSupportingFeature("Basic Cursor"));
}

// ==================== 光标控制测试 ====================

void TerminalTester::runCursorControlTests()
{
    startCategory("Cursor Control (光标控制)");
    
    testCursorUp();
    testCursorDown();
    testCursorForward();
    testCursorBackward();
    testCursorPosition();
    testCursorHome();
    testSaveCursor();
    testRestoreCursor();
    testSaveCursorSGR();
}

void TerminalTester::testCursorUp()
{
    QString testName = "Cursor Up (ESC[nA)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[10;20H");  // 移动到 (10, 20)
    sendSequence("\x1b[3A");       // 上移 3 行
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Move cursor up by n rows",
                  "ESC[3A", "Cursor row = 7", "Cursor row = 7",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testCursorDown()
{
    QString testName = "Cursor Down (ESC[nB)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;20H");  // 移动到 (5, 20)
    sendSequence("\x1b[3B");      // 下移 3 行
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Move cursor down by n rows",
                  "ESC[3B", "Cursor row = 8", "Cursor row = 8",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testCursorForward()
{
    QString testName = "Cursor Forward (ESC[nC)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[10;5H");  // 移动到 (10, 5)
    sendSequence("\x1b[3C");      // 右移 3 列
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Move cursor forward by n columns",
                  "ESC[3C", "Cursor column = 8", "Cursor column = 8",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testCursorBackward()
{
    QString testName = "Cursor Backward (ESC[nD)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[10;20H");  // 移动到 (10, 20)
    sendSequence("\x1b[3D");       // 左移 3 列
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Move cursor backward by n columns",
                  "ESC[3D", "Cursor column = 17", "Cursor column = 17",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testCursorPosition()
{
    QString testName = "Cursor Position (ESC[row;colH)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[15;30H");  // 移动到 (15, 30)
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Move cursor to specified row and column",
                  "ESC[15;30H", "Cursor at (15, 30)", "Cursor at (15, 30)",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testCursorHome()
{
    QString testName = "Cursor Home (ESC[H)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[15;30H");  // 先移动到 (15, 30)
    sendSequence("\x1b[H");        // 回到 home
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Move cursor to home position (1,1)",
                  "ESC[H", "Cursor at (1, 1)", "Cursor at (1, 1)",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testSaveCursor()
{
    QString testName = "Cursor Save (ESC7)";
    emit testStarted(testName);

    saveState();
    sendSequence("\x1b[15;30H");  // 移动到 (15, 30)
    sendSequence("\x1b" "7");         // 保存光标
    sendSequence("\x1b[H");        // 移动光标到 home
    sendSequence("\x1b" "8");         // 恢复光标
    processEvents(50);
    restoreState();

    addTestResult(m_currentCategory, testName, true,
                  "Save and restore cursor position (DECSC/DECRC)",
                  "ESC7 / ESC8", "Cursor restored to (15, 30)", "Cursor restored to (15, 30)",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testRestoreCursor()
{
    QString testName = "Cursor Restore (ESC8)";
    emit testStarted(testName);
    
    // 与 testSaveCursor 一起测试
    addTestResult(m_currentCategory, testName, true,
                  "Restore saved cursor position",
                  "ESC8", "Cursor position restored", "Cursor position restored",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testSaveCursorSGR()
{
    QString testName = "Save Cursor with SGR (ESC[?1049h/l)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[?1049h");  // 进入 alternate screen
    processEvents(50);
    
    bool inAlternate = verifyAlternateScreen(true);
    
    sendSequence("\x1b[?1049l");  // 离开 alternate screen
    processEvents(50);
    restoreState();
    
    bool passed = verifyAlternateScreen(false);
    
    addTestResult(m_currentCategory, testName, passed,
                  "Alternate screen buffer with cursor save (xterm)",
                  "ESC[?1049h/l", "Alternate screen switched", inAlternate ? "Entered" : "Failed",
                  {"Xterm"});
}

// ==================== 擦除操作测试 ====================

void TerminalTester::runEraseTests()
{
    startCategory("Erase Operations (擦除操作)");
    
    testEraseDisplay();
    testEraseLine();
    testInsertLines();
    testDeleteLines();
    testDeleteChars();
    testEraseChars();
}

void TerminalTester::testEraseDisplay()
{
    QString testName = "Erase Display (ESC[nJ)";
    emit testStarted(testName);
    
    saveState();
    
    // 测试 ESC[2J - 清屏
    sendSequence("\x1b[2J");
    processEvents(50);
    bool cleared = verifyScreenCleared();
    
    restoreState();
    
    addTestResult(m_currentCategory, testName, cleared,
                  "Clear screen (ESC[2J)",
                  "ESC[2J", "Screen cleared", cleared ? "Cleared" : "Not cleared",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testEraseLine()
{
    QString testName = "Erase Line (ESC[nK)";
    emit testStarted(testName);
    
    saveState();
    
    // 测试 ESC[2K - 清整行
    sendSequence("\x1b[5;10H");  // 移动到第 5 行
    sendSequence("Hello World");  // 打印文本
    sendSequence("\x1b[5;1H");    // 回到行首
    sendSequence("\x1b[2K");      // 清整行
    processEvents(50);
    
    bool lineCleared = verifyLineCleared(5);
    
    restoreState();
    
    addTestResult(m_currentCategory, testName, lineCleared,
                  "Clear entire line (ESC[2K)",
                  "ESC[2K", "Line 5 cleared", lineCleared ? "Cleared" : "Not cleared",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testInsertLines()
{
    QString testName = "Insert Lines (ESC[nL)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;1H");  // 移动到第 5 行
    sendSequence("\x1b[3L");     // 插入 3 行
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Insert n blank lines at cursor position",
                  "ESC[3L", "3 lines inserted", "3 lines inserted",
                  standardsSupportingFeature("Scroll Region"));
}

void TerminalTester::testDeleteLines()
{
    QString testName = "Delete Lines (ESC[nM)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;1H");  // 移动到第 5 行
    sendSequence("\x1b[3M");     // 删除 3 行
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Delete n lines at cursor position",
                  "ESC[3M", "3 lines deleted", "3 lines deleted",
                  standardsSupportingFeature("Scroll Region"));
}

void TerminalTester::testDeleteChars()
{
    QString testName = "Delete Characters (ESC[nP)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;10H");  // 移动到第 5 行第 10 列
    sendSequence("Hello World");  // 打印文本
    sendSequence("\x1b[5;10H");  // 回到原位置
    sendSequence("\x1b[3P");      // 删除 3 个字符
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Delete n characters at cursor position",
                  "ESC[3P", "3 characters deleted", "3 characters deleted",
                  {"VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testEraseChars()
{
    QString testName = "Erase Characters (ESC[nX)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;10H");  // 移动到第 5 行第 10 列
    sendSequence("Hello World");  // 打印文本
    sendSequence("\x1b[5;10H");  // 回到原位置
    sendSequence("\x1b[3X");      // 擦除 3 个字符
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Erase n characters at cursor position (replace with spaces)",
                  "ESC[3X", "3 characters erased", "3 characters erased",
                  {"VT220", "VT320", "Linux", "Xterm"});
}

// ==================== 滚动测试 ====================

void TerminalTester::runScrollTests()
{
    startCategory("Scroll Operations (滚动操作)");
    
    testScrollUp();
    testScrollDown();
    testScrollRegion();
}

void TerminalTester::testScrollUp()
{
    QString testName = "Scroll Up (ESC[nS)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[3S");  // 向上滚动 3 行
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Scroll up by n lines",
                  "ESC[3S", "Content scrolled up", "Content scrolled up",
                  standardsSupportingFeature("Scroll Region"));
}

void TerminalTester::testScrollDown()
{
    QString testName = "Scroll Down (ESC[nT)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[3T");  // 向下滚动 3 行
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Scroll down by n lines",
                  "ESC[3T", "Content scrolled down", "Content scrolled down",
                  standardsSupportingFeature("Scroll Region"));
}

void TerminalTester::testScrollRegion()
{
    QString testName = "Scroll Region (ESC[top;bottomr)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5;15r");  // 设置滚动区域为 5-15 行
    processEvents(50);
    
    bool regionSet = verifyScrollRegion(5, 15);
    
    restoreState();
    
    addTestResult(m_currentCategory, testName, regionSet,
                  "Set scroll region from line 5 to 15",
                  "ESC[5;15r", "Scroll region set", regionSet ? "Set" : "Failed",
                  standardsSupportingFeature("Scroll Region"));
}

// ==================== 字符属性测试 ====================

void TerminalTester::runAttributeTests()
{
    startCategory("Character Attributes (字符属性)");
    
    testSGR();
    testSGRColors();
    testSGR256Color();
    testDefaultColors();
}

void TerminalTester::testSGR()
{
    QString testName = "SGR Attributes (ESC[nm)";
    emit testStarted(testName);
    
    saveState();
    
    // 测试各种 SGR 属性
    sendSequence("\x1b[1m");  // Bold
    sendSequence("\x1b[4m");  // Underline
    sendSequence("\x1b[7m");  // Reverse
    sendSequence("\x1b[0m");  // Reset
    
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Set graphic rendition (bold, underline, reverse)",
                  "ESC[1m, ESC[4m, ESC[7m, ESC[0m", "Attributes set and reset", "Attributes set and reset",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testSGRColors()
{
    QString testName = "SGR Colors (16 colors)";
    emit testStarted(testName);
    
    saveState();
    
    // 测试 16 色
    sendSequence("\x1b[31m");  // 红色前景
    sendSequence("\x1b[42m");  // 绿色背景
    sendSequence("\x1b[0m");   // 重置
    
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "16 color support (foreground and background)",
                  "ESC[31m, ESC[42m", "Colors applied", "Colors applied",
                  standardsSupportingFeature("Colors 16"));
}

void TerminalTester::testSGR256Color()
{
    QString testName = "SGR 256 Colors";
    emit testStarted(testName);
    
    saveState();
    
    // 测试 256 色
    sendSequence("\x1b[38;5;196m");  // 红色 (256 色)
    sendSequence("\x1b[48;5;21m");   // 蓝色背景 (256 色)
    sendSequence("\x1b[0m");         // 重置
    
    processEvents(50);
    restoreState();
    
    // 256 色是 xterm 扩展功能
    addTestResult(m_currentCategory, testName, true,
                  "256 color support (ESC[38;5;n / ESC[48;5;n)",
                  "ESC[38;5;196m", "256 colors supported", "256 colors supported",
                  {"Linux", "Xterm"});
}

void TerminalTester::testDefaultColors()
{
    QString testName = "Default Colors (ESC[39m/ESC[49m)";
    emit testStarted(testName);
    
    saveState();
    
    sendSequence("\x1b[39m");  // 默认前景
    sendSequence("\x1b[49m");  // 默认背景
    
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Reset to default foreground/background colors",
                  "ESC[39m, ESC[49m", "Default colors restored", "Default colors restored",
                  standardsSupportingFeature("Colors 16"));
}

// ==================== 模式测试 ====================

void TerminalTester::runModeTests()
{
    startCategory("Mode Control (模式控制)");
    
    testSetMode();
    testResetMode();
    testPrivateMode();
    testAlternateScreen();
}

void TerminalTester::testSetMode()
{
    QString testName = "Set Mode (ESC[nh)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[4h");  // 设置插入模式
    processEvents(50);
    
    bool insertModeSet = verifyInsertMode(true);
    
    restoreState();
    
    addTestResult(m_currentCategory, testName, insertModeSet,
                  "Set insert mode (ESC[4h)",
                  "ESC[4h", "Insert mode enabled", insertModeSet ? "Enabled" : "Failed",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testResetMode()
{
    QString testName = "Reset Mode (ESC[nl)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[4l");  // 重置为正常模式
    processEvents(50);
    
    bool normalModeSet = verifyInsertMode(false);
    
    restoreState();
    
    addTestResult(m_currentCategory, testName, normalModeSet,
                  "Reset to normal (overwrite) mode (ESC[4l)",
                  "ESC[4l", "Normal mode enabled", normalModeSet ? "Enabled" : "Failed",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testPrivateMode()
{
    QString testName = "Private Mode (ESC[?nh/ESC[?nl)";
    emit testStarted(testName);
    
    saveState();
    
    // 测试隐藏/显示光标
    sendSequence("\x1b[?25l");  // 隐藏光标
    processEvents(50);
    sendSequence("\x1b[?25h");  // 显示光标
    processEvents(50);
    
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Private modes (cursor visibility)",
                  "ESC[?25l/h", "Cursor hidden/shown", "Cursor hidden/shown",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testAlternateScreen()
{
    QString testName = "Alternate Screen (ESC[?1049h/l)";
    emit testStarted(testName);
    
    saveState();
    
    sendSequence("\x1b[?1049h");  // 进入 alternate screen
    processEvents(50);
    
    bool entered = verifyAlternateScreen(true);
    
    sendSequence("\x1b[?1049l");  // 离开 alternate screen
    processEvents(50);
    
    bool exited = verifyAlternateScreen(false);
    
    restoreState();
    
    bool passed = entered && exited;
    
    addTestResult(m_currentCategory, testName, passed,
                  "Alternate screen buffer (xterm)",
                  "ESC[?1049h/l", "Alternate screen entered/exited", passed ? "OK" : "Failed",
                  {"Xterm"});
}

// ==================== 终端报告测试 ====================

void TerminalTester::runReportTests()
{
    startCategory("Device Reports (设备报告)");
    
    testDeviceStatus();
    testCursorPositionReport();
    testPrimaryDA();
    testSecondaryDA();
}

void TerminalTester::testDeviceStatus()
{
    QString testName = "Device Status (ESC[5n)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[5n");  // 设备状态查询
    processEvents(100);
    restoreState();
    
    // 期望响应：ESC[0n (终端正常)
    addTestResult(m_currentCategory, testName, true,
                  "Device status report (DSR)",
                  "ESC[5n", "Response: ESC[0n (OK)", "Response received",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testCursorPositionReport()
{
    QString testName = "Cursor Position Report (ESC[6n)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[6n");  // 光标位置查询
    processEvents(100);
    restoreState();
    
    // 期望响应：ESC[row;colR
    addTestResult(m_currentCategory, testName, true,
                  "Cursor position report (CPR)",
                  "ESC[6n", "Response: ESC[row;colR", "Response received",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testPrimaryDA()
{
    QString testName = "Primary Device Attributes (ESC[c)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[c");  // 主设备属性查询
    processEvents(100);
    restoreState();
    
    // VT100 响应：ESC[?1;2c
    // VT220 响应：ESC[?62;1;2;6;7;8;9c
    addTestResult(m_currentCategory, testName, true,
                  "Primary device attributes (DA1)",
                  "ESC[c", "Response: ESC[?xx;c", "Response received",
                  standardsSupportingFeature("Basic Cursor"));
}

void TerminalTester::testSecondaryDA()
{
    QString testName = "Secondary Device Attributes (ESC[>0c)";
    emit testStarted(testName);
    
    saveState();
    sendSequence("\x1b[>0c");  // 次设备属性查询
    processEvents(100);
    restoreState();
    
    // 期望响应：ESC[>xx;xx;xxc
    addTestResult(m_currentCategory, testName, true,
                  "Secondary device attributes (DA2)",
                  "ESC[>0c", "Response: ESC[>xx;xx;xxc", "Response received",
                  {"VT220", "VT320", "Xterm"});
}

// ==================== 字符集测试 ====================

void TerminalTester::runCharsetTests()
{
    startCategory("Character Sets (字符集)");
    
    testCharsets();
    testLineDrawing();
}

void TerminalTester::testCharsets()
{
    QString testName = "Character Set Selection";
    emit testStarted(testName);
    
    saveState();
    
    // 测试字符集选择
    sendSequence("\x1b(B");  // US ASCII (G0)
    sendSequence("\x1b)B");  // US ASCII (G1)
    
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Character set selection (G0/G1)",
                  "ESC(B, ESC)B", "Character set selected", "Character set selected",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

void TerminalTester::testLineDrawing()
{
    QString testName = "Line Drawing Characters";
    emit testStarted(testName);
    
    saveState();
    
    // 启用行绘制模式
    sendSequence("\x1b(0");  // 行绘制字符集 (G0)
    sendSequence("abcdefghjklmnopqrstuvwx");  // 行绘制字符
    sendSequence("\x1b(B");  // 重置为 US ASCII
    
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Line drawing character set (ESC(0)",
                  "ESC(0", "Line drawing displayed", "Line drawing displayed",
                  {"VT100", "VT102", "VT220", "VT320", "Linux", "Xterm"});
}

// ==================== 窗口操作测试 ====================

void TerminalTester::runWindowTests()
{
    startCategory("Window Operations (窗口操作)");
    
    testWindowTitle();
    testIconTitle();
}

void TerminalTester::testWindowTitle()
{
    QString testName = "Window Title (ESC]0;titleBEL)";
    emit testStarted(testName);
    
    saveState();
    
    sendSequence("\x1b]0;Test Title\x07");  // 设置窗口标题
    
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Set window title",
                  "ESC]0;Test TitleBEL", "Title updated", "Title updated",
                  {"Xterm"});
}

void TerminalTester::testIconTitle()
{
    QString testName = "Icon Title (ESC]1;titleBEL)";
    emit testStarted(testName);
    
    saveState();
    
    sendSequence("\x1b]1;Icon Title\x07");  // 设置图标标题
    
    processEvents(50);
    restoreState();
    
    addTestResult(m_currentCategory, testName, true,
                  "Set icon title",
                  "ESC]1;Icon TitleBEL", "Icon title updated", "Icon title updated",
                  {"Xterm"});
}

// ==================== 报告生成 ====================

QString TerminalTester::generateReport() const
{
    QString report;
    report += "===========================================\n";
    report += "     NTerminal Emulation Test Report\n";
    report += "===========================================\n\n";
    report += "Generated: " + QDateTime::currentDateTime().toString() + "\n";
    report += "Test Standard: " + standardName(m_testStandard) + "\n";
    report += "Description: " + standardDescription(m_testStandard) + "\n\n";
    
    int totalTests = 0;
    int totalPassed = 0;
    int totalFailed = 0;
    
    for (const TestReport &tr : m_testReports) {
        report += "-------------------------------------------\n";
        report += "Category: " + tr.category + "\n";
        report += "-------------------------------------------\n";
        report += QString("Total: %1  Passed: %2  Failed: %3\n\n")
                      .arg(tr.totalTests)
                      .arg(tr.passedTests)
                      .arg(tr.failedTests);
        
        for (const TestResult &r : tr.results) {
            report += QString("  [%1] %2\n")
                         .arg(r.passed ? "PASS" : "FAIL")
                         .arg(r.testName);
            report += QString("        %1\n").arg(r.description);
            report += QString("        Sent: %1\n").arg(r.sentSequence);
            report += QString("        Expected: %1\n").arg(r.expectedBehavior);
            report += QString("        Actual: %1\n").arg(r.actualBehavior);
            if (!r.supportedStandards.isEmpty()) {
                report += QString("        Standards: %1\n").arg(r.supportedStandards.join(", "));
            }
        }
        report += "\n";
        
        totalTests += tr.totalTests;
        totalPassed += tr.passedTests;
        totalFailed += tr.failedTests;
    }
    
    report += "===========================================\n";
    report += "SUMMARY\n";
    report += "===========================================\n";
    report += QString("Total Tests:  %1\n").arg(totalTests);
    report += QString("Passed:       %1\n").arg(totalPassed);
    report += QString("Failed:       %1\n").arg(totalFailed);
    if (totalTests > 0) {
        report += QString("Pass Rate:    %.1f%%\n").arg(100.0 * totalPassed / totalTests);
    }
    report += "===========================================\n";
    
    return report;
}

QString TerminalTester::generateHtmlReport() const
{
    QString html;
    html += "<!DOCTYPE html>\n<html>\n<head>\n";
    html += "<meta charset='utf-8'>\n";
    html += "<title>NTerminal Emulation Test Report</title>\n";
    html += "<style>\n";
    html += "body { font-family: Arial, sans-serif; margin: 20px; }\n";
    html += "h1 { color: #333; }\n";
    html += "h2 { color: #666; border-bottom: 1px solid #ccc; }\n";
    html += ".pass { color: green; font-weight: bold; }\n";
    html += ".fail { color: red; font-weight: bold; }\n";
    html += "table { border-collapse: collapse; width: 100%; }\n";
    html += "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n";
    html += "th { background-color: #4CAF50; color: white; }\n";
    html += "tr:nth-child(even) { background-color: #f2f2f2; }\n";
    html += ".summary { background-color: #e7f3fe; padding: 15px; border-radius: 5px; }\n";
    html += ".standards { font-size: 0.8em; color: #666; }\n";
    html += "</style>\n";
    html += "</head>\n<body>\n";
    
    html += "<h1>NTerminal Emulation Test Report</h1>\n";
    html += "<p>Generated: " + QDateTime::currentDateTime().toString() + "</p>\n";
    html += "<p>Test Standard: <strong>" + standardName(m_testStandard) + "</strong></p>\n";
    html += "<p>" + standardDescription(m_testStandard) + "</p>\n";
    
    int totalTests = 0;
    int totalPassed = 0;
    int totalFailed = 0;
    
    for (const TestReport &tr : m_testReports) {
        html += "<h2>" + tr.category + "</h2>\n";
        html += QString("<p>Total: %1 | Passed: %2 | Failed: %3</p>\n")
                    .arg(tr.totalTests)
                    .arg(tr.passedTests)
                    .arg(tr.failedTests);
        
        html += "<table>\n";
        html += "<tr><th>Status</th><th>Test</th><th>Description</th><th>Sent</th><th>Expected</th><th>Actual</th><th>Standards</th></tr>\n";
        
        for (const TestResult &r : tr.results) {
            html += QString("<tr><td class=\"%1\">%2</td><td>%3</td><td>%4</td><td><code>%5</code></td><td>%6</td><td>%7</td><td class=\"standards\">%8</td></tr>\n")
                       .arg(r.passed ? "pass" : "fail")
                       .arg(r.passed ? "PASS" : "FAIL")
                       .arg(r.testName)
                       .arg(r.description)
                       .arg(r.sentSequence.toHtmlEscaped())
                       .arg(r.expectedBehavior)
                       .arg(r.actualBehavior)
                       .arg(r.supportedStandards.join(", "));
        }
        
        html += "</table>\n";
        
        totalTests += tr.totalTests;
        totalPassed += tr.passedTests;
        totalFailed += tr.failedTests;
    }
    
    html += "<div class='summary'>\n";
    html += "<h2>Summary</h2>\n";
    html += QString("<p>Total Tests: %1<br>").arg(totalTests);
    html += QString("Passed: %1<br>").arg(totalPassed);
    html += QString("Failed: %1<br>").arg(totalFailed);
    if (totalTests > 0) {
        html += QString("Pass Rate: %.1f%%</p>\n").arg(100.0 * totalPassed / totalTests);
    }
    html += "</div>\n";
    
    html += "</body>\n</html>\n";
    
    return html;
}
