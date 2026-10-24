#include "terminal/terminalproxy.h"
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
#include <QStringList>

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
    , m_parser(new TerminalParser(this))
    , m_isConnected(false)
    , m_columns(200)
    , m_terminalRows(24)
    , m_historyLines(1000)
    , m_viewportLines(50)
    , m_cursorColumn(0)
    , m_cursorRow(0)
    , m_insertMode(false)
    , m_applicationCursorKeys(false)
    , m_useDecGraphics(false)
    , m_g0Charset('B')
    , m_g1Charset('0')
    , m_useG1(false)
    , m_scrollTop(0)
    , m_scrollBottom(0)
    , m_historyStart(0)
    , m_historySize(0)
    , m_useAlternateScreen(false)
    , m_savedCursorColumn(0)
    , m_savedCursorRow(0)
    , m_savedHistorySize(0)
    , m_savedHistoryStart(0)
    , m_defaultForeground(Qt::white)
    , m_defaultBackground(Qt::black)
    , m_currentForeground(Qt::white)
    , m_currentBackground(Qt::black)
    , m_currentAttributes(Normal)
    , m_charWidth(10)
    , m_charHeight(20)
    , m_selecting(false)
    , m_hasSelection(false)
    , m_mouseTrackingEnabled(false)
    , m_mouseButtonTracking(false)
    , m_mouseAnyEventTracking(false)
    , m_mouseSgrMode(false)
    , m_mouseFocusTracking(false)
{
    setFocusPolicy(Qt::StrongFocus);
    // 无边框：终端区与标签页面板齐平，避免四周出现 2px 的凹陷边框
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setMouseTracking(true);

    // 初始化字体：默认等宽字体；正式主题/字体由创建方通过 setTheme/setFont 注入
    // （MainWindow::applyTerminalTheme 在创建标签页时统一注入 ThemeManager/FontManager
    //  的当前值，构造函数本身不访问全局单例，保证可测试性）
    // GUI 重设计：优先内置等宽字体（JetBrains Mono / Cascadia Code），13px
    m_currentFont = QFont("JetBrains Mono", 10);
    m_currentFont.setStyleHint(QFont::Monospace);
    m_currentFont.setPixelSize(13);
    QFontMetrics fmetrics(m_currentFont);
    m_charWidth = fmetrics.horizontalAdvance('W');
    m_charHeight = fmetrics.height();

    connect(m_parser, &TerminalParser::textReceived, this, &TerminalProxy::onParserTextReceived);
    connect(m_parser, &TerminalParser::cursorUp, this, &TerminalProxy::onParserCursorUp);
    connect(m_parser, &TerminalParser::cursorDown, this, &TerminalProxy::onParserCursorDown);
    connect(m_parser, &TerminalParser::cursorForward, this, &TerminalProxy::onParserCursorForward);
    connect(m_parser, &TerminalParser::cursorBackward, this, &TerminalProxy::onParserCursorBackward);
    connect(m_parser, &TerminalParser::cursorPosition, this, &TerminalProxy::onParserCursorPosition);
    connect(m_parser, &TerminalParser::carriageReturn, this, &TerminalProxy::onParserCarriageReturn);
    connect(m_parser, &TerminalParser::lineFeed, this, &TerminalProxy::onParserLineFeed);
    connect(m_parser, &TerminalParser::clearScreen, this, &TerminalProxy::onParserClearScreen);
    connect(m_parser, &TerminalParser::eraseDown, this, &TerminalProxy::onParserEraseDown);
    connect(m_parser, &TerminalParser::eraseUp, this, &TerminalProxy::onParserEraseUp);
    connect(m_parser, &TerminalParser::clearLine, this, &TerminalProxy::onParserClearLine);
    connect(m_parser, &TerminalParser::eraseEndOfLine, this, &TerminalProxy::onParserEraseEndOfLine);
    connect(m_parser, &TerminalParser::eraseStartOfLine, this, &TerminalProxy::onParserEraseStartOfLine);
    connect(m_parser, &TerminalParser::backspace, this, &TerminalProxy::onParserBackspace);
    connect(m_parser, &TerminalParser::scrollUp, this, &TerminalProxy::onParserScrollUp);
    connect(m_parser, &TerminalParser::scrollDown, this, &TerminalProxy::onParserScrollDown);
    connect(m_parser, &TerminalParser::insertLines, this, &TerminalProxy::onParserInsertLines);
    connect(m_parser, &TerminalParser::deleteLines, this, &TerminalProxy::onParserDeleteLines);
    connect(m_parser, &TerminalParser::setScrollRegion, this, &TerminalProxy::onParserSetScrollRegion);
    connect(m_parser, &TerminalParser::setMode, this, &TerminalProxy::onParserSetMode);
    connect(m_parser, &TerminalParser::resetMode, this, &TerminalProxy::onParserResetMode);
    connect(m_parser, &TerminalParser::setPrivateMode, this, &TerminalProxy::onParserSetPrivateMode);
    connect(m_parser, &TerminalParser::resetPrivateMode, this, &TerminalProxy::onParserResetPrivateMode);
    connect(m_parser, &TerminalParser::setAttributes, this, &TerminalProxy::onParserSetAttributes);
    connect(m_parser, &TerminalParser::resetAttributes, this, &TerminalProxy::onParserResetAttributes);
    connect(m_parser, &TerminalParser::setForegroundColor256, this, &TerminalProxy::onParserSetForegroundColor256);
    connect(m_parser, &TerminalParser::setBackgroundColor256, this, &TerminalProxy::onParserSetBackgroundColor256);
    connect(m_parser, &TerminalParser::deviceStatusReport, this, &TerminalProxy::onDeviceStatusReport);
    connect(m_parser, &TerminalParser::cursorPositionReportRequest, this, &TerminalProxy::onCursorPositionReportRequest);
    connect(m_parser, &TerminalParser::primaryDeviceAttributes, this, &TerminalProxy::onPrimaryDeviceAttributes);
    connect(m_parser, &TerminalParser::secondaryDeviceAttributes, this, &TerminalProxy::onSecondaryDeviceAttributes);

    // 字符编辑命令
    connect(m_parser, &TerminalParser::insertChars, this, &TerminalProxy::onParserInsertChars);
    connect(m_parser, &TerminalParser::deleteChars, this, &TerminalProxy::onParserDeleteChars);
    connect(m_parser, &TerminalParser::eraseChars, this, &TerminalProxy::onParserEraseChars);

    // 光标定位命令
    connect(m_parser, &TerminalParser::cursorNextLine, this, &TerminalProxy::onParserCursorNextLine);
    connect(m_parser, &TerminalParser::cursorPreviousLine, this, &TerminalProxy::onParserCursorPreviousLine);
    connect(m_parser, &TerminalParser::cursorHorizontalAbsolute, this, &TerminalProxy::onParserCursorHorizontalAbsolute);
    connect(m_parser, &TerminalParser::verticalPositionAbsolute, this, &TerminalProxy::onParserVerticalPositionAbsolute);

    // 制表符处理
    connect(m_parser, &TerminalParser::tabSet, this, &TerminalProxy::onParserTabSet);
    connect(m_parser, &TerminalParser::tabClear, this, &TerminalProxy::onParserTabClear);
    connect(m_parser, &TerminalParser::cursorForwardTab, this, &TerminalProxy::onParserCursorForwardTab);
    connect(m_parser, &TerminalParser::cursorBackwardTab, this, &TerminalProxy::onParserCursorBackwardTab);

    // 剪贴板操作
    connect(m_parser, &TerminalParser::clipboardRead, this, &TerminalProxy::onParserClipboardRead);
    connect(m_parser, &TerminalParser::clipboardWrite, this, &TerminalProxy::onParserClipboardWrite);

    // 字符集切换
    connect(m_parser, &TerminalParser::setCharacterSet, this, &TerminalProxy::onParserSetCharacterSet);

    connect(m_parser, &TerminalParser::setWindowTitle, this, [this](const QString &title) {
        // OSC window title - can be used to update window title if needed
        Q_UNUSED(title);
    });

    // 初始主题：内置默认（白字黑底）；正式主题由创建方通过 setTheme 注入
    m_currentTheme = TerminalTheme();
    m_currentTheme.defaultForeground = m_defaultForeground;
    m_currentTheme.defaultBackground = m_defaultBackground;

    viewport()->setAutoFillBackground(true);
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, m_defaultBackground);
    pal.setColor(QPalette::Base, m_defaultBackground);
    viewport()->setPalette(pal);

    // 初始化制表位
    m_tabStops.resize(m_columns);
    for (int i = 0; i < m_columns; ++i) {
        m_tabStops[i] = (i % DEFAULT_TAB_WIDTH == 0);
    }

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
    m_systemStats = QJsonObject();
    m_sessionProcess = new QProcess(this);

    // Set process channel mode to merged so we capture all output
    m_sessionProcess->setProcessChannelMode(QProcess::MergedChannels);

    // Capture session process output for debugging
    connect(m_sessionProcess, &QProcess::readyRead, this, [this]() {
        QByteArray output = m_sessionProcess->readAll();
        QString outputStr = QString::fromUtf8(output);
        // Print each line separately for better readability
        for (const QString &line : outputStr.split('\n', Qt::SkipEmptyParts)) {
            qDebug().noquote() << "[SessionProcess]" << line;
        }
    });

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
    m_systemStats = QJsonObject();
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

int TerminalProxy::setIncomingFilter(IncomingFilter filter)
{
    m_incomingFilter = std::move(filter);
    ++m_incomingFilterToken;
    return m_incomingFilterToken;
}

void TerminalProxy::clearIncomingFilter(int token)
{
    if (token == m_incomingFilterToken) {
        m_incomingFilter = nullptr;
    }
}

void TerminalProxy::clearIncomingFilter()
{
    m_incomingFilter = nullptr;
    ++m_incomingFilterToken;
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
                if (termLine >= 0 && termLine < m_terminalRows && termLine < m_terminalBuffer.size() && col < m_terminalBuffer[termLine].size()) {
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

    painter.setFont(m_currentFont);

    QFontMetrics fmetrics(m_currentFont);

    int topLine = 0;
    if (!m_useAlternateScreen) {
        topLine = verticalScrollBar()->value();
    }
    int leftColumn = horizontalScrollBar()->value();

    int visibleLines = viewport()->height() / m_charHeight;
    int visibleColumns = viewport()->width() / m_charWidth;

    static int lastTopLine = -1;
    static int lastHistorySize = -1;
    static bool lastUseAlternateScreen = false;
    if (topLine != lastTopLine || m_historySize != lastHistorySize || m_useAlternateScreen != lastUseAlternateScreen) {
        qDebug() << "[TerminalProxy] paintEvent: topLine=" << topLine
                 << "historySize=" << m_historySize
                 << "visibleLines=" << visibleLines
                 << "terminalRows=" << m_terminalRows
                 << "cursorRow=" << m_cursorRow
                 << "useAlternateScreen=" << m_useAlternateScreen;
        lastTopLine = topLine;
        lastHistorySize = m_historySize;
        lastUseAlternateScreen = m_useAlternateScreen;
    }

    for (int i = 0; i < visibleLines; ++i) {
        int line = topLine + i;
        const QVector<Cell> *lineData = nullptr;

        if (m_useAlternateScreen) {
            // 在 alternate screen 模式下，只绘制当前屏幕缓冲区
            if (i < m_terminalRows && i < m_terminalBuffer.size()) {
                lineData = &m_terminalBuffer[i];
            }
        } else {
            // 在 normal screen 模式下，绘制历史缓冲区和终端缓冲区
            if (line < m_historySize) {
                int idx = historyIndex(line);
                if (idx >= 0) {
                    lineData = &m_historyBuffer[idx];
                }
            } else {
                int termLine = line - m_historySize;
                if (termLine >= 0 && termLine < m_terminalRows && termLine < m_terminalBuffer.size()) {
                    lineData = &m_terminalBuffer[termLine];
                }
            }
        }

        int y = i * m_charHeight + fmetrics.ascent();

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

    // 在 alternate screen 模式下，光标行直接对应屏幕行
    int cursorScreenLine = m_useAlternateScreen ? m_cursorRow : (m_historySize + m_cursorRow - topLine);
    int cursorScreenCol = m_cursorColumn - leftColumn;

    if (cursorScreenLine >= 0 && cursorScreenLine < visibleLines &&
        cursorScreenCol >= 0 && cursorScreenCol < visibleColumns) {
        int x = cursorScreenCol * m_charWidth;
        int y = cursorScreenLine * m_charHeight;

        painter.fillRect(x, y, 2, m_charHeight, m_defaultForeground);
    }
}

void TerminalProxy::showEvent(QShowEvent *event)
{
    QAbstractScrollArea::showEvent(event);
    
    QFontMetrics fm(m_currentFont);
    m_charWidth = fm.horizontalAdvance('W');
    m_charHeight = fm.height();

    int rows = viewport()->height() / m_charHeight;
    int cols = viewport()->width() / m_charWidth;

    if (rows > 0 && cols > 0) {
        m_terminalRows = rows;
        m_columns = cols;
        allocateBuffers();
        updateScrollBars();

        if (m_isConnected) {
            sendTerminalSize();
        }
    }

    qDebug() << "[TerminalProxy] showEvent: terminalRows=" << m_terminalRows
             << "columns=" << m_columns
             << "charWidth=" << m_charWidth
             << "charHeight=" << m_charHeight
             << "viewportSize=" << viewport()->size();
}

void TerminalProxy::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    QFontMetrics fm(m_currentFont);
    m_charWidth = fm.horizontalAdvance('W');
    m_charHeight = fm.height();

    int rows = viewport()->height() / m_charHeight;
    int cols = viewport()->width() / m_charWidth;

    if (rows > 0 && cols > 0 && (rows != m_terminalRows || cols != m_columns)) {
        m_terminalRows = rows;
        m_columns = cols;
        allocateBuffers();
    }

    updateScrollBars();
    sendTerminalSize();
    viewport()->update();

    qDebug() << "[TerminalProxy] resizeEvent: terminalRows=" << m_terminalRows
             << "columns=" << m_columns
             << "viewportSize=" << viewport()->size();
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
    // 如果启用了鼠标跟踪，发送鼠标事件到终端
    if (m_mouseTrackingEnabled || m_mouseButtonTracking || m_mouseAnyEventTracking) {
        SelectionPoint point = viewportToBuffer(event->pos());
        int button = 0;

        if (event->button() == Qt::LeftButton) {
            button = 0;
        } else if (event->button() == Qt::MiddleButton) {
            button = 1;
        } else if (event->button() == Qt::RightButton) {
            button = 2;
        }

        sendMouseEvent(button, point.column, point.line, true);
        return;
    }

    // 正常的选择模式
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
    // 如果启用了任意事件跟踪，发送移动事件
    if (m_mouseAnyEventTracking) {
        SelectionPoint point = viewportToBuffer(event->pos());
        int button = 32; // 移动事件标记

        if (event->buttons() & Qt::LeftButton) {
            button = 32; // 拖动左键
        } else if (event->buttons() & Qt::MiddleButton) {
            button = 33; // 拖动中键
        } else if (event->buttons() & Qt::RightButton) {
            button = 34; // 拖动右键
        } else {
            button = 35; // 无按钮移动
        }

        sendMouseEvent(button, point.column, point.line, true);
        return;
    }

    // 正常的选择模式
    if (m_selecting) {
        updateSelection(event->pos());
        viewport()->update();
    }
}

void TerminalProxy::mouseReleaseEvent(QMouseEvent *event)
{
    // 如果启用了鼠标跟踪，发送释放事件
    if (m_mouseTrackingEnabled || m_mouseButtonTracking || m_mouseAnyEventTracking) {
        SelectionPoint point = viewportToBuffer(event->pos());
        int button = 0;

        if (event->button() == Qt::LeftButton) {
            button = 0;
        } else if (event->button() == Qt::MiddleButton) {
            button = 1;
        } else if (event->button() == Qt::RightButton) {
            button = 2;
        }

        sendMouseEvent(button, point.column, point.line, false);
        return;
    }

    // 正常的选择模式
    m_selecting = false;
}

void TerminalProxy::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

void TerminalProxy::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("复制"));
    QAction *pasteAction = menu.addAction(tr("粘贴"));
    
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

    // 如果启用了焦点事件跟踪，发送焦点获得事件
    if (m_mouseFocusTracking) {
        QByteArray focusIn = "\033[I";
        qDebug() << "[TerminalProxy] Sending focus in event";
        sendData(focusIn);
    }

    viewport()->update();
}

void TerminalProxy::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event);

    // 如果启用了焦点事件跟踪，发送焦点失去事件
    if (m_mouseFocusTracking) {
        QByteArray focusOut = "\033[O";
        qDebug() << "[TerminalProxy] Sending focus out event";
        sendData(focusOut);
    }

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
        case Ipc::MessageType::SystemStats: {
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (doc.isObject()) {
                m_systemStats = doc.object();
                emit systemStatsUpdated(m_systemStats);
            }
            break;
        }
        default:
            break;
    }
}

void TerminalProxy::onParserTextReceived(const QString &text)
{
    for (const QChar &ch : text) {
        // DEC 图形字符集转换
        QChar displayChar = ch;
        if (m_useDecGraphics && m_g0Charset == '0') {
            static const QMap<ushort, QChar> decGraphicsMap = {
                {'j', QChar(0x2518)},  // ┘
                {'k', QChar(0x2510)},  // ┐
                {'l', QChar(0x250C)},  // ┌
                {'m', QChar(0x2514)},  // └
                {'n', QChar(0x253C)},  // ┼
                {'q', QChar(0x2500)},  // ─
                {'t', QChar(0x251C)},  // ├
                {'u', QChar(0x2524)},  // ┤
                {'v', QChar(0x2534)},  // ┴
                {'w', QChar(0x252C)},  // ┬
                {'x', QChar(0x2502)},  // │
                {'a', QChar(0x2592)},  // ▒
                {'`', QChar(0x25C6)},  // ◆
                {'~', QChar(0x00B7)},  // ·
            };

            if (decGraphicsMap.contains(ch.unicode())) {
                displayChar = decGraphicsMap[ch.unicode()];
            }
        }

        bool isWide = isWideCharacter(displayChar);
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
            m_cursorColumn >= 0 && m_cursorColumn < m_columns &&
            m_cursorRow < m_terminalBuffer.size() &&
            m_cursorColumn < m_terminalBuffer[m_cursorRow].size()) {
            Cell &cell = m_terminalBuffer[m_cursorRow][m_cursorColumn];
            cell.character = displayChar;
            cell.foreground = m_currentForeground;
            cell.background = m_currentBackground;
            cell.attributes = m_currentAttributes;
            cell.isWide = isWide;
            m_cursorColumn++;

            if (isWide && m_cursorColumn < m_columns && m_cursorColumn < m_terminalBuffer[m_cursorRow].size()) {
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
    qDebug() << "[TerminalProxy] CursorPosition - row:" << row << "column:" << column
             << "-> m_cursorRow:" << (qBound(0, row - 1, m_terminalRows - 1))
             << "m_cursorColumn:" << (qBound(0, column - 1, m_columns - 1))
             << "useAlternateScreen:" << m_useAlternateScreen;
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
    int bottom = (m_scrollBottom > 0) ? m_scrollBottom : m_terminalRows - 1;

    qDebug() << "[TerminalProxy] LineFeed - cursorRow:" << m_cursorRow << "->" << (m_cursorRow + 1)
             << "bottom:" << bottom << "useAlternateScreen:" << m_useAlternateScreen;

    m_cursorRow++;
    if (m_cursorRow > bottom) {
        qDebug() << "[TerminalProxy] LineFeed - triggering scrollTerminalUp";
        scrollTerminalUp();
        m_cursorRow = bottom;
    }
    if (m_cursorRow >= m_terminalRows) {
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
             << "scrollbarMax=" << verticalScrollBar()->maximum()
             << "useAlternateScreen=" << m_useAlternateScreen;

    // 在 alternate screen 模式下，不清除历史缓冲区
    if (!m_useAlternateScreen) {
        m_historyStart = 0;
        m_historySize = 0;
    }

    for (int i = 0; i < m_terminalRows && i < m_terminalBuffer.size(); ++i) {
        for (int j = 0; j < m_columns && j < m_terminalBuffer[i].size(); ++j) {
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

    verticalScrollBar()->setValue(0);

    qDebug() << "[TerminalProxy] ClearScreen - After: historySize=" << m_historySize
             << "cursorRow=" << m_cursorRow
             << "scrollbarValue=" << verticalScrollBar()->value()
             << "scrollbarMax=" << verticalScrollBar()->maximum();

    viewport()->update();
}

void TerminalProxy::onParserEraseDown()
{
    qDebug() << "[TerminalProxy] EraseDown - cursorRow=" << m_cursorRow
             << "cursorColumn=" << m_cursorColumn
             << "historySize=" << m_historySize
             << "useAlternateScreen=" << m_useAlternateScreen;

    // 在 alternate screen 模式下，不清除历史缓冲区
    if (!m_useAlternateScreen && m_cursorRow == 0) {
        qDebug() << "[TerminalProxy] EraseDown - cursor at top, clearing history buffer";
        m_historyStart = 0;
        m_historySize = 0;
        updateScrollBars();
        verticalScrollBar()->setValue(0);
    }

    for (int row = m_cursorRow; row < m_terminalRows && row < m_terminalBuffer.size(); ++row) {
        for (int col = (row == m_cursorRow ? m_cursorColumn : 0); col < m_columns && col < m_terminalBuffer[row].size(); ++col) {
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
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalBuffer.size()) {
        for (int j = 0; j < m_columns && j < m_terminalBuffer[m_cursorRow].size(); ++j) {
            m_terminalBuffer[m_cursorRow][j].character = QChar(' ');
            m_terminalBuffer[m_cursorRow][j].foreground = m_defaultForeground;
            m_terminalBuffer[m_cursorRow][j].background = m_defaultBackground;
            m_terminalBuffer[m_cursorRow][j].attributes = Normal;
            m_terminalBuffer[m_cursorRow][j].isWide = false;
        }
    }
    viewport()->update();
}

void TerminalProxy::onParserEraseUp()
{
    qDebug() << "[TerminalProxy] EraseUp - cursorRow=" << m_cursorRow;
    
    for (int row = 0; row <= m_cursorRow && row < m_terminalBuffer.size(); ++row) {
        int endCol = (row == m_cursorRow ? m_cursorColumn + 1 : m_columns);
        for (int col = 0; col < endCol && col < m_terminalBuffer[row].size(); ++col) {
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
    
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows && m_cursorRow < m_terminalBuffer.size()) {
        for (int col = m_cursorColumn; col < m_columns && col < m_terminalBuffer[m_cursorRow].size(); ++col) {
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
    
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows && m_cursorRow < m_terminalBuffer.size()) {
        for (int col = 0; col <= m_cursorColumn && col < m_terminalBuffer[m_cursorRow].size(); ++col) {
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
            m_cursorColumn >= 0 && m_cursorColumn < m_columns &&
            m_cursorRow < m_terminalBuffer.size() &&
            m_cursorColumn < m_terminalBuffer[m_cursorRow].size()) {
            m_terminalBuffer[m_cursorRow][m_cursorColumn].character = QChar(' ');
            m_terminalBuffer[m_cursorRow][m_cursorColumn].foreground = m_defaultForeground;
            m_terminalBuffer[m_cursorRow][m_cursorColumn].background = m_defaultBackground;
            m_terminalBuffer[m_cursorRow][m_cursorColumn].attributes = Normal;
            m_terminalBuffer[m_cursorRow][m_cursorColumn].isWide = false;
        }
        viewport()->update();
    }
}

void TerminalProxy::onParserScrollUp(int count)
{
    qDebug() << "[TerminalProxy] ScrollUp - count:" << count
             << "scrollTop:" << m_scrollTop
             << "scrollBottom:" << m_scrollBottom
             << "useAlternateScreen:" << m_useAlternateScreen;

    int top = m_scrollTop;
    int bottom = (m_scrollBottom > 0) ? m_scrollBottom : m_terminalRows - 1;

    for (int n = 0; n < count; ++n) {
        for (int i = top; i < bottom && i + 1 < m_terminalBuffer.size(); ++i) {
            m_terminalBuffer[i] = m_terminalBuffer[i + 1];
        }

        if (bottom >= 0 && bottom < m_terminalBuffer.size()) {
            m_terminalBuffer[bottom].fill({QChar(' '), m_defaultForeground, m_defaultBackground, Normal, false});
        }
    }

    viewport()->update();
}

void TerminalProxy::onParserScrollDown(int count)
{
    qDebug() << "[TerminalProxy] ScrollDown - count:" << count
             << "scrollTop:" << m_scrollTop
             << "scrollBottom:" << m_scrollBottom
             << "useAlternateScreen:" << m_useAlternateScreen;

    int top = m_scrollTop;
    int bottom = (m_scrollBottom > 0) ? m_scrollBottom : m_terminalRows - 1;

    for (int n = 0; n < count; ++n) {
        for (int i = bottom; i > top && i < m_terminalBuffer.size(); --i) {
            m_terminalBuffer[i] = m_terminalBuffer[i - 1];
        }

        if (top >= 0 && top < m_terminalBuffer.size()) {
            m_terminalBuffer[top].fill({QChar(' '), m_defaultForeground, m_defaultBackground, Normal, false});
        }
    }

    viewport()->update();
}

void TerminalProxy::onParserInsertLines(int count)
{
    qDebug() << "[TerminalProxy] InsertLines - count:" << count << "cursorRow:" << m_cursorRow
             << "scrollTop:" << m_scrollTop << "scrollBottom:" << m_scrollBottom
             << "useAlternateScreen:" << m_useAlternateScreen;

    if (m_cursorRow < 0 || m_cursorRow >= m_terminalRows) return;

    // 使用滚动区域而不是整个终端
    int top = m_scrollTop;
    int bottom = (m_scrollBottom > 0) ? m_scrollBottom : m_terminalRows - 1;

    // 如果光标不在滚动区域内，不执行插入操作
    if (m_cursorRow < top || m_cursorRow > bottom) return;

    for (int n = 0; n < count; ++n) {
        // 在滚动区域内，从底部向上移动行
        for (int i = bottom; i > m_cursorRow && i < m_terminalBuffer.size(); --i) {
            m_terminalBuffer[i] = m_terminalBuffer[i - 1];
        }

        // 在光标位置填充空白行
        if (m_cursorRow >= 0 && m_cursorRow < m_terminalBuffer.size()) {
            m_terminalBuffer[m_cursorRow].fill({QChar(' '), m_defaultForeground, m_defaultBackground, Normal, false});
        }
    }

    viewport()->update();
}

void TerminalProxy::onParserDeleteLines(int count)
{
    qDebug() << "[TerminalProxy] DeleteLines - count:" << count << "cursorRow:" << m_cursorRow
             << "scrollTop:" << m_scrollTop << "scrollBottom:" << m_scrollBottom
             << "useAlternateScreen:" << m_useAlternateScreen;

    if (m_cursorRow < 0 || m_cursorRow >= m_terminalRows) return;

    // 使用滚动区域而不是整个终端
    int top = m_scrollTop;
    int bottom = (m_scrollBottom > 0) ? m_scrollBottom : m_terminalRows - 1;

    // 如果光标不在滚动区域内，不执行删除操作
    if (m_cursorRow < top || m_cursorRow > bottom) return;

    for (int n = 0; n < count; ++n) {
        // 在滚动区域内，从光标位置向下移动行
        for (int i = m_cursorRow; i < bottom && i + 1 < m_terminalBuffer.size(); ++i) {
            m_terminalBuffer[i] = m_terminalBuffer[i + 1];
        }

        // 在底部填充空白行
        if (bottom >= 0 && bottom < m_terminalBuffer.size()) {
            m_terminalBuffer[bottom].fill({QChar(' '), m_defaultForeground, m_defaultBackground, Normal, false});
        }
    }

    viewport()->update();
}

void TerminalProxy::onParserSetScrollRegion(int top, int bottom)
{
    qDebug() << "[TerminalProxy] SetScrollRegion - top:" << top << "bottom:" << bottom
             << "terminalRows:" << m_terminalRows;
    
    m_scrollTop = (top > 0) ? top - 1 : 0;
    m_scrollBottom = (bottom > 0) ? bottom - 1 : m_terminalRows - 1;
    
    if (m_scrollBottom >= m_terminalRows) {
        m_scrollBottom = m_terminalRows - 1;
    }
    
    qDebug() << "[TerminalProxy] Scroll region set: scrollTop=" << m_scrollTop 
             << "scrollBottom=" << m_scrollBottom;
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

void TerminalProxy::onParserSetPrivateMode(int param)
{
    qDebug() << "[TerminalProxy] SetPrivateMode - param:" << param;

    if (param == 1049) {
        // Alternate Screen Buffer (xterm)
        qDebug() << "[TerminalProxy] Entering alternate screen buffer (vi/vim mode)";
        qDebug() << "[TerminalProxy] m_terminalBuffer.size()=" << m_terminalBuffer.size()
                 << "m_terminalRows=" << m_terminalRows
                 << "m_columns=" << m_columns;

        if (!m_useAlternateScreen) {
            m_savedCursorColumn = m_cursorColumn;
            m_savedCursorRow = m_cursorRow;
            m_savedHistorySize = m_historySize;
            m_savedHistoryStart = m_historyStart;

            // 保存主屏幕缓冲区的内容
            m_alternateBuffer = m_terminalBuffer;

            // 清空当前屏幕，准备显示 alternate screen 内容
            for (int i = 0; i < m_terminalRows; ++i) {
                if (i < m_terminalBuffer.size()) {
                    for (int j = 0; j < m_columns && j < m_terminalBuffer[i].size(); ++j) {
                        m_terminalBuffer[i][j].character = QChar(' ');
                        m_terminalBuffer[i][j].foreground = m_defaultForeground;
                        m_terminalBuffer[i][j].background = m_defaultBackground;
                        m_terminalBuffer[i][j].attributes = Normal;
                        m_terminalBuffer[i][j].isWide = false;
                    }
                }
            }

            m_historyStart = 0;
            m_historySize = 0;
            m_cursorRow = 0;
            m_cursorColumn = 0;
            m_useAlternateScreen = true;

            updateScrollBars();
            verticalScrollBar()->setValue(0);
            viewport()->update();
        }
    } else if (param == 1) {
        m_applicationCursorKeys = true;
        qDebug() << "[TerminalProxy] Application cursor keys enabled (private)";
    } else if (param == 25) {
        qDebug() << "[TerminalProxy] Cursor visible (private mode)";
    } else if (param == 1000) {
        // X11 鼠标报告
        m_mouseTrackingEnabled = true;
        qDebug() << "[TerminalProxy] X11 mouse tracking enabled";
    } else if (param == 1002) {
        // 按钮事件鼠标跟踪
        m_mouseButtonTracking = true;
        qDebug() << "[TerminalProxy] Mouse button event tracking enabled";
    } else if (param == 1003) {
        // 任意事件鼠标跟踪
        m_mouseAnyEventTracking = true;
        qDebug() << "[TerminalProxy] Mouse any event tracking enabled";
    } else if (param == 1004) {
        // 焦点事件报告
        m_mouseFocusTracking = true;
        qDebug() << "[TerminalProxy] Focus event tracking enabled";
    } else if (param == 1006) {
        // SGR 鼠标模式
        m_mouseSgrMode = true;
        qDebug() << "[TerminalProxy] SGR mouse mode enabled";
    }
}

void TerminalProxy::onParserResetPrivateMode(int param)
{
    qDebug() << "[TerminalProxy] ResetPrivateMode - param:" << param
             << "useAlternateScreen:" << m_useAlternateScreen
             << "savedCursorRow:" << m_savedCursorRow
             << "savedCursorColumn:" << m_savedCursorColumn
             << "savedHistorySize:" << m_savedHistorySize;

    if (param == 1049) {
        // Alternate Screen Buffer (xterm)
        qDebug() << "[TerminalProxy] Leaving alternate screen buffer (vi/vim mode)";

        if (m_useAlternateScreen) {
            // 恢复主屏幕缓冲区的内容
            if (!m_alternateBuffer.isEmpty()) {
                m_terminalBuffer = m_alternateBuffer;
            }

            m_cursorColumn = m_savedCursorColumn;
            m_cursorRow = m_savedCursorRow;
            m_historySize = m_savedHistorySize;
            m_historyStart = m_savedHistoryStart;
            m_useAlternateScreen = false;

            m_alternateBuffer.clear();

            updateScrollBars();
            ensureCursorVisible();
            viewport()->update();
        }
    } else if (param == 1) {
        m_applicationCursorKeys = false;
        qDebug() << "[TerminalProxy] Normal cursor keys enabled (private)";
    } else if (param == 25) {
        qDebug() << "[TerminalProxy] Cursor invisible (private mode)";
    } else if (param == 1000) {
        // X11 鼠标报告
        m_mouseTrackingEnabled = false;
        qDebug() << "[TerminalProxy] X11 mouse tracking disabled";
    } else if (param == 1002) {
        // 按钮事件鼠标跟踪
        m_mouseButtonTracking = false;
        qDebug() << "[TerminalProxy] Mouse button event tracking disabled";
    } else if (param == 1003) {
        // 任意事件鼠标跟踪
        m_mouseAnyEventTracking = false;
        qDebug() << "[TerminalProxy] Mouse any event tracking disabled";
    } else if (param == 1004) {
        // 焦点事件报告
        m_mouseFocusTracking = false;
        qDebug() << "[TerminalProxy] Focus event tracking disabled";
    } else if (param == 1006) {
        // SGR 鼠标模式
        m_mouseSgrMode = false;
        qDebug() << "[TerminalProxy] SGR mouse mode disabled";
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
        }
        // 注意：38;5;n 和 48;5;n 由专门的信号处理，这里不再处理
    }
}

void TerminalProxy::onParserResetAttributes()
{
    m_currentForeground = m_defaultForeground;
    m_currentBackground = m_defaultBackground;
    m_currentAttributes = Normal;
}

void TerminalProxy::onParserSetForegroundColor256(int colorIndex)
{
    qDebug() << "[TerminalProxy] Setting 256 color foreground:" << colorIndex;
    m_currentForeground = ansi256ColorToQColor(colorIndex);
}

void TerminalProxy::onParserSetBackgroundColor256(int colorIndex)
{
    qDebug() << "[TerminalProxy] Setting 256 color background:" << colorIndex;
    m_currentBackground = ansi256ColorToQColor(colorIndex);
}

// 字符编辑命令实现

void TerminalProxy::onParserInsertChars(int count)
{
    if (count <= 0) count = 1;

    QVector<QVector<Cell>> &buffer = m_useAlternateScreen ? m_alternateBuffer : m_terminalBuffer;
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        QVector<Cell> &line = buffer[m_cursorRow];

        for (int i = m_columns - 1; i >= m_cursorColumn + count; --i) {
            if (i - count >= 0 && i - count < m_columns) {
                line[i] = line[i - count];
            }
        }

        Cell emptyCell;
        emptyCell.character = ' ';
        emptyCell.foreground = m_currentForeground;
        emptyCell.background = m_currentBackground;
        emptyCell.attributes = m_currentAttributes;
        emptyCell.isWide = false;

        for (int i = 0; i < count && m_cursorColumn + i < m_columns; ++i) {
            line[m_cursorColumn + i] = emptyCell;
        }

        viewport()->update();
    }
}

void TerminalProxy::onParserDeleteChars(int count)
{
    if (count <= 0) count = 1;

    QVector<QVector<Cell>> &buffer = m_useAlternateScreen ? m_alternateBuffer : m_terminalBuffer;
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        QVector<Cell> &line = buffer[m_cursorRow];

        for (int i = m_cursorColumn; i < m_columns - count; ++i) {
            if (i + count < m_columns) {
                line[i] = line[i + count];
            }
        }

        Cell emptyCell;
        emptyCell.character = ' ';
        emptyCell.foreground = m_currentForeground;
        emptyCell.background = m_currentBackground;
        emptyCell.attributes = m_currentAttributes;
        emptyCell.isWide = false;

        for (int i = m_columns - count; i < m_columns; ++i) {
            if (i >= 0) {
                line[i] = emptyCell;
            }
        }

        viewport()->update();
    }
}

void TerminalProxy::onParserEraseChars(int count)
{
    if (count <= 0) count = 1;

    QVector<QVector<Cell>> &buffer = m_useAlternateScreen ? m_alternateBuffer : m_terminalBuffer;
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        QVector<Cell> &line = buffer[m_cursorRow];

        Cell emptyCell;
        emptyCell.character = ' ';
        emptyCell.foreground = m_currentForeground;
        emptyCell.background = m_currentBackground;
        emptyCell.attributes = m_currentAttributes;
        emptyCell.isWide = false;

        for (int i = 0; i < count && m_cursorColumn + i < m_columns; ++i) {
            line[m_cursorColumn + i] = emptyCell;
        }

        viewport()->update();
    }
}

// 光标定位命令实现

void TerminalProxy::onParserCursorNextLine(int count)
{
    if (count <= 0) count = 1;
    int newRow = m_cursorRow + count;
    if (newRow >= m_terminalRows) newRow = m_terminalRows - 1;
    m_cursorRow = newRow;
    m_cursorColumn = 0;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalProxy::onParserCursorPreviousLine(int count)
{
    if (count <= 0) count = 1;
    int newRow = m_cursorRow - count;
    if (newRow < 0) newRow = 0;
    m_cursorRow = newRow;
    m_cursorColumn = 0;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalProxy::onParserCursorHorizontalAbsolute(int col)
{
    int actualCol = qMax(0, col - 1);
    if (actualCol >= m_columns) actualCol = m_columns - 1;
    m_cursorColumn = actualCol;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalProxy::onParserVerticalPositionAbsolute(int row)
{
    int actualRow = qMax(0, row - 1);
    if (actualRow >= m_terminalRows) actualRow = m_terminalRows - 1;
    m_cursorRow = actualRow;
    ensureCursorVisible();
    viewport()->update();
}

// 制表符处理实现

void TerminalProxy::onParserTabSet()
{
    if (m_cursorColumn >= 0 && m_cursorColumn < m_tabStops.size()) {
        m_tabStops[m_cursorColumn] = true;
    }
}

void TerminalProxy::onParserTabClear(int param)
{
    if (param == 0) {
        if (m_cursorColumn >= 0 && m_cursorColumn < m_tabStops.size()) {
            m_tabStops[m_cursorColumn] = false;
        }
    } else if (param == 3) {
        m_tabStops.fill(false);
    }
}

void TerminalProxy::onParserCursorForwardTab(int count)
{
    if (count <= 0) count = 1;
    int currentCol = m_cursorColumn;

    for (int i = 0; i < count; ++i) {
        bool found = false;
        for (int col = currentCol + 1; col < m_columns; ++col) {
            if (col < m_tabStops.size() && m_tabStops[col]) {
                currentCol = col;
                found = true;
                break;
            }
        }
        if (!found) {
            currentCol = m_columns - 1;
            break;
        }
    }

    m_cursorColumn = currentCol;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalProxy::onParserCursorBackwardTab(int count)
{
    if (count <= 0) count = 1;
    int currentCol = m_cursorColumn;

    for (int i = 0; i < count; ++i) {
        bool found = false;
        for (int col = currentCol - 1; col >= 0; --col) {
            if (col < m_tabStops.size() && m_tabStops[col]) {
                currentCol = col;
                found = true;
                break;
            }
        }
        if (!found) {
            currentCol = 0;
            break;
        }
    }

    m_cursorColumn = currentCol;
    ensureCursorVisible();
    viewport()->update();
}

// 剪贴板操作实现

void TerminalProxy::onParserClipboardRead()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();

    if (!text.isEmpty()) {
        QByteArray base64 = text.toUtf8().toBase64();
        QByteArray response = "\033]52;c;" + base64 + "\007";
        sendData(response);
    }
}

void TerminalProxy::onParserClipboardWrite(const QString &data)
{
    if (!data.isEmpty()) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(data);
        qDebug() << "[TerminalProxy] 剪贴板已更新:" << data.left(50);
    }
}

// 字符集切换实现

void TerminalProxy::onParserSetCharacterSet(int gset, char charset)
{
    switch (gset) {
        case 0:
            m_g0Charset = charset;
            break;
        case 1:
            m_g1Charset = charset;
            break;
        default:
            break;
    }
    m_useDecGraphics = (m_g0Charset == '0' || m_g1Charset == '0');
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
    qDebug() << "[TerminalProxy] onPrimaryDeviceAttributes - responding with terminal attributes";
    QByteArray response = "\x1b[?6c";
    sendData(response);
}

void TerminalProxy::onSecondaryDeviceAttributes()
{
    qDebug() << "[TerminalProxy] onSecondaryDeviceAttributes - responding with terminal type";
    // ESC[>1;2;0c - VT220 with firmware version 2.0
    QByteArray response = "\x1b[>1;2;0c";
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
            m_systemStats = QJsonObject();
            emit systemStatsUpdated(QJsonObject());
            emit connectionClosed();
            if (status == static_cast<int>(Ipc::ConnectionStatus::Error)) {
                emit connectionError(message);
            }
            break;
        default:
            break;
    }
}

void TerminalProxy::processTerminalData(const QByteArray &data)
{
    handleDataReceived(data);
}

void TerminalProxy::handleDataReceived(const QByteArray &payload)
{
    qDebug() << "========== TerminalProxy::handleDataReceived ==========";
    qDebug() << "[TerminalProxy] Received" << payload.size() << "bytes from IPC";
    qDebug() << "[TerminalProxy] Data hex:" << payload.toHex(' ');
    qDebug() << "=======================================================";

    if (m_incomingFilter && m_incomingFilter(payload)) {
        return;   // 传输协议接管，不显示也不解析
    }

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
        
        if (rows > 0 && cols > 0) {
            if (rows != m_terminalRows || cols != m_columns) {
                m_terminalRows = rows;
                m_columns = cols;
                allocateBuffers();
                updateScrollBars();
            }
            
            QJsonObject size;
            size["rows"] = rows;
            size["columns"] = cols;
            
            m_ipcClient->sendMessage(Ipc::MessageType::TerminalSize, size);
            
            qDebug() << "[TerminalProxy] sendTerminalSize: rows=" << rows 
                     << "cols=" << cols 
                     << "terminalRows=" << m_terminalRows
                     << "columns=" << m_columns;
        }
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
    
    if (m_useAlternateScreen) {
        verticalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setPageStep(visibleLines);
        verticalScrollBar()->setSingleStep(1);
    } else {
        int newMax = qMax(0, totalLines - visibleLines);
        verticalScrollBar()->setRange(0, newMax);
        verticalScrollBar()->setPageStep(visibleLines);
        verticalScrollBar()->setSingleStep(1);
    }
    
    int charWidth = m_charWidth > 0 ? m_charWidth : 10;
    int visibleColumns = viewport()->width() / charWidth;
    if (visibleColumns <= 0) visibleColumns = 1;
    
    int requiredColumns = qMax(m_cursorColumn + 1, visibleColumns);
    if (requiredColumns > m_columns) {
        int newColumns = qMax(requiredColumns, m_columns + 50);
        // qDebug() << "[TerminalProxy] updateScrollBars - Dynamic expand columns from" << m_columns << "to" << newColumns;
        m_columns = newColumns;
        allocateBuffers();
    }

    horizontalScrollBar()->setRange(0, qMax(0, m_columns - visibleColumns));
    horizontalScrollBar()->setPageStep(visibleColumns);
    horizontalScrollBar()->setSingleStep(1);

    // qDebug() << "[TerminalProxy] updateScrollBars: totalLines=" << totalLines
    //          << "historySize=" << m_historySize
    //          << "terminalRows=" << m_terminalRows
    //          << "visibleLines=" << visibleLines
    //          << "visibleColumns=" << visibleColumns
    //          << "columns=" << m_columns
    //          << "scrollbarMax=" << verticalScrollBar()->maximum()
    //          << "useAlternateScreen=" << m_useAlternateScreen;
}

void TerminalProxy::ensureCursorVisible()
{
    if (m_useAlternateScreen) {
        return;
    }
    
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
    if (!m_useAlternateScreen) {
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
    }
    
    QVector<QVector<Cell>> oldTerminalBuffer = m_terminalBuffer;
    int oldColumns = m_terminalBuffer.isEmpty() ? 0 : m_terminalBuffer[0].size();
    int oldRows = m_terminalBuffer.size();
    
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
    
    if (m_cursorRow >= m_terminalRows) {
        m_cursorRow = m_terminalRows - 1;
    }
    if (m_cursorColumn >= m_columns) {
        m_cursorColumn = m_columns - 1;
    }
    
    m_scrollTop = 0;
    m_scrollBottom = m_terminalRows - 1;
    
    qDebug() << "[TerminalProxy] allocateBuffers: terminalRows=" << m_terminalRows
             << "columns=" << m_columns
             << "oldRows=" << oldRows
             << "oldColumns=" << oldColumns;
}

int TerminalProxy::historyIndex(int line) const
{
    if (line < 0 || line >= m_historySize) return -1;
    return (m_historyStart + line) % m_historyLines;
}

void TerminalProxy::scrollTerminalUp()
{
    int top = m_scrollTop;
    int bottom = (m_scrollBottom > 0) ? m_scrollBottom : m_terminalRows - 1;
    
    if (!m_useAlternateScreen) {
        if (top == 0 && bottom == m_terminalRows - 1) {
            if (m_historySize < m_historyLines) {
                m_historySize++;
            } else {
                m_historyStart = (m_historyStart + 1) % m_historyLines;
            }
            
            int idx = historyIndex(m_historySize - 1);
            if (idx >= 0 && !m_terminalBuffer.isEmpty()) {
                m_historyBuffer[idx] = m_terminalBuffer[0];
            }
        }
    }
    
    for (int i = top; i < bottom && i + 1 < m_terminalBuffer.size(); ++i) {
        m_terminalBuffer[i] = m_terminalBuffer[i + 1];
    }
    
    if (bottom >= 0 && bottom < m_terminalBuffer.size()) {
        m_terminalBuffer[bottom].fill({QChar(' '), m_defaultForeground, m_defaultBackground, Normal, false});
    }
    
    updateScrollBars();
    
    qDebug() << "[TerminalProxy] scrollTerminalUp: historySize=" << m_historySize 
             << "historyStart=" << m_historyStart
             << "useAlternateScreen=" << m_useAlternateScreen
             << "scrollTop=" << top
             << "scrollBottom=" << bottom;
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

// ANSI 调色板转换（基于注入的 m_currentTheme，不访问全局单例）
QColor TerminalProxy::ansiColorToQColor(int code, bool isBright) const
{
    if (code >= 0 && code < 8) {
        return isBright ? m_currentTheme.ansiColors[code + 8] : m_currentTheme.ansiColors[code];
    }

    return m_currentTheme.defaultForeground;
}

QColor TerminalProxy::ansi256ColorToQColor(int code) const
{
    if (code < 8) {
        return ansiColorToQColor(code, false);
    } else if (code < 16) {
        return ansiColorToQColor(code - 8, true);
    }
    // 16-231: 6x6x6 color cube；232-255: 灰阶
    // 与 TerminalTheme::ansi256Color 的调色板公式一致
    return m_currentTheme.ansi256Color(code);
}

// ---- 只读状态查询接口 ----

TerminalProxy::Cell TerminalProxy::cellAt(int row, int column) const
{
    Cell blank;
    blank.character = QChar(' ');
    blank.foreground = m_defaultForeground;
    blank.background = m_defaultBackground;
    blank.attributes = Normal;
    blank.isWide = false;

    if (row < 0 || row >= m_terminalRows || row >= m_terminalBuffer.size()) {
        return blank;
    }
    const QVector<Cell> &line = m_terminalBuffer[row];
    if (column < 0 || column >= line.size()) {
        return blank;
    }
    return line[column];
}

QString TerminalProxy::screenText() const
{
    QStringList lines;
    for (int row = 0; row < m_terminalRows && row < m_terminalBuffer.size(); ++row) {
        QString text;
        const QVector<Cell> &line = m_terminalBuffer[row];
        for (int col = 0; col < m_columns && col < line.size(); ++col) {
            text.append(line[col].character);
        }
        // 去掉行尾空格（保留行内空格）
        while (!text.isEmpty() && text.endsWith(QLatin1Char(' '))) {
            text.chop(1);
        }
        lines.append(text);
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalProxy::setTheme(const TerminalTheme &theme)
{
    const QColor oldDefaultForeground = m_defaultForeground;
    const QColor oldDefaultBackground = m_defaultBackground;

    m_currentTheme = theme;
    m_defaultForeground = theme.defaultForeground;
    m_defaultBackground = theme.defaultBackground;
    m_currentForeground = theme.defaultForeground;
    m_currentBackground = theme.defaultBackground;

    // 缓冲区里"未显式着色"的单元格保存的是旧默认色。若只切换默认色，
    // 这些单元格会继续按旧色绘制（例如浅色主题下留下整块黑色背景）。
    // 这里把仍使用旧默认色的单元格迁移到新默认色，保留其它显式颜色。
    if (oldDefaultForeground != m_defaultForeground
        || oldDefaultBackground != m_defaultBackground) {
        auto remapDefaultColors = [&](QVector<QVector<Cell>> &buffer) {
            for (QVector<Cell> &line : buffer) {
                for (Cell &cell : line) {
                    if (cell.foreground == oldDefaultForeground) {
                        cell.foreground = m_defaultForeground;
                    }
                    if (cell.background == oldDefaultBackground) {
                        cell.background = m_defaultBackground;
                    }
                }
            }
        };
        remapDefaultColors(m_terminalBuffer);
        remapDefaultColors(m_historyBuffer);
        remapDefaultColors(m_alternateBuffer);
    }

    viewport()->setAutoFillBackground(true);
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, theme.defaultBackground);
    pal.setColor(QPalette::Base, theme.defaultBackground);
    viewport()->setPalette(pal);
    viewport()->setStyleSheet(QString("background-color: %1;").arg(theme.defaultBackground.name()));

    viewport()->update();
}

void TerminalProxy::setFont(const QFont &font)
{
    m_currentFont = font;
    QFontMetrics fmetrics(m_currentFont);
    m_charWidth = fmetrics.horizontalAdvance('W');
    m_charHeight = fmetrics.height();

    // 重新计算终端行列数
    int newCols = viewport()->width() / m_charWidth;
    int newRows = viewport()->height() / m_charHeight;

    if (newCols > 0 && newRows > 0 && (newCols != m_columns || newRows != m_terminalRows)) {
        m_columns = newCols;
        m_terminalRows = newRows;
        allocateBuffers();
    }

    updateScrollBars();
    sendTerminalSize();
    viewport()->update();
}

void TerminalProxy::sendMouseEvent(int button, int x, int y, bool pressed)
{
    if (!m_mouseTrackingEnabled && !m_mouseButtonTracking && !m_mouseAnyEventTracking) {
        return;
    }

    // 转换为终端坐标（1-based）
    int termX = x + 1;
    int termY = y + 1;

    // 限制坐标范围
    termX = qBound(1, termX, 223);
    termY = qBound(1, termY, 223);

    QByteArray mouseReport;

    if (m_mouseSgrMode) {
        // SGR 鼠标模式: CSI < button ; x ; y M/m
        int cb = button;
        if (!pressed) {
            // 释放事件
            mouseReport = QString("\033[<%1;%2;%3m").arg(cb).arg(termX).arg(termY).toLatin1();
        } else {
            // 按下事件
            mouseReport = QString("\033[<%1;%2;%3M").arg(cb).arg(termX).arg(termY).toLatin1();
        }
    } else {
        // X11 鼠标模式: CSI M button x y
        int cb = button + 32;
        if (!pressed) {
            cb = 3 + 32; // 释放按钮
        }

        mouseReport.append('\033');
        mouseReport.append('[');
        mouseReport.append('M');
        mouseReport.append(static_cast<char>(cb));
        mouseReport.append(static_cast<char>(termX + 32));
        mouseReport.append(static_cast<char>(termY + 32));
    }

    qDebug() << "[TerminalProxy] Sending mouse event:" << mouseReport.toHex();
    sendData(mouseReport);
}

QString TerminalProxy::historyText(int maxLines) const
{
    if (maxLines <= 0 || m_historySize <= 0) {
        return QString();
    }

    const int count = qMin(maxLines, m_historySize);
    const int first = m_historySize - count;
    QStringList lines;
    lines.reserve(count);

    for (int line = first; line < m_historySize; ++line) {
        const int index = historyIndex(line);
        if (index < 0 || index >= m_historyBuffer.size()) {
            continue;
        }
        QString text;
        const QVector<Cell> &cells = m_historyBuffer[index];
        for (int col = 0; col < m_columns && col < cells.size(); ++col) {
            text.append(cells[col].character);
        }
        while (!text.isEmpty() && text.endsWith(QLatin1Char(' '))) {
            text.chop(1);
        }
        lines.append(text);
    }

    return lines.join(QLatin1Char('\n'));
}
