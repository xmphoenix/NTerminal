#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include <QAbstractScrollArea>
#include <QColor>
#include <QVector>
#include <QSerialPort>
#include <QTcpSocket>
#include <QProcess>
#include "connectiondialog.h"
#include "sshclient.h"
#include "pythonscriptengine.h"

class TerminalWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget();

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
    };
    
    struct SelectionPoint {
        int line;    // 缓冲区行号（0-based）
        int column;  // 列号（0-based）
        bool operator==(const SelectionPoint &other) const {
            return line == other.line && column == other.column;
        }
        bool operator!=(const SelectionPoint &other) const {
            return !(*this == other);
        }
    };

    void setTerminalSize(int columns, int rows);
    void setHistorySize(int lines);
    void setViewportSize(int lines);

    void writeText(const QString &text);
    void writeChar(QChar ch);
    void newLine();
    void carriageReturn();
    void setCursor(int column, int row);
    void clearScreen();
    void clearLine();

    void setInsertMode(bool insert);
    void setOverwriteMode(bool overwrite) { setInsertMode(!overwrite); }
    void setApplicationCursorKeys(bool enabled);
    bool applicationCursorKeys() const { return m_applicationCursorKeys; }

    // 连接管理
    void setConnectionSettings(const ConnectionDialog::ConnectionSettings &settings);
    bool openConnection();
    void closeConnection();
    bool isConnected() const;
    void sendData(const QByteArray &data);
    void sendText(const QString &text);
    ConnectionDialog::ConnectionSettings getConnectionSettings() const;
    
    // 文本选择和复制
    void copyToClipboard();
    void clearSelection();
    bool hasSelection() const;
    QString getSelectedText() const;

    // Python 脚本引擎
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
    void scriptStateChanged();  // 脚本状态改变
    void scriptStarted();
    void scriptStopped(int exitCode);
    void scriptError(const QString &error);
    void scriptLogMessage(const QString &level, const QString &message);

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
    bool focusNextPrevChild(bool next) override;

private:
    void updateScrollBars();
    void ensureCursorVisible();
    void scrollUpIfNeeded();
    void allocateBuffers();
    int historyIndex(int line) const;
    void shiftLinesUp(int count);
    void appendHistoryLine();
    void scrollTerminalUp();
    
    // 键盘处理辅助函数
    void handleArrowKey(int key, Qt::KeyboardModifiers modifiers);
    void handleHomeKey(Qt::KeyboardModifiers modifiers);
    void handleEndKey(Qt::KeyboardModifiers modifiers);
    QByteArray getArrowSequence(int key, Qt::KeyboardModifiers modifiers) const;
    QByteArray getHomeSequence(Qt::KeyboardModifiers modifiers) const;
    QByteArray getEndSequence(Qt::KeyboardModifiers modifiers) const;
    
    // 选择相关私有方法
    SelectionPoint viewportToBuffer(const QPoint &pos) const;
    void updateSelection(const QPoint &pos);
    void normalizeSelection() const;
    void drawSelectionBackground(QPainter &painter, int line, int lineInView, int topLine, int leftColumn, int visibleLines, int visibleColumns) const;

    // 连接相关私有方法
    void connectTerminalParserSignals();
    bool openSerialConnection();
    void closeSerialConnection();
    bool openTelnetConnection();
    void closeTelnetConnection();
    bool openSshConnection();
    void closeSshConnection();
    void processReceivedData(const QByteArray &data);
    QByteArray processTelnetProtocol(const QByteArray &data);  // Telnet协议协商处理

    int m_columns;
    int m_terminalRows;
    int m_historyLines;
    int m_viewportLines;

    int m_cursorColumn;
    int m_cursorRow;

    bool m_insertMode;
    bool m_applicationCursorKeys;

    // 制表位管理
    QVector<bool> m_tabStops;  // 每列是否为制表位
    static const int DEFAULT_TAB_WIDTH = 8;  // 默认制表位宽度

    // DEC 图形字符集
    bool m_useDecGraphics;  // 是否使用 DEC 图形字符集
    char m_g0Charset;       // G0 字符集
    char m_g1Charset;       // G1 字符集
    bool m_useG1;           // 当前是否使用 G1 字符集

    QVector<QVector<Cell>> m_historyBuffer;
    QVector<QVector<Cell>> m_terminalBuffer;
    int m_historyStart;
    int m_historySize;

    QColor m_defaultForeground;
    QColor m_defaultBackground;
    QColor m_currentForeground;
    QColor m_currentBackground;
    Attributes m_currentAttributes;

    int m_charWidth;
    int m_charHeight;
    
    // 文本选择相关
    mutable SelectionPoint m_selectionStart;
    mutable SelectionPoint m_selectionEnd;
    bool m_selecting;
    bool m_hasSelection;

    // 连接相关成员
    ConnectionDialog::ConnectionSettings m_connectionSettings;
    QSerialPort *m_serialPort;
    QTcpSocket *m_tcpSocket;
    QProcess *m_sshProcess;      // 使用系统ssh命令（备选方案）
    SshClient *m_sshClient;      // 使用libssh（推荐方案）
    // Terminal parser removed
    bool m_isConnected;

    // Python 脚本引擎
    PythonScriptEngine *m_scriptEngine;
    QString m_scriptPath;
    QString m_scriptName;
    QDateTime m_scriptStartTime;

    // Telnet自动登录状态
    enum TelnetLoginState {
        TelnetLoginNone,        // 未开始登录
        TelnetLoginWaitPrompt,  // 等待登录提示
        TelnetLoginSentUser,    // 已发送用户名
        TelnetLoginWaitPassword,// 等待密码提示
        TelnetLoginSentPassword,// 已发送密码
        TelnetLoginDone         // 登录完成
    };
    TelnetLoginState m_telnetLoginState;
    QString m_receivedBuffer;   // 用于检测登录提示的缓冲区
    
    // SSH自动登录状态
    enum SshLoginState {
        SshLoginNone,           // 未开始登录
        SshLoginWaitPrompt,     // 等待登录提示
        SshLoginWaitPassword,   // 等待密码提示
        SshLoginSentPassword,   // 已发送密码
        SshLoginWaitPassphrase, // 等待密钥密码短语
        SshLoginSentPassphrase, // 已发送密钥密码短语
        SshLoginDone            // 登录完成
    };
    SshLoginState m_sshLoginState;
    QString m_sshReceivedBuffer;
    
