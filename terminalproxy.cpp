#include "terminalproxy.h"
#include <QPainter>
#include <QScrollBar>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QRandomGenerator>

static QColor ansiColorToQColor(int code, bool isBright);
[[maybe_unused]] static QColor ansi256ColorToQColor(int code);

static bool isWideCharacter(const QChar &ch)
{
    uint unicode = ch.unicode();
    if (unicode >= 0x4E00 && unicode <= 0x9FFF) return true;
    if (unicode >= 0x3400 && unicode <= 0x4DBF) return true;
    if (unicode >= 0x3000 && unicode <= 0x303F) return true;
    if (unicode >= 0xFF00 && unicode <= 0xFFEF) return true;
    if (unicode >= 0xAC00 && unicode <= 0xD7AF) return true;
    if (unicode >= 0x3040 && unicode <= 0x309F) return true;
    if (unicode >= 0x30A0 && unicode <= 0x30FF) return true;
    return false;
}

TerminalProxy::TerminalProxy(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_sessionProcess(nullptr)
    , m_ipcClient(nullptr)
    , m_parser(new VT100Parser(this))
    , m_isConnected(false)
    , m_columns(200)
    , m_terminalRows(24)
    , m_historyLines(1000)
    , m_viewportLines(50)
    , m_cursorColumn(0)
    , m_cursorRow(0)
    , m_insertMode(false)
    , m_applicationCursorKeys(false)
    , m_historyStart(0)
    , m_historySize(0)
    , m_defaultForeground(Qt::white)
    , m_defaultBackground(Qt::black)
    , m_currentForeground(Qt::white)
    , m_currentBackground(Qt::black)
    , m_currentAttributes(Normal)
    , m_charWidth(10)
    , m_charHeight(20)
    , m_selecting(false)
    , m_hasSelection(false)
{
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setMouseTracking(true);
    
    connect(m_parser, &VT100Parser::textReceived, this, &TerminalProxy::onParserTextReceived);
    connect(m_parser, &VT100Parser::cursorUp, this, &TerminalProxy::onParserCursorUp);
    connect(m_parser, &VT100Parser::cursorDown, this, &TerminalProxy::onParserCursorDown);
    connect(m_parser, &VT100Parser::cursorForward, this, &TerminalProxy::onParserCursorForward);
    connect(m_parser, &VT100Parser::cursorBackward, this, &TerminalProxy::onParserCursorBackward);
    connect(m_parser, &VT100Parser::cursorPosition, this, &TerminalProxy::onParserCursorPosition);
    connect(m_parser, &VT100Parser::carriageReturn, this, &TerminalProxy::onParserCarriageReturn);
    connect(m_parser, &VT100Parser::lineFeed, this, &TerminalProxy::onParserLineFeed);
    connect(m_parser, &VT100Parser::clearScreen, this, &TerminalProxy::onParserClearScreen);
    connect(m_parser, &VT100Parser::eraseDown, this, &TerminalProxy::onParserEraseDown);
    connect(m_parser, &VT100Parser::eraseUp, this, &TerminalProxy::onParserEraseUp);
    connect(m_parser, &VT100Parser::clearLine, this, &TerminalProxy::onParserClearLine);
    connect(m_parser, &VT100Parser::eraseEndOfLine, this, &TerminalProxy::onParserEraseEndOfLine);
    connect(m_parser, &VT100Parser::eraseStartOfLine, this, &TerminalProxy::onParserEraseStartOfLine);
    connect(m_parser, &VT100Parser::backspace, this, &TerminalProxy::onParserBackspace);
    connect(m_parser, &VT100Parser::setMode, this, &TerminalProxy::onParserSetMode);
    connect(m_parser, &VT100Parser::resetMode, this, &TerminalProxy::onParserResetMode);
    connect(m_parser, &VT100Parser::setAttributes, this, &TerminalProxy::onParserSetAttributes);
    connect(m_parser, &VT100Parser::resetAttributes, this, &TerminalProxy::onParserResetAttributes);
    connect(m_parser, &VT100Parser::deviceStatusReport, this, &TerminalProxy::onDeviceStatusReport);
    connect(m_parser, &VT100Parser::cursorPositionReportRequest, this, &TerminalProxy::onCursorPositionReportRequest);
    connect(m_parser, &VT100Parser::primaryDeviceAttributes, this, &TerminalProxy::onPrimaryDeviceAttributes);
    
    allocateBuffers();
    updateScrollBars();
}

TerminalProxy::~TerminalProxy()
{
    closeConnection();
}

void TerminalProxy::setConnectionSettings(const Common::ConnectionSettings &settings)
{
    m_settings = settings;
}

bool TerminalProxy::openConnection()
{
    m_sessionProcess = new QProcess(this);
    
    connect(m_sessionProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalProxy::onProcessFinished);
    connect(m_sessionProcess, &QProcess::errorOccurred, this, &TerminalProxy::onProcessError);
    
    m_ipcClient = new Ipc::IpcClient(this);
    
    connect(m_ipcClient, &Ipc::IpcClient::connected, this, &TerminalProxy::onIpcConnected);
    connect(m_ipcClient, &Ipc::IpcClient::disconnected, this, &TerminalProxy::onIpcDisconnected);
    connect(m_ipcClient, &Ipc::IpcClient::errorOccurred, this, &TerminalProxy::onIpcError);
    connect(m_ipcClient, &Ipc::IpcClient::messageReceived, this, &TerminalProxy::onIpcMessageReceived);
    
    m_sessionId = QString("session_%1_%2")
                  .arg(QDateTime::currentMSecsSinceEpoch())
                  .arg(QRandomGenerator::global()->bounded(100000));
    
    QString program = QCoreApplication::applicationFilePath();
    QStringList args;
    args << "--session" << m_sessionId;
    
    qDebug() << "[TerminalProxy] Starting session process:" << program << args;
    m_sessionProcess->start(program, args);
    
    if (!m_sessionProcess->waitForStarted(5000)) {
        qWarning() << "[TerminalProxy] Failed to start session process";
        emit connectionError("Failed to start session process");
        return false;
    }
    
    qDebug() << "[TerminalProxy] Session process started, waiting for IPC server...";
    
    QTimer::singleShot(500, [this]() {
        QString serverName = QString("nterminal_%1").arg(m_sessionId);
        qDebug() << "[TerminalProxy] Connecting to IPC server:" << serverName;
        
        if (!m_ipcClient->connectToServer(serverName, 5000)) {
            qWarning() << "[TerminalProxy] Failed to connect to IPC server";
            emit connectionError("Failed to connect to session process");
        } else {
            qDebug() << "[TerminalProxy] IPC connected, sending config...";
            sendSessionConfig();
            sendTerminalSize();
        }
    });
    
    return true;
}

void TerminalProxy::closeConnection()
{
    if (m_ipcClient && m_ipcClient->isConnected()) {
        m_ipcClient->sendMessage(Ipc::MessageType::Shutdown, QByteArray());
        m_ipcClient->disconnectFromServer();
    }
    
    if (m_sessionProcess) {
        m_sessionProcess->terminate();
        if (!m_sessionProcess->waitForFinished(3000)) {
            m_sessionProcess->kill();
        }
        delete m_sessionProcess;
        m_sessionProcess = nullptr;
    }
    
    if (m_ipcClient) {
        delete m_ipcClient;
        m_ipcClient = nullptr;
    }
    
    m_isConnected = false;
}

bool TerminalProxy::isConnected() const
{
    return m_isConnected;
}

Common::ConnectionSettings TerminalProxy::getConnectionSettings() const
{
    return m_settings;
}

void TerminalProxy::sendText(const QString &text)
{
    sendData(text.toUtf8());
}

void TerminalProxy::sendData(const QByteArray &data)
{
    if (m_ipcClient && m_ipcClient->isConnected()) {
        m_ipcClient->sendMessage(Ipc::MessageType::DataToSend, data);
        emit dataSent(data);
    }
}

void TerminalProxy::copyToClipboard()
{
    if (m_hasSelection) {
        QString text = getSelectedText();
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(text);
    }
}

void TerminalProxy::clearSelection()
{
    m_hasSelection = false;
    m_selecting = false;
    viewport()->update();
}

bool TerminalProxy::hasSelection() const
{
    return m_hasSelection;
}

QString TerminalProxy::getSelectedText() const
{
    if (!m_hasSelection) {
        return QString();
    }
    
    normalizeSelection();
    
    QString text;
    for (int line = m_selectionStart.line; line <= m_selectionEnd.line; ++line) {
        int startCol = (line == m_selectionStart.line) ? m_selectionStart.column : 0;
        int endCol = (line == m_selectionEnd.line) ? m_selectionEnd.column : m_columns;
        
        for (int col = startCol; col < endCol; ++col) {
            const Cell *cell = nullptr;
            if (line < m_historySize) {
                int idx = historyIndex(line);
                if (idx >= 0) {
                    cell = &m_historyBuffer[idx][col];
                }
            } else {
                int termLine = line - m_historySize;
                if (termLine >= 0 && termLine < m_terminalRows) {
                    cell = &m_terminalBuffer[termLine][col];
                }
            }
            
            if (cell) {
                text.append(cell->character);
            }
        }
        
        if (line < m_selectionEnd.line) {
            text.append('\n');
        }
    }
    
    return text;
}

void TerminalProxy::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), m_defaultBackground);
    
    QFont font("Microsoft YaHei Mono", 10);
    font.setStyleHint(QFont::Monospace);
    painter.setFont(font);
    
    QFontMetrics fm(font);
    m_charWidth = fm.horizontalAdvance('W');
    m_charHeight = fm.height();
    
    int topLine = verticalScrollBar()->value();
    int leftColumn = horizontalScrollBar()->value();
    
    int visibleLines = viewport()->height() / m_charHeight;
    int visibleColumns = viewport()->width() / m_charWidth;
    
    static int lastTopLine = -1;
    static int lastHistorySize = -1;
    if (topLine != lastTopLine || m_historySize != lastHistorySize) {
        qDebug() << "[TerminalProxy] paintEvent: topLine=" << topLine 
                 << "historySize=" << m_historySize
                 << "visibleLines=" << visibleLines
                 << "cursorRow=" << m_cursorRow
                 << "cursorGlobalLine=" << (m_historySize + m_cursorRow);
        lastTopLine = topLine;
        lastHistorySize = m_historySize;
    }
    
    for (int i = 0; i < visibleLines; ++i) {
        int line = topLine + i;
        const QVector<Cell> *lineData = nullptr;
        
        if (line < m_historySize) {
            int idx = historyIndex(line);
            if (idx >= 0) {
                lineData = &m_historyBuffer[idx];
            }
        } else {
            int termLine = line - m_historySize;
            if (termLine >= 0 && termLine < m_terminalRows) {
                lineData = &m_terminalBuffer[termLine];
            }
        }
        
        int y = i * m_charHeight + fm.ascent();
        
        for (int j = 0; j < visibleColumns; ++j) {
            int col = leftColumn + j;
            int x = j * m_charWidth;
            
            if (lineData && col < lineData->size()) {
                const Cell &cell = (*lineData)[col];
                
                if (cell.character == QChar(0)) {
                    continue;
                }
                
                QColor bg = cell.background;
                QColor fg = cell.foreground;
                
                if (cell.attributes & Reverse) {
                    std::swap(fg, bg);
                }
                
                int cellWidth = cell.isWide ? 2 : 1;
                int pixelWidth = cellWidth * m_charWidth;
                
                if (bg != m_defaultBackground) {
                    painter.fillRect(x, i * m_charHeight, pixelWidth, m_charHeight, bg);
                }
                
                painter.setPen(fg);
                painter.drawText(x, y, QString(cell.character));
                
                if (cell.isWide) {
                    j++;
                }
            }
        }
    }
    
    if (m_hasSelection) {
        drawSelectionBackground(painter, 0, 0, topLine, leftColumn, visibleLines, visibleColumns);
    }
    
    int cursorGlobalLine = m_historySize + m_cursorRow;
    if (cursorGlobalLine >= topLine && cursorGlobalLine < topLine + visibleLines) {
        int cursorScreenLine = cursorGlobalLine - topLine;
        int cursorScreenCol = m_cursorColumn - leftColumn;
        
        if (cursorScreenCol >= 0 && cursorScreenCol < visibleColumns) {
            int x = cursorScreenCol * m_charWidth;
            int y = cursorScreenLine * m_charHeight;
            
            painter.fillRect(x, y, 2, m_charHeight, m_defaultForeground);
        }
    }
}

void TerminalProxy::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    updateScrollBars();
    sendTerminalSize();
    viewport()->update();
}

void TerminalProxy::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    viewport()->update();
}

void TerminalProxy::keyPressEvent(QKeyEvent *event)
{
    if (!m_isConnected) {
        return;
    }
    
    sendKeyEvent(event);
}

void TerminalProxy::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_hasSelection = false;
        m_selectionStart = viewportToBuffer(event->pos());
        m_selectionEnd = m_selectionStart;
        viewport()->update();
    }
}

void TerminalProxy::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting) {
        updateSelection(event->pos());
        viewport()->update();
    }
}

void TerminalProxy::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_selecting = false;
}

void TerminalProxy::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

void TerminalProxy::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("Copy"));
    QAction *pasteAction = menu.addAction(tr("Paste"));
    
    copyAction->setEnabled(m_hasSelection);
    
    connect(copyAction, &QAction::triggered, this, &TerminalProxy::copyToClipboard);
    connect(pasteAction, &QAction::triggered, [this]() {
        QClipboard *clipboard = QApplication::clipboard();
        QString text = clipboard->text();
        if (!text.isEmpty()) {
            sendText(text);
        }
    });
    
    menu.exec(event->globalPos());
}

void TerminalProxy::focusInEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    viewport()->update();
}

void TerminalProxy::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    viewport()->update();
}

bool TerminalProxy::focusNextPrevChild(bool next)
{
    Q_UNUSED(next);
    return false;
}

void TerminalProxy::onProcessStarted()
{
    qDebug() << "[TerminalProxy] Session process started";
}

void TerminalProxy::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "[TerminalProxy] Session process finished:" << exitCode << exitStatus;
    m_isConnected = false;
    emit connectionClosed();
}

void TerminalProxy::onProcessError(QProcess::ProcessError error)
{
    qWarning() << "[TerminalProxy] Session process error:" << error;
    emit connectionError(QString("Session process error: %1").arg(error));
}

void TerminalProxy::onIpcConnected()
{
    qDebug() << "[TerminalProxy] IPC connected signal received";
}

void TerminalProxy::onIpcDisconnected()
{
    qDebug() << "[TerminalProxy] IPC disconnected";
    m_isConnected = false;
    emit connectionClosed();
}

void TerminalProxy::onIpcError(const QString &error)
{
    qWarning() << "[TerminalProxy] IPC error:" << error;
    emit connectionError(error);
}

void TerminalProxy::onIpcMessageReceived(Ipc::MessageType type, const QByteArray &payload)
{
    switch (type) {
        case Ipc::MessageType::ConnectionStatus:
            handleConnectionStatus(payload);
            break;
        case Ipc::MessageType::DataReceived:
            handleDataReceived(payload);
            break;
        default:
            break;
    }
}

void TerminalProxy::onParserTextReceived(const QString &text)
{
    for (const QChar &ch : text) {
        bool isWide = isWideCharacter(ch);
        int charWidth = isWide ? 2 : 1;
        
        if (m_cursorColumn + charWidth > m_columns) {
            m_cursorColumn = 0;
            m_cursorRow++;
        }
        
        if (m_cursorRow >= m_terminalRows) {
            scrollTerminalUp();
            m_cursorRow = m_terminalRows - 1;
        }
        
        if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows &&
            m_cursorColumn >= 0 && m_cursorColumn < m_columns) {
            Cell &cell = m_terminalBuffer[m_cursorRow][m_cursorColumn];
            cell.character = ch;
            cell.foreground = m_currentForeground;
            cell.background = m_currentBackground;
            cell.attributes = m_currentAttributes;
            cell.isWide = isWide;
            m_cursorColumn++;
            
            if (isWide && m_cursorColumn < m_columns) {
                Cell &placeholder = m_terminalBuffer[m_cursorRow][m_cursorColumn];
                placeholder.character = QChar(0);
                placeholder.foreground = m_currentForeground;
                placeholder.background = m_currentBackground;
                placeholder.attributes = m_currentAttributes;
                placeholder.isWide = false;
                m_cursorColumn++;
            }
        }
    }
    
    updateScrollBars();
    viewport()->update();
    ensureCursorVisible();
}

void TerminalProxy::onParserCursorUp(int count)
{
    m_cursorRow = qMax(0, m_cursorRow - count);
    viewport()->update();
    ensureCursorVisible();
}

void TerminalProxy::onParserCursorDown(int count)
{
    m_cursorRow = qMin(m_terminalRows - 1, m_cursorRow + count);
    viewport()->update();
    ensureCursorVisible();
}

void TerminalProxy::onParserCursorForward(int count)
{
    m_cursorColumn = qMin(m_columns - 1, m_cursorColumn + count);
    viewport()->update();
    ensureCursorVisible();
}

void TerminalProxy::onParserCursorBackward(int count)
{
    m_cursorColumn = qMax(0, m_cursorColumn - count);
    viewport()->update();
    ensureCursorVisible();
}

void TerminalProxy::onParserCursorPosition(int row, int column)
{
    m_cursorRow = qBound(0, row - 1, m_terminalRows - 1);
    m_cursorColumn = qBound(0, column - 1, m_columns - 1);
    viewport()->update();
    ensureCursorVisible();
}

void TerminalProxy::onParserCarriageReturn()
{
    m_cursorColumn = 0;
    viewport()->update();
}

void TerminalProxy::onParserLineFeed()
{
    m_cursorRow++;
    if (m_cursorRow >= m_terminalRows) {
        scrollTerminalUp();
        m_cursorRow = m_terminalRows - 1;
    }
    viewport()->update();
    ensureCursorVisible();
}

void TerminalProxy::onParserClearScreen()
{
    qDebug() << "[TerminalProxy] ClearScreen - Before: historySize=" << m_historySize 
             << "historyStart=" << m_historyStart
             << "cursorRow=" << m_cursorRow 
             << "cursorColumn=" << m_cursorColumn
             << "scrollbarValue=" << verticalScrollBar()->value()
             << "scrollbarMax=" << verticalScrollBar()->maximum();
    
    for (int i = 0; i < m_terminalRows; ++i) {
        for (int j = 0; j < m_columns; ++j) {
            m_terminalBuffer[i][j].character = QChar(' ');
            m_terminalBuffer[i][j].foreground = m_defaultForeground;
            m_terminalBuffer[i][j].background = m_defaultBackground;
            m_terminalBuffer[i][j].attributes = Normal;
            m_terminalBuffer[i][j].isWide = false;
        }
    }
    
    m_cursorRow = 0;
    m_cursorColumn = 0;
    
    updateScrollBars();
    
    int newScrollValue = m_historySize;
    verticalScrollBar()->setValue(newScrollValue);
    
    qDebug() << "[TerminalProxy] ClearScreen - After: historySize=" << m_historySize 
             << "cursorRow=" << m_cursorRow 
             << "scrollbarValue=" << verticalScrollBar()->value()
             << "scrollbarMax=" << verticalScrollBar()->maximum()
             << "newScrollValue=" << newScrollValue;
    
    viewport()->update();
}

void TerminalProxy::onParserEraseDown()
{
    qDebug() << "[TerminalProxy] EraseDown - cursorRow=" << m_cursorRow 
             << "cursorColumn=" << m_cursorColumn
             << "historySize=" << m_historySize;
    
    if (m_cursorRow == 0) {
        qDebug() << "[TerminalProxy] EraseDown - cursor at top, clearing history buffer";
        m_historyStart = 0;
        m_historySize = 0;
        updateScrollBars();
        verticalScrollBar()->setValue(0);
    }
    
    for (int row = m_cursorRow; row < m_terminalRows; ++row) {
        for (int col = (row == m_cursorRow ? m_cursorColumn : 0); col < m_columns; ++col) {
            m_terminalBuffer[row][col].character = QChar(' ');
            m_terminalBuffer[row][col].foreground = m_defaultForeground;
            m_terminalBuffer[row][col].background = m_defaultBackground;
            m_terminalBuffer[row][col].attributes = Normal;
            m_terminalBuffer[row][col].isWide = false;
        }
    }
    
    viewport()->update();
}

void TerminalProxy::onParserClearLine()
{
    for (int j = 0; j < m_columns; ++j) {
        m_terminalBuffer[m_cursorRow][j].character = QChar(' ');
        m_terminalBuffer[m_cursorRow][j].foreground = m_defaultForeground;
        m_terminalBuffer[m_cursorRow][j].background = m_defaultBackground;
        m_terminalBuffer[m_cursorRow][j].attributes = Normal;
        m_terminalBuffer[m_cursorRow][j].isWide = false;
    }
    viewport()->update();
}

void TerminalProxy::onParserEraseUp()
{
    qDebug() << "[TerminalProxy] EraseUp - cursorRow=" << m_cursorRow;
    
    for (int row = 0; row <= m_cursorRow; ++row) {
        int endCol = (row == m_cursorRow ? m_cursorColumn + 1 : m_columns);
        for (int col = 0; col < endCol; ++col) {
            m_terminalBuffer[row][col].character = QChar(' ');
            m_terminalBuffer[row][col].foreground = m_defaultForeground;
            m_terminalBuffer[row][col].background = m_defaultBackground;
            m_terminalBuffer[row][col].attributes = Normal;
            m_terminalBuffer[row][col].isWide = false;
        }
    }
    viewport()->update();
}

void TerminalProxy::onParserEraseEndOfLine()
{
    qDebug() << "[TerminalProxy] EraseEndOfLine - cursorRow=" << m_cursorRow 
             << "cursorColumn=" << m_cursorColumn;
    
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        for (int col = m_cursorColumn; col < m_columns; ++col) {
            m_terminalBuffer[m_cursorRow][col].character = QChar(' ');
            m_terminalBuffer[m_cursorRow][col].foreground = m_defaultForeground;
            m_terminalBuffer[m_cursorRow][col].background = m_defaultBackground;
            m_terminalBuffer[m_cursorRow][col].attributes = Normal;
            m_terminalBuffer[m_cursorRow][col].isWide = false;
        }
    }
    viewport()->update();
}

void TerminalProxy::onParserEraseStartOfLine()
{
    qDebug() << "[TerminalProxy] EraseStartOfLine - cursorRow=" << m_cursorRow 
             << "cursorColumn=" << m_cursorColumn;
    
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        for (int col = 0; col <= m_cursorColumn; ++col) {
            m_terminalBuffer[m_cursorRow][col].character = QChar(' ');
            m_terminalBuffer[m_cursorRow][col].foreground = m_defaultForeground;
            m_terminalBuffer[m_cursorRow][col].background = m_defaultBackground;
            m_terminalBuffer[m_cursorRow][col].attributes = Normal;
            m_terminalBuffer[m_cursorRow][col].isWide = false;
        }
    }
    viewport()->update();
}

void TerminalProxy::onParserBackspace()
{
    qDebug() << "[TerminalProxy] Backspace - cursorRow=" << m_cursorRow 
             << "cursorColumn=" << m_cursorColumn;
    
    if (m_cursorColumn > 0) {
        m_cursorColumn--;
        if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows &&
            m_cursorColumn >= 0 && m_cursorColumn < m_columns) {
            m_terminalBuffer[m_cursorRow][m_cursorColumn].character = QChar(' ');
            m_terminalBuffer[m_cursorRow][m_cursorColumn].foreground = m_defaultForeground;
            m_terminalBuffer[m_cursorRow][m_cursorColumn].background = m_defaultBackground;
            m_terminalBuffer[m_cursorRow][m_cursorColumn].attributes = Normal;
            m_terminalBuffer[m_cursorRow][m_cursorColumn].isWide = false;
        }
        viewport()->update();
    }
}

void TerminalProxy::onParserSetMode(int param)
{
    qDebug() << "[TerminalProxy] SetMode - param:" << param;
    
    if (param == 4) {
        m_insertMode = true;
        qDebug() << "[TerminalProxy] Insert mode enabled";
    } else if (param == 1) {
        m_applicationCursorKeys = true;
        qDebug() << "[TerminalProxy] Application cursor keys enabled";
    }
}

void TerminalProxy::onParserResetMode(int param)
{
    qDebug() << "[TerminalProxy] ResetMode - param:" << param;
    
    if (param == 4) {
        m_insertMode = false;
        qDebug() << "[TerminalProxy] Insert mode disabled (overwrite mode)";
    } else if (param == 1) {
        m_applicationCursorKeys = false;
        qDebug() << "[TerminalProxy] Normal cursor keys enabled";
    }
}

void TerminalProxy::onParserSetAttributes(const QVector<int> &params)
{
    for (int code : params) {
        if (code == 0) {
            m_currentForeground = m_defaultForeground;
            m_currentBackground = m_defaultBackground;
            m_currentAttributes = Normal;
        } else if (code == 1) {
            m_currentAttributes |= Bold;
        } else if (code == 4) {
            m_currentAttributes |= Underline;
        } else if (code == 5) {
            m_currentAttributes |= Blink;
        } else if (code == 7) {
            m_currentAttributes |= Reverse;
        } else if (code >= 30 && code <= 37) {
            m_currentForeground = ansiColorToQColor(code - 30, false);
        } else if (code >= 90 && code <= 97) {
            m_currentForeground = ansiColorToQColor(code - 90, true);
        } else if (code >= 40 && code <= 47) {
            m_currentBackground = ansiColorToQColor(code - 40, false);
        } else if (code >= 100 && code <= 107) {
            m_currentBackground = ansiColorToQColor(code - 100, true);
        } else if (code == 38 || code == 48) {
        }
    }
}

void TerminalProxy::onParserResetAttributes()
{
    m_currentForeground = m_defaultForeground;
    m_currentBackground = m_defaultBackground;
    m_currentAttributes = Normal;
}

void TerminalProxy::onDeviceStatusReport()
{
    qDebug() << "[TerminalProxy] onDeviceStatusReport - responding with device status OK";
    QByteArray response = "\x1b[0n";
    sendData(response);
}

void TerminalProxy::onCursorPositionReportRequest()
{
    qDebug() << "[TerminalProxy] onCursorPositionReportRequest - cursor at (" << m_cursorRow + 1 << "," << m_cursorColumn + 1 << ")";
    int row = m_cursorRow + 1;
    int column = m_cursorColumn + 1;
    QByteArray response = QString("\x1b[%1;%2R").arg(row).arg(column).toUtf8();
    sendData(response);
}

void TerminalProxy::onPrimaryDeviceAttributes()
{
    qDebug() << "[TerminalProxy] onPrimaryDeviceAttributes - responding with VT100 attributes";
    QByteArray response = "\x1b[?6c";
    sendData(response);
}

void TerminalProxy::handleConnectionStatus(const QByteArray &payload)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError) {
        return;
    }
    
    QJsonObject obj = doc.object();
    int status = obj["status"].toInt();
    QString message = obj["message"].toString();
    
    qDebug() << "[TerminalProxy] Connection status:" << status << message;
    
    switch (static_cast<Ipc::ConnectionStatus>(status)) {
        case Ipc::ConnectionStatus::Connected:
            m_isConnected = true;
            emit connectionOpened();
            break;
        case Ipc::ConnectionStatus::Disconnected:
        case Ipc::ConnectionStatus::Error:
            m_isConnected = false;
            emit connectionClosed();
            if (status == static_cast<int>(Ipc::ConnectionStatus::Error)) {
                emit connectionError(message);
            }
            break;
        default:
            break;
    }
}

void TerminalProxy::handleDataReceived(const QByteArray &payload)
{
    emit dataReceived(payload);
    m_parser->processData(payload);
}

void TerminalProxy::sendSessionConfig()
{
    if (m_ipcClient && m_ipcClient->isConnected()) {
        QJsonObject config = m_settings.toJson();
        m_ipcClient->sendMessage(Ipc::MessageType::SessionConfig, config);
    }
}

void TerminalProxy::sendTerminalSize()
{
    if (m_ipcClient && m_ipcClient->isConnected()) {
        int charHeight = m_charHeight > 0 ? m_charHeight : 20;
        int charWidth = m_charWidth > 0 ? m_charWidth : 10;
        
        int rows = viewport()->height() / charHeight;
        int cols = viewport()->width() / charWidth;
        
        QJsonObject size;
        size["rows"] = rows;
        size["columns"] = cols;
        
        m_ipcClient->sendMessage(Ipc::MessageType::TerminalSize, size);
    }
}

void TerminalProxy::sendKeyEvent(QKeyEvent *event)
{
    int key = event->key();
    Qt::KeyboardModifiers modifiers = event->modifiers();
    QString text = event->text();
    
    QByteArray data;
    
    if (modifiers & Qt::ControlModifier) {
        if (key >= Qt::Key_A && key <= Qt::Key_Z) {
            char ctrlChar = static_cast<char>(key - Qt::Key_A + 1);
            data.append(ctrlChar);
        }
    } else {
        switch (key) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                data.append("\r");
                break;
            case Qt::Key_Backspace:
                data.append("\b");
                break;
            case Qt::Key_Tab:
                data.append("\t");
                break;
            case Qt::Key_Escape:
                data.append("\x1b");
                break;
            case Qt::Key_Up:
                if (modifiers & Qt::ShiftModifier) {
                    data.append("\x1b[1;2A");
                } else if (modifiers & Qt::ControlModifier) {
                    data.append("\x1b[1;5A");
                } else {
                    data.append(m_applicationCursorKeys ? "\x1bOA" : "\x1b[A");
                }
                break;
            case Qt::Key_Down:
                if (modifiers & Qt::ShiftModifier) {
                    data.append("\x1b[1;2B");
                } else if (modifiers & Qt::ControlModifier) {
                    data.append("\x1b[1;5B");
                } else {
                    data.append(m_applicationCursorKeys ? "\x1bOB" : "\x1b[B");
                }
                break;
            case Qt::Key_Right:
                if (modifiers & Qt::ShiftModifier) {
                    data.append("\x1b[1;2C");
                } else if (modifiers & Qt::ControlModifier) {
                    data.append("\x1b[1;5C");
                } else {
                    data.append(m_applicationCursorKeys ? "\x1bOC" : "\x1b[C");
                }
                break;
            case Qt::Key_Left:
                if (modifiers & Qt::ShiftModifier) {
                    data.append("\x1b[1;2D");
                } else if (modifiers & Qt::ControlModifier) {
                    data.append("\x1b[1;5D");
                } else {
                    data.append(m_applicationCursorKeys ? "\x1bOD" : "\x1b[D");
                }
                break;
            case Qt::Key_Home:
                data.append(m_applicationCursorKeys ? "\x1bOH" : "\x1b[H");
                break;
            case Qt::Key_End:
                data.append(m_applicationCursorKeys ? "\x1bOF" : "\x1b[F");
                break;
            case Qt::Key_PageUp:
                data.append("\x1b[5~");
                break;
            case Qt::Key_PageDown:
                data.append("\x1b[6~");
                break;
            case Qt::Key_Insert:
                data.append("\x1b[2~");
                break;
            case Qt::Key_Delete:
                data.append("\x1b[3~");
                break;
            default:
                if (!text.isEmpty()) {
                    data = text.toUtf8();
                }
                break;
        }
    }
    
    if (!data.isEmpty()) {
        sendData(data);
    }
}

void TerminalProxy::updateScrollBars()
{
    int totalLines = m_historySize + m_terminalRows;
    
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    int visibleLines = viewport()->height() / charHeight;
    if (visibleLines <= 0) visibleLines = 1;
    
    int newMax = qMax(0, totalLines - visibleLines);
    verticalScrollBar()->setRange(0, newMax);
    verticalScrollBar()->setPageStep(visibleLines);
    verticalScrollBar()->setSingleStep(1);
    
    int charWidth = m_charWidth > 0 ? m_charWidth : 10;
    int visibleColumns = viewport()->width() / charWidth;
    if (visibleColumns <= 0) visibleColumns = 1;
    
    int requiredColumns = qMax(m_cursorColumn + 1, visibleColumns);
    if (requiredColumns > m_columns) {
        int newColumns = qMax(requiredColumns, m_columns + 50);
        qDebug() << "[TerminalProxy] updateScrollBars - Dynamic expand columns from" << m_columns << "to" << newColumns;
        m_columns = newColumns;
        allocateBuffers();
    }
    
    horizontalScrollBar()->setRange(0, qMax(0, m_columns - visibleColumns));
    horizontalScrollBar()->setPageStep(visibleColumns);
    horizontalScrollBar()->setSingleStep(1);
    
    qDebug() << "[TerminalProxy] updateScrollBars: totalLines=" << totalLines 
             << "historySize=" << m_historySize
             << "terminalRows=" << m_terminalRows
             << "visibleLines=" << visibleLines
             << "visibleColumns=" << visibleColumns
             << "columns=" << m_columns
             << "scrollbarMax=" << newMax;
}

void TerminalProxy::ensureCursorVisible()
{
    int cursorGlobalLine = m_historySize + m_cursorRow;
    
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    int visibleLines = viewport()->height() / charHeight;
    if (visibleLines <= 0) visibleLines = 1;
    
    int maxScroll = verticalScrollBar()->maximum();
    int desiredScroll = qMax(0, cursorGlobalLine - visibleLines + 1);
    
    if (desiredScroll <= maxScroll) {
        verticalScrollBar()->setValue(desiredScroll);
    } else {
        verticalScrollBar()->setValue(maxScroll);
    }
}

void TerminalProxy::allocateBuffers()
{
    QVector<QVector<Cell>> oldHistoryBuffer = m_historyBuffer;
    int oldColumns = m_historyBuffer.isEmpty() ? 0 : m_historyBuffer[0].size();
    
    m_historyBuffer.resize(m_historyLines);
    for (int i = 0; i < m_historyLines; ++i) {
        int oldSize = m_historyBuffer[i].size();
        m_historyBuffer[i].resize(m_columns);
        
        if (i < oldHistoryBuffer.size() && oldColumns > 0) {
            for (int j = 0; j < oldColumns && j < m_columns; ++j) {
                m_historyBuffer[i][j] = oldHistoryBuffer[i][j];
            }
        }
        
        for (int j = oldSize; j < m_columns; ++j) {
            m_historyBuffer[i][j].character = QChar(' ');
            m_historyBuffer[i][j].foreground = m_defaultForeground;
            m_historyBuffer[i][j].background = m_defaultBackground;
            m_historyBuffer[i][j].attributes = Normal;
            m_historyBuffer[i][j].isWide = false;
        }
    }
    
    QVector<QVector<Cell>> oldTerminalBuffer = m_terminalBuffer;
    oldColumns = m_terminalBuffer.isEmpty() ? 0 : m_terminalBuffer[0].size();
    
    m_terminalBuffer.resize(m_terminalRows);
    for (int i = 0; i < m_terminalRows; ++i) {
        int oldSize = m_terminalBuffer[i].size();
        m_terminalBuffer[i].resize(m_columns);
        
        if (i < oldTerminalBuffer.size() && oldColumns > 0) {
            for (int j = 0; j < oldColumns && j < m_columns; ++j) {
                m_terminalBuffer[i][j] = oldTerminalBuffer[i][j];
            }
        }
        
        for (int j = oldSize; j < m_columns; ++j) {
            m_terminalBuffer[i][j].character = QChar(' ');
            m_terminalBuffer[i][j].foreground = m_defaultForeground;
            m_terminalBuffer[i][j].background = m_defaultBackground;
            m_terminalBuffer[i][j].attributes = Normal;
            m_terminalBuffer[i][j].isWide = false;
        }
    }
}

int TerminalProxy::historyIndex(int line) const
{
    if (line < 0 || line >= m_historySize) return -1;
    return (m_historyStart + line) % m_historyLines;
}

void TerminalProxy::scrollTerminalUp()
{
    if (m_historySize < m_historyLines) {
        m_historySize++;
    } else {
        m_historyStart = (m_historyStart + 1) % m_historyLines;
    }
    
    int idx = historyIndex(m_historySize - 1);
    if (idx >= 0) {
        m_historyBuffer[idx] = m_terminalBuffer[0];
    }
    
    for (int i = 0; i < m_terminalRows - 1; ++i) {
        m_terminalBuffer[i] = m_terminalBuffer[i + 1];
    }
    
    m_terminalBuffer[m_terminalRows - 1].fill({QChar(' '), m_defaultForeground, m_defaultBackground, Normal, false});
    
    updateScrollBars();
    
    qDebug() << "[TerminalProxy] scrollTerminalUp: historySize=" << m_historySize 
             << "historyStart=" << m_historyStart;
}

TerminalProxy::SelectionPoint TerminalProxy::viewportToBuffer(const QPoint &pos) const
{
    SelectionPoint point;
    
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    int charWidth = m_charWidth > 0 ? m_charWidth : 10;
    
    point.line = verticalScrollBar()->value() + pos.y() / charHeight;
    point.column = horizontalScrollBar()->value() + pos.x() / charWidth;
    
    return point;
}

void TerminalProxy::updateSelection(const QPoint &pos)
{
    m_selectionEnd = viewportToBuffer(pos);
    m_hasSelection = true;
}

void TerminalProxy::normalizeSelection() const
{
    if (m_selectionStart.line > m_selectionEnd.line ||
        (m_selectionStart.line == m_selectionEnd.line && m_selectionStart.column > m_selectionEnd.column)) {
        std::swap(m_selectionStart, m_selectionEnd);
    }
}

void TerminalProxy::drawSelectionBackground(QPainter &painter, int line, int lineInView, int topLine, int leftColumn, int visibleLines, int visibleColumns) const
{
    Q_UNUSED(line);
    Q_UNUSED(lineInView);
    
    normalizeSelection();
    
    QColor selectionColor(0, 120, 215, 100);
    
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    int charWidth = m_charWidth > 0 ? m_charWidth : 10;
    
    for (int i = 0; i < visibleLines; ++i) {
        int currentLine = topLine + i;
        
        if (currentLine >= m_selectionStart.line && currentLine <= m_selectionEnd.line) {
            int startCol = (currentLine == m_selectionStart.line) ? m_selectionStart.column : 0;
            int endCol = (currentLine == m_selectionEnd.line) ? m_selectionEnd.column : m_columns;
            
            startCol = qMax(startCol, leftColumn);
            endCol = qMin(endCol, leftColumn + visibleColumns);
            
            if (startCol < endCol) {
                int x = (startCol - leftColumn) * charWidth;
                int y = i * charHeight;
                int width = (endCol - startCol) * charWidth;
                
                painter.fillRect(x, y, width, charHeight, selectionColor);
            }
        }
    }
}

static QColor ansiColorToQColor(int code, bool isBright)
{
    static const QColor standardColors[] = {
        QColor(0, 0, 0),
        QColor(170, 0, 0),
        QColor(0, 170, 0),
        QColor(170, 85, 0),
        QColor(0, 0, 170),
        QColor(170, 0, 170),
        QColor(0, 170, 170),
        QColor(170, 170, 170)
    };
    
    static const QColor brightColors[] = {
        QColor(85, 85, 85),
        QColor(255, 85, 85),
        QColor(85, 255, 85),
        QColor(255, 255, 85),
        QColor(85, 85, 255),
        QColor(255, 85, 255),
        QColor(85, 255, 255),
        QColor(255, 255, 255)
    };
    
    if (code >= 0 && code < 8) {
        return isBright ? brightColors[code] : standardColors[code];
    }
    
    return standardColors[0];
}

static QColor ansi256ColorToQColor(int code)
{
    if (code < 8) {
        return ansiColorToQColor(code, false);
    } else if (code < 16) {
        return ansiColorToQColor(code - 8, true);
    } else if (code < 232) {
        int n = code - 16;
        int r = (n / 36) ? (n / 36) * 40 + 55 : 0;
        int g = ((n % 36) / 6) ? ((n % 36) / 6) * 40 + 55 : 0;
        int b = (n % 6) ? (n % 6) * 40 + 55 : 0;
        return QColor(r, g, b);
    } else {
        int gray = (code - 232) * 10 + 8;
        return QColor(gray, gray, gray);
    }
}
