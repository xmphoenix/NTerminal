#ifndef TERMINALPROXY_H
#define TERMINALPROXY_H

#include <QAbstractScrollArea>
#include <QProcess>
#include <QColor>
#include <QVector>
#include <QFont>
#include <QDateTime>
#include "common/connectionsettings.h"
#include "common/messagetypes.h"
#include "ipc/ipcclient.h"
#include "terminal/terminalparser.h"
#include "terminal/terminaltheme.h"

class PythonScriptEngine;

// ============================================================================
// 终端代理（terminal 核心层）
//
// 职责：承载会话子进程（QProcess + IPC 客户端）、驱动 TerminalParser、
// 维护屏幕缓冲（Cell 矩阵 + 历史环形缓冲 + 备用屏）并完成自绘渲染。
//
// 主题/字体为注入式依赖（setTheme/setFont），构造函数不访问全局单例，
// 因此可在无 GUI 初始化的环境（单元测试）中实例化。
//
// 只读状态查询接口（cursorRow/cellAt/screenText 等）供
// TerminalTester 与自动化测试断言屏幕状态。
// ============================================================================

class TerminalProxy : public QAbstractScrollArea
{
    Q_OBJECT

public:
    enum Attribute {
        Normal = 0,
        Bold = 1 << 0,
        Underline = 1 << 1,
        Reverse = 1 << 2,
        Blink = 1 << 3
    };
    Q_DECLARE_FLAGS(Attributes, Attribute)

    struct Cell {
        QChar character;
        QColor foreground;
        QColor background;
        Attributes attributes;
        bool isWide;
    };

    struct SelectionPoint {
        int line;
        int column;
        bool operator==(const SelectionPoint &other) const {
            return line == other.line && column == other.column;
        }
        bool operator!=(const SelectionPoint &other) const {
            return !(*this == other);
        }
    };

    explicit TerminalProxy(QWidget *parent = nullptr);
    ~TerminalProxy();

    void setConnectionSettings(const Common::ConnectionSettings &settings);
    bool openConnection();
    void closeConnection();
    bool isConnected() const;
    Common::ConnectionSettings getConnectionSettings() const;

    void setTheme(const TerminalTheme &theme);
    TerminalTheme currentTheme() const { return m_currentTheme; }

    void setFont(const QFont &font);
    QFont currentFont() const { return m_currentFont; }

    void sendText(const QString &text);
    void sendData(const QByteArray &data);

    // 测试/回放接口：将数据直接注入解析管线（等价于远端数据到达，
    // 不经过 IPC 与真实连接）。供 TerminalTester 与自动化测试驱动终端状态。
    void processTerminalData(const QByteArray &data);

    void copyToClipboard();
    void clearSelection();
    bool hasSelection() const;
    QString getSelectedText() const;

    QString sessionId() const { return m_sessionId; }

    // ---- 只读状态查询（测试/调试断言接口，屏幕坐标 0 起始） ----
    int cursorRow() const { return m_cursorRow; }
    int cursorColumn() const { return m_cursorColumn; }
    int columns() const { return m_columns; }
    int rows() const { return m_terminalRows; }
    bool useAlternateScreen() const { return m_useAlternateScreen; }
    bool insertMode() const { return m_insertMode; }
    bool applicationCursorKeys() const { return m_applicationCursorKeys; }
    int scrollTop() const { return m_scrollTop; }
    int scrollBottom() const { return m_scrollBottom; }
    // 返回屏幕 (row, column) 处的 Cell；越界时返回空白 Cell
    Cell cellAt(int row, int column) const;
    // 返回当前屏幕全部文本（各行去尾部空格，以 '\n' 连接）
    QString screenText() const;

    // Python 脚本支持
    bool loadScript(const QString &scriptPath);
    void stopScript();
    bool isScriptRunning() const;
    QString currentScript() const;
    QString currentScriptName() const;
    QDateTime scriptStartTime() const;

signals:
    void connectionOpened();
    void connectionClosed();
    void connectionError(const QString &error);
    void dataReceived(const QByteArray &data);
    void dataSent(const QByteArray &data);

    // Python 脚本信号
    void scriptStateChanged();
    void scriptStarted();
    void scriptStopped(int exitCode);
    void scriptError(const QString &error);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    
    void onIpcConnected();
    void onIpcDisconnected();
    void onIpcError(const QString &error);
    void onIpcMessageReceived(Ipc::MessageType type, const QByteArray &payload);
    
    void onParserTextReceived(const QString &text);
    void onParserCursorUp(int count);
    void onParserCursorDown(int count);
    void onParserCursorForward(int count);
    void onParserCursorBackward(int count);
    void onParserCursorPosition(int row, int column);
    void onParserCarriageReturn();
    void onParserLineFeed();
    void onParserClearScreen();
    void onParserEraseDown();
    void onParserEraseUp();
    void onParserClearLine();
    void onParserEraseEndOfLine();
    void onParserEraseStartOfLine();
    void onParserBackspace();
    void onParserScrollUp(int count);
    void onParserScrollDown(int count);
    void onParserInsertLines(int count);
    void onParserDeleteLines(int count);
    void onParserSetScrollRegion(int top, int bottom);
    void onParserSetMode(int param);
    void onParserResetMode(int param);
    void onParserSetPrivateMode(int param);
    void onParserResetPrivateMode(int param);
    void onParserSetAttributes(const QVector<int> &params);
    void onParserResetAttributes();
    void onParserSetForegroundColor256(int colorIndex);
    void onParserSetBackgroundColor256(int colorIndex);

    // 字符编辑命令
    void onParserInsertChars(int count);
    void onParserDeleteChars(int count);
    void onParserEraseChars(int count);

    // 光标定位命令
    void onParserCursorNextLine(int count);
    void onParserCursorPreviousLine(int count);
    void onParserCursorHorizontalAbsolute(int col);
    void onParserVerticalPositionAbsolute(int row);

    // 制表符处理
    void onParserTabSet();
    void onParserTabClear(int param);
    void onParserCursorForwardTab(int count);
    void onParserCursorBackwardTab(int count);

    // 剪贴板操作
    void onParserClipboardRead();
    void onParserClipboardWrite(const QString &data);

    // 字符集切换
    void onParserSetCharacterSet(int gset, char charset);

    void onDeviceStatusReport();
    void onCursorPositionReportRequest();
    void onPrimaryDeviceAttributes();
    void onSecondaryDeviceAttributes();

private:
    void updateScrollBars();
    void ensureCursorVisible();
    void allocateBuffers();
    int historyIndex(int line) const;
    void scrollTerminalUp();
    
    // ANSI 调色板转换（使用注入的 m_currentTheme，不访问单例）
    QColor ansiColorToQColor(int code, bool isBright) const;
    QColor ansi256ColorToQColor(int code) const;
    
    void handleConnectionStatus(const QByteArray &payload);
    void handleDataReceived(const QByteArray &payload);
    
    void sendSessionConfig();
    void sendTerminalSize();
    void sendKeyEvent(QKeyEvent *event);
    
    SelectionPoint viewportToBuffer(const QPoint &pos) const;
    void updateSelection(const QPoint &pos);
    void normalizeSelection() const;
    void drawSelectionBackground(QPainter &painter, int line, int lineInView, int topLine, int leftColumn, int visibleLines, int visibleColumns) const;
    
    QString m_sessionId;
    QProcess *m_sessionProcess;
    Ipc::IpcClient *m_ipcClient;
    TerminalParser *m_parser;
    
    Common::ConnectionSettings m_settings;
    bool m_isConnected;
    
    int m_columns;
    int m_terminalRows;
    int m_historyLines;
    int m_viewportLines;
    
    int m_cursorColumn;
    int m_cursorRow;
    
    bool m_insertMode;
    bool m_applicationCursorKeys;

    // 制表位管理
    QVector<bool> m_tabStops;
    static const int DEFAULT_TAB_WIDTH = 8;

    // DEC 图形字符集
    bool m_useDecGraphics;
    char m_g0Charset;
    char m_g1Charset;
    bool m_useG1;

    int m_scrollTop;
    int m_scrollBottom;
    
    QVector<QVector<Cell>> m_historyBuffer;
    QVector<QVector<Cell>> m_terminalBuffer;
    QVector<QVector<Cell>> m_alternateBuffer;
    int m_historyStart;
    int m_historySize;
    
    bool m_useAlternateScreen;
    int m_savedCursorColumn;
    int m_savedCursorRow;
    int m_savedHistorySize;
    int m_savedHistoryStart;
    
    QColor m_defaultForeground;
    QColor m_defaultBackground;
    QColor m_currentForeground;
    QColor m_currentBackground;
    Attributes m_currentAttributes;

    TerminalTheme m_currentTheme;
    QFont m_currentFont;

    int m_charWidth;
    int m_charHeight;

    mutable SelectionPoint m_selectionStart;
    mutable SelectionPoint m_selectionEnd;
    bool m_selecting;
    bool m_hasSelection;

    // 鼠标报告模式
    bool m_mouseTrackingEnabled;      // ?1000 - X11 鼠标报告
    bool m_mouseButtonTracking;       // ?1002 - 按钮事件跟踪
    bool m_mouseAnyEventTracking;     // ?1003 - 任意事件跟踪
    bool m_mouseSgrMode;              // ?1006 - SGR 鼠标模式
    bool m_mouseFocusTracking;        // ?1004 - 焦点事件报告

    void sendMouseEvent(int button, int x, int y, bool pressed);

    // Python 脚本引擎
    PythonScriptEngine *m_scriptEngine;
    QString m_currentScriptPath;
    QDateTime m_scriptStartTime;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TerminalProxy::Attributes)

#endif // TERMINALPROXY_H