private slots:
    void onSerialDataReady();
    void onTcpDataReady();
    void onSshProcessReadyRead();
    void onSshProcessErrorOccurred(QProcess::ProcessError error);
    void onSerialError(QSerialPort::SerialPortError error);
    void onTcpError(QAbstractSocket::SocketError error);
    
    // Terminal parser signal handling
    void onTextReceived(const QString &text);
    void onSetAttributes(const QVector<int> &params);
    void onResetAttributes();
    void onCursorUp(int count);
    void onCursorDown(int count);
    void onCursorForward(int count);
    void onCursorBackward(int count);
    void onCursorPosition(int row, int column);
    void onCursorHome();
    void onBackspace();
    void onCarriageReturn();
    void onLineFeed();
    void onClearScreen();
    void onEraseDown();
    void onEraseUp();
    void onEraseEndOfLine();
    void onEraseStartOfLine();
    void onClearLine();
    void onSetMode(int param);
    void onResetMode(int param);

    // 字符编辑命令
    void onInsertChars(int count);
    void onDeleteChars(int count);
    void onEraseChars(int count);

    // 光标定位命令
    void onCursorNextLine(int count);
    void onCursorPreviousLine(int count);
    void onCursorHorizontalAbsolute(int col);
    void onVerticalPositionAbsolute(int row);

    // 制表符处理
    void onTabSet();
    void onTabClear(int param);
    void onCursorForwardTab(int count);
    void onCursorBackwardTab(int count);

    // 剪贴板操作
    void onClipboardRead();
    void onClipboardWrite(const QString &data);

    // 字符集切换
    void onSetCharacterSet(int gset, char charset);

    // 终端能力查询响应
    void onDeviceStatusReport();
    void onClaude CodePositionReportRequest();
    void onPrimaryDeviceAttributes();

    // Python 脚本引擎槽函数
    void onScriptSendCommand(const QString &command);
    void onScriptLogMessage(const QString &level, const QString &message);
    void onScriptStarted();
    void onScriptStopped(int exitCode);
    void onScriptError(const QString &error);

    // Telnet自动登录
    void sendTelnetCredentials();
    void checkLoginPrompt(const QString &text);
    
    // SSH自动登录
    void sendSshCredentials();
    void checkSshLoginPrompt(const QString &text);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TerminalWidget::Attributes)

#endif // TERMINALWIDGET_H