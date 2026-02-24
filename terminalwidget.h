#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include <QAbstractScrollArea>
#include <QColor>
#include <QVector>
#include <QSerialPort>
#include <QTcpSocket>
#include <QProcess>
#include "connectiondialog.h"
#include "vt100parser.h"

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

private:
    void updateScrollBars();
    void ensureCursorVisible();
    void scrollUpIfNeeded();
    void allocateBuffers();
    int historyIndex(int line) const;
    void shiftLinesUp(int count);
    void appendHistoryLine();
    void scrollTerminalUp();
    
    // 选择相关私有方法
    SelectionPoint viewportToBuffer(const QPoint &pos) const;
    void updateSelection(const QPoint &pos);
    void normalizeSelection() const;
    void drawSelectionBackground(QPainter &painter, int line, int lineInView, int topLine, int leftColumn, int visibleLines, int visibleColumns) const;
    
    // 连接相关私有方法
    void connectVT100ParserSignals();
    bool openSerialConnection();
    void closeSerialConnection();
    bool openTelnetConnection();
    void closeTelnetConnection();
    bool openSshConnection();
    void closeSshConnection();
    void processReceivedData(const QByteArray &data);

    int m_columns;
    int m_terminalRows;
    int m_historyLines;
    int m_viewportLines;

    int m_cursorColumn;
    int m_cursorRow;

    bool m_insertMode;

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
    QProcess *m_sshProcess;
    VT100Parser *m_vt100Parser;
    bool m_isConnected;
    
private slots:
    void onSerialDataReady();
    void onTcpDataReady();
    void onSshProcessReadyRead();
    void onSshProcessErrorOccurred(QProcess::ProcessError error);
    void onSerialError(QSerialPort::SerialPortError error);
    void onTcpError(QAbstractSocket::SocketError error);
    
    // VT100解析器信号处理
    void onTextReceived(const QString &text);
    void onSetAttributes(const QVector<int> &params);
    void onResetAttributes();
    void onCursorUp(int count);
    void onCursorDown(int count);
    void onCursorForward(int count);
    void onCursorBackward(int count);
    void onCursorPosition(int row, int column);
    void onCursorHome();
    void onCarriageReturn();
    void onLineFeed();
    void onClearScreen();
    void onClearLine();
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TerminalWidget::Attributes)

#endif // TERMINALWIDGET_H