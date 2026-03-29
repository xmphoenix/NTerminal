#ifndef TERMINALPROXY_H
#define TERMINALPROXY_H

#include <QAbstractScrollArea>
#include <QProcess>
#include <QColor>
#include <QVector>
#include <QFont>
#include "common/connectionsettings.h"
#include "common/messagetypes.h"
#include "ipc/ipcclient.h"
#include "terminalparser.h"
#include "thememanager.h"
#include "fontmanager.h"

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

    void copyToClipboard();
    void clearSelection();
    bool hasSelection() const;
    QString getSelectedText() const;

    QString sessionId() const { return m_sessionId; }

signals:
    void connectionOpened();
    void connectionClosed();
    void connectionError(const QString &error);
    void dataReceived(const QByteArray &data);
    void dataSent(const QByteArray &data);

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
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TerminalProxy::Attributes)

#endif // TERMINALPROXY_H
