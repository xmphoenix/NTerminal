#ifndef VT100TESTER_H
#define VT100TESTER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>

class TerminalProxy;

/**
 * @brief 多终端标准兼容性测试器
 * 
 * 测试 NTerminal 程序本身的终端仿真能力，验证是否符合以下标准：
 * 
 * DEC VT 系列:
 * - VT100 (1978)
 * - VT102 (1978)
 * - VT220 (1983)
 * - VT320 (1987)
 * 
 * 其他标准:
 * - Linux console
 * - xterm (X Terminal Emulator)
 * - scoansi (SCO ANSI)
 * - ANSI X3.64 / ECMA-48
 * 
 * 参考文档:
 * - https://invisible-island.net/vttest/vttest.html
 * - https://vt100.net/docs/vt100-ug/chapter3.html
 * - https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - https://en.wikipedia.org/wiki/ANSI_escape_code
 */
class VT100Tester : public QObject
{
    Q_OBJECT

public:
    enum TerminalStandard {
        VT100,
        VT102,
        VT220,
        VT320,
        Linux,
        XTerm,
        SCOANSI,
        ANSI
    };

    struct TestResult {
        QString testName;
        bool passed;
        QString description;
        QString sentSequence;
        QString expectedBehavior;
        QString actualBehavior;
        QStringList supportedStandards;  // 支持该功能的标准
    };

    struct TestReport {
        QString category;
        int totalTests;
        int passedTests;
        int failedTests;
        QVector<TestResult> results;
    };

    struct StandardInfo {
        QString name;
        QString description;
        int year;
    };

    explicit VT100Tester(QObject *parent = nullptr);
    ~VT100Tester();

    // 设置要测试的终端代理
    void setTerminal(TerminalProxy *terminal);
    TerminalProxy* terminal() const { return m_terminal; }

    // 设置测试的终端标准
    void setTestStandard(TerminalStandard standard);
    TerminalStandard testStandard() const { return m_testStandard; }

    // 运行所有测试
    void runAllTests();

    // 按类别运行测试
    void runControlCharacterTests();      // 控制字符测试
    void runCursorControlTests();         // 光标控制测试
    void runEraseTests();                 // 擦除操作测试
    void runScrollTests();                // 滚动测试
    void runAttributeTests();             // 字符属性测试
    void runModeTests();                  // 模式设置测试
    void runReportTests();                // 终端报告测试
    void runCharsetTests();               // 字符集测试
    void runWindowTests();                // 窗口操作测试
    void runKeyboardTests();              // 键盘映射测试
    void runMouseTests();                 // 鼠标支持测试

    // 获取测试报告
    QVector<TestReport> getTestReports() const { return m_testReports; }

    // 生成测试报告（文本格式）
    QString generateReport() const;

    // 生成 HTML 报告
    QString generateHtmlReport() const;

    // 获取终端标准信息
    static QString standardName(TerminalStandard std);
    static QString standardDescription(TerminalStandard std);
    static QStringList standardsSupportingFeature(const QString &feature);

signals:
    void testStarted(const QString &testName);
    void testCompleted(const QString &testName, bool passed);
    void sequenceSent(const QByteArray &sequence);
    void behaviorVerified(const QString &behavior);
    void standardDetected(TerminalStandard standard, int confidence);

public slots:
    // 运行单个测试类别
    void runTestCategory(int index);

private:
    // 测试辅助函数
    void addTestResult(const QString &category, const QString &testName, 
                       bool passed, const QString &description,
                       const QString &sent = QString(), 
                       const QString &expected = QString(), 
                       const QString &actual = QString(),
                       const QStringList &standards = QStringList());
    
    void clearReports();
    void startCategory(const QString &category);
    
    // 发送序列到终端并等待处理
    void sendSequence(const QByteArray &sequence);
    void processEvents(int ms = 100);
    
    // 验证终端状态
    bool verifyCursorPosition(int expectedRow, int expectedCol);
    bool verifyScreenCleared();
    bool verifyLineCleared(int row);
    bool verifyCharacterAt(int row, int col, QChar expectedChar);
    bool verifyAttributeAt(int row, int col, int expectedAttribute);
    bool verifyAlternateScreen(bool expected);
    bool verifyScrollRegion(int top, int bottom);
    bool verifyInsertMode(bool enabled);
    bool verifyOriginMode(bool absolute);
    
    // 获取终端状态
    QString getTerminalState();
    QByteArray getScreenContent(int startRow, int endRow);
    QString getCellCharacter(int row, int col);
    int getCellAttribute(int row, int col);
    
    // 保存和恢复状态
    void saveState();
    void restoreState();
    void resetTerminal();

    // ==================== 控制字符测试 ====================
    void testBEL();     // Bell (0x07) - 所有标准
    void testBS();      // Backspace (0x08) - 所有标准
    void testHT();      // Horizontal Tab (0x09) - 所有标准
    void testLF();      // Line Feed (0x0A) - 所有标准
    void testVT();      // Vertical Tab (0x0B) - VT100+
    void testFF();      // Form Feed (0x0C) - VT100+
    void testCR();      // Carriage Return (0x0D) - 所有标准
    void testDEL();     // Delete (0x7F) - 所有标准
    void testSO();      // Shift Out (0x0E) - VT100+
    void testSI();      // Shift In (0x0F) - VT100+
    void testCAN();     // Cancel (0x18) - VT100+
    void testSUB();     // Substitute (0x1A) - VT100+
    void testESC();     // Escape (0x1B) - 所有标准
    void testFS();      // File Separator (0x1C)
    void testGS();      // Group Separator (0x1D)
    void testRS();      // Record Separator (0x1E)
    void testUS();      // Unit Separator (0x1F)

    // ==================== 光标控制测试 ====================
    void testCursorUp();        // ESC [ n A - VT100+
    void testCursorDown();      // ESC [ n B - VT100+
    void testCursorForward();   // ESC [ n C - VT100+
    void testCursorBackward();  // ESC [ n D - VT100+
    void testCursorNextLine();  // ESC [ n E - VT100+
    void testCursorPrevLine();  // ESC [ n F - VT100+
    void testCursorColumn();    // ESC [ n G - VT100+
    void testCursorPosition();  // ESC [ row ; col H - VT100+
    void testCursorHome();      // ESC [ H - VT100+
    void testSaveCursor();      // ESC 7 / DECSC - VT100+
    void testRestoreCursor();   // ESC 8 / DECRC - VT100+
    void testSaveCursorXterm(); // ESC [ s - xterm
    void testRestoreCursorXterm();// ESC [ u - xterm
    void testSaveCursorSGR();   // ESC [ ? 1049 h/l - xterm

    // ==================== 擦除操作测试 ====================
    void testEraseDisplay();    // ESC [ n J - VT100+
    void testEraseLine();       // ESC [ n K - VT100+
    void testInsertLines();     // ESC [ n L - VT100+
    void testDeleteLines();     // ESC [ n M - VT100+
    void testDeleteChars();     // ESC [ n P - VT220+
    void testEraseChars();      // ESC [ n X - VT220+
    void testInsertChars();     // ESC [ n @ - VT220+

    // ==================== 滚动测试 ====================
    void testScrollUp();        // ESC [ n S - VT100+
    void testScrollDown();      // ESC [ n T - VT100+
    void testScrollRegion();    // ESC [ top ; bottom r - VT100+

    // ==================== 字符属性测试 ====================
    void testSGR();             // ESC [ n m - VT100+
    void testSGRColors();       // 16 色 - VT100+
    void testSGR256Color();     // 256 色 - xterm
    void testSGRTrueColor();    // 24 位真彩色 - xterm
    void testDefaultColors();   // ESC [ 39 m / ESC [ 49 m - VT100+
    void testSGRBold();         // ESC [ 1 m - VT100+
    void testSGRDim();          // ESC [ 2 m - VT100+
    void testSGRItalic();       // ESC [ 3 m - VT100+
    void testSGRUnderline();    // ESC [ 4 m - VT100+
    void testSGRBlink();        // ESC [ 5 m - VT100+
    void testSGRReverse();      // ESC [ 7 m - VT100+
    void testSGRHidden();       // ESC [ 8 m - VT100+
    void testSGRStrike();       // ESC [ 9 m - VT100+

    // ==================== 模式测试 ====================
    void testSetMode();         // ESC [ n h - VT100+
    void testResetMode();       // ESC [ n l - VT100+
    void testPrivateMode();     // ESC [ ? n h / ESC [ ? n l - VT100+
    void testInsertMode();      // ESC [ 4 h - VT100+
    void testOriginMode();      // ESC [ 6 h - VT100+
    void testAutoWrapMode();    // ESC [ 7 h - VT100+
    void testCursorKeysMode();  // ESC [ ? 1 h - VT100+
    void testShowCursor();      // ESC [ ? 25 h - VT100+
    void testAlternateScreen(); // ESC [ ? 1049 h - xterm
    void testMouseTracking();   // ESC [ ? 1000 h - xterm

    // ==================== 终端报告测试 ====================
    void testDeviceStatus();    // ESC [ 5 n - VT100+
    void testCursorPositionReport();  // ESC [ 6 n - VT100+
    void testPrimaryDA();       // ESC [ c - VT100+
    void testSecondaryDA();     // ESC [ > 0 c - VT220+
    void testTertiaryDA();      // ESC [ = c - VT220+

    // ==================== 字符集测试 ====================
    void testCharsets();        // ESC ( A / ESC ) A - VT100+
    void testLineDrawing();     // ESC ( 0 - VT100+
    void testCharsetUK();       // ESC ( A - VT100+
    void testCharsetUS();       // ESC ( B - VT100+
    void testCharsetSpecial();  // ESC ( 0 - VT100+

    // ==================== 窗口操作测试 ====================
    void testWindowTitle();     // ESC ] 0 ; title BEL - xterm
    void testIconTitle();       // ESC ] 1 ; title BEL - xterm
    void testIconName();        // ESC ] 2 ; name BEL - xterm

    // ==================== 键盘映射测试 ====================
    void testFunctionKeys();    // F1-F4, F1-F12 - VT100+/xterm
    void testArrowKeys();       // 方向键 - VT100+/xterm
    void testKeypad();          // 数字键盘 - VT100+
    void testCtrlArrows();      // Ctrl+ 方向键 - xterm
    void testShiftArrows();     // Shift+ 方向键 - xterm

    // ==================== 鼠标支持测试 ====================
    void testX10Mouse();        // ESC [ ? 9 h - xterm
    void testVT200Mouse();      // ESC [ ? 1000 h - xterm
    void testSGRMouse();        // ESC [ ? 1006 h - xterm
    void testURXVTMouse();      // ESC [ ? 1015 h - urxvt

    // 测试状态
    QVector<TestReport> m_testReports;
    QString m_currentCategory;
    
    // 终端引用
    TerminalProxy *m_terminal;
    
    // 状态保存
    int m_savedCursorRow;
    int m_savedCursorCol;
    QByteArray m_savedScreenContent;
    bool m_savedAlternateScreen;
    bool m_savedInsertMode;
    bool m_savedOriginMode;
    int m_savedScrollTop;
    int m_savedScrollBottom;
    
    // 当前测试标准
    TerminalStandard m_testStandard;
    
    // 标准信息映射
    static QMap<TerminalStandard, StandardInfo> m_standardInfo;
};

#endif // VT100TESTER_H
