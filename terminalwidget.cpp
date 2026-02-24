#include "terminalwidget.h"
#include <QPainter>
#include <QScrollBar>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QDebug>

static QColor ansiColorToQColor(int code, bool isBright);
static QColor ansi256ColorToQColor(int code);

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_columns(80)
    , m_terminalRows(24)
    , m_historyLines(1000)
    , m_viewportLines(50)
    , m_cursorColumn(0)
    , m_cursorRow(0)
    , m_insertMode(false)
    , m_historyStart(0)
    , m_historySize(0)
    , m_defaultForeground(Qt::white)
    , m_defaultBackground(Qt::black)
    , m_currentForeground(Qt::white)
    , m_currentBackground(Qt::black)
    , m_currentAttributes(Normal)
    , m_charWidth(10)
    , m_charHeight(20)
    , m_serialPort(nullptr)
    , m_tcpSocket(nullptr)
    , m_sshProcess(nullptr)
    , m_vt100Parser(nullptr)
    , m_isConnected(false)
{
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // 初始化VT100解析器
    m_vt100Parser = new VT100Parser(this);
    connectVT100ParserSignals();
    
    allocateBuffers();
    updateScrollBars();
}

TerminalWidget::~TerminalWidget()
{
    closeConnection();
    if (m_serialPort) delete m_serialPort;
    if (m_tcpSocket) delete m_tcpSocket;
    if (m_sshProcess) delete m_sshProcess;
}

// 基础终端方法
void TerminalWidget::setTerminalSize(int columns, int rows)
{
    m_columns = columns;
    m_terminalRows = rows;
    allocateBuffers();
    updateScrollBars();
    viewport()->update();
}

void TerminalWidget::setHistorySize(int lines)
{
    m_historyLines = lines;
    allocateBuffers();
    updateScrollBars();
}

void TerminalWidget::setViewportSize(int lines)
{
    m_viewportLines = lines;
    updateScrollBars();
}

void TerminalWidget::writeText(const QString &text)
{
    for (const QChar &ch : text) {
        writeChar(ch);
    }
    viewport()->update();
}

void TerminalWidget::writeChar(QChar ch)
{
    if (m_cursorColumn >= m_columns) {
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
        
        m_cursorColumn++;
    }
}

void TerminalWidget::newLine()
{
    m_cursorColumn = 0;
    m_cursorRow++;
    
    if (m_cursorRow >= m_terminalRows) {
        scrollTerminalUp();
        m_cursorRow = m_terminalRows - 1;
    }
    viewport()->update();
}

void TerminalWidget::carriageReturn()
{
    m_cursorColumn = 0;
}

void TerminalWidget::setCursor(int column, int row)
{
    if (column >= 0 && column < m_columns && row >= 0 && row < m_terminalRows) {
        m_cursorColumn = column;
        m_cursorRow = row;
    }
    viewport()->update();
}

void TerminalWidget::clearScreen()
{
    for (int row = 0; row < m_terminalRows; ++row) {
        for (int col = 0; col < m_columns; ++col) {
            Cell &cell = m_terminalBuffer[row][col];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
    }
    m_cursorColumn = 0;
    m_cursorRow = 0;
    viewport()->update();
}

void TerminalWidget::clearLine()
{
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        for (int col = 0; col < m_columns; ++col) {
            Cell &cell = m_terminalBuffer[m_cursorRow][col];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
    }
    viewport()->update();
}

void TerminalWidget::setInsertMode(bool insert)
{
    m_insertMode = insert;
}

// 缓冲区管理
void TerminalWidget::allocateBuffers()
{
    m_historyBuffer.resize(m_historyLines);
    for (int i = 0; i < m_historyLines; ++i) {
        m_historyBuffer[i].resize(m_columns);
        for (int j = 0; j < m_columns; ++j) {
            m_historyBuffer[i][j].character = QChar(' ');
            m_historyBuffer[i][j].foreground = m_defaultForeground;
            m_historyBuffer[i][j].background = m_defaultBackground;
            m_historyBuffer[i][j].attributes = Normal;
        }
    }
    
    m_terminalBuffer.resize(m_terminalRows);
    for (int i = 0; i < m_terminalRows; ++i) {
        m_terminalBuffer[i].resize(m_columns);
        for (int j = 0; j < m_columns; ++j) {
            m_terminalBuffer[i][j].character = QChar(' ');
            m_terminalBuffer[i][j].foreground = m_defaultForeground;
            m_terminalBuffer[i][j].background = m_defaultBackground;
            m_terminalBuffer[i][j].attributes = Normal;
        }
    }
}

void TerminalWidget::updateScrollBars()
{
    int totalLines = m_historySize + m_terminalRows;
    verticalScrollBar()->setRange(0, qMax(0, totalLines - m_viewportLines));
    verticalScrollBar()->setPageStep(m_viewportLines);
    verticalScrollBar()->setSingleStep(1);
    
    horizontalScrollBar()->setRange(0, qMax(0, m_columns - viewport()->width() / m_charWidth));
    horizontalScrollBar()->setPageStep(viewport()->width() / m_charWidth);
    horizontalScrollBar()->setSingleStep(1);
}

void TerminalWidget::ensureCursorVisible()
{
    int cursorGlobalLine = m_historySize + m_cursorRow;
    int topLine = verticalScrollBar()->value();
    
    if (cursorGlobalLine < topLine) {
        verticalScrollBar()->setValue(cursorGlobalLine);
    } else if (cursorGlobalLine >= topLine + m_viewportLines) {
        verticalScrollBar()->setValue(cursorGlobalLine - m_viewportLines + 1);
    }
}

void TerminalWidget::scrollUpIfNeeded()
{
    if (m_cursorRow >= m_terminalRows) {
        scrollTerminalUp();
        m_cursorRow = m_terminalRows - 1;
    }
}

int TerminalWidget::historyIndex(int line) const
{
    if (line < 0 || line >= m_historySize) return -1;
    return (m_historyStart + line) % m_historyLines;
}

void TerminalWidget::shiftLinesUp(int count)
{
    for (int i = 0; i < count; ++i) {
        appendHistoryLine();
    }
}

void TerminalWidget::appendHistoryLine()
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
    
    // 上移终端缓冲区
    for (int row = 0; row < m_terminalRows - 1; ++row) {
        m_terminalBuffer[row] = m_terminalBuffer[row + 1];
    }
    
    // 清除最后一行
    for (int col = 0; col < m_columns; ++col) {
        Cell &cell = m_terminalBuffer[m_terminalRows - 1][col];
        cell.character = QChar(' ');
        cell.foreground = m_defaultForeground;
        cell.background = m_defaultBackground;
        cell.attributes = Normal;
    }
}

void TerminalWidget::scrollTerminalUp()
{
    appendHistoryLine();
}

// 绘图事件
void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), m_defaultBackground);
    
    QFont font = painter.font();
    font.setFamily("Courier New");
    font.setFixedPitch(true);
    painter.setFont(font);
    
    QFontMetrics fm(font);
    m_charWidth = fm.horizontalAdvance('X');
    m_charHeight = fm.height();
    
    int topLine = verticalScrollBar()->value();
    int leftColumn = horizontalScrollBar()->value() / m_charWidth;
    int visibleColumns = viewport()->width() / m_charWidth + 1;
    int visibleLines = viewport()->height() / m_charHeight + 1;
    int totalLines = m_historySize + m_terminalRows;
    
    for (int line = topLine; line < topLine + visibleLines && line < totalLines; ++line) {
        int lineInView = line - topLine;
        if (line < m_historySize) {
            int idx = historyIndex(line);
            if (idx < 0) continue;
            
            for (int col = leftColumn; col < leftColumn + visibleColumns && col < m_columns; ++col) {
                const Cell &cell = m_historyBuffer[idx][col];
                QColor fg = cell.foreground;
                QColor bg = cell.background;
                if (cell.attributes & Reverse) {
                    qSwap(fg, bg);
                }
                painter.setPen(fg);
                if (bg != m_defaultBackground) {
                    painter.fillRect(col * m_charWidth - leftColumn * m_charWidth,
                                     lineInView * m_charHeight,
                                     m_charWidth, m_charHeight, bg);
                }
                
                QFont font = painter.font();
                if (cell.attributes & Bold) {
                    font.setBold(true);
                }
                if (cell.attributes & Underline) {
                    font.setUnderline(true);
                }
                painter.setFont(font);
                
                painter.drawText(col * m_charWidth - leftColumn * m_charWidth,
                                 lineInView * m_charHeight + fm.ascent(),
                                 QString(cell.character));
                
                // 恢复字体
                if (cell.attributes & (Bold | Underline)) {
                    font.setBold(false);
                    font.setUnderline(false);
                    painter.setFont(font);
                }
            }
        } else {
            int terminalLine = line - m_historySize;
            if (terminalLine < 0 || terminalLine >= m_terminalRows) continue;
            
            for (int col = leftColumn; col < leftColumn + visibleColumns && col < m_columns; ++col) {
                const Cell &cell = m_terminalBuffer[terminalLine][col];
                QColor fg = cell.foreground;
                QColor bg = cell.background;
                if (cell.attributes & Reverse) {
                    qSwap(fg, bg);
                }
                painter.setPen(fg);
                if (bg != m_defaultBackground) {
                    painter.fillRect(col * m_charWidth - leftColumn * m_charWidth,
                                     lineInView * m_charHeight,
                                     m_charWidth, m_charHeight, bg);
                }
                
                QFont font = painter.font();
                if (cell.attributes & Bold) {
                    font.setBold(true);
                }
                if (cell.attributes & Underline) {
                    font.setUnderline(true);
                }
                painter.setFont(font);
                
                painter.drawText(col * m_charWidth - leftColumn * m_charWidth,
                                 lineInView * m_charHeight + fm.ascent(),
                                 QString(cell.character));
                
                // 恢复字体
                if (cell.attributes & (Bold | Underline)) {
                    font.setBold(false);
                    font.setUnderline(false);
                    painter.setFont(font);
                }
            }
        }
    }
    
    // 绘制光标
    if (hasFocus()) {
        int cursorGlobalLine = m_historySize + m_cursorRow;
        if (cursorGlobalLine >= topLine && cursorGlobalLine < topLine + visibleLines) {
            int cursorLineInView = cursorGlobalLine - topLine;
            painter.setPen(Qt::white);
            painter.drawRect(m_cursorColumn * m_charWidth - leftColumn * m_charWidth,
                             cursorLineInView * m_charHeight,
                             m_charWidth, m_charHeight);
        }
    }
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    viewport()->update();
}

void TerminalWidget::scrollContentsBy(int dx, int dy)
{
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    viewport()->update();
}

// 键盘事件处理
void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_isConnected) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    
    QString text = event->text();
    if (!text.isEmpty()) {
        // 发送输入的字符到连接
        sendText(text);
        
        // 本地回显（可选）
        // writeText(text);
    } else {
        // 处理特殊键
        switch (event->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                sendData(QByteArray("\r\n", 2));
                break;
            case Qt::Key_Backspace:
                sendData(QByteArray("\x08", 1));
                break;
            case Qt::Key_Tab:
                sendData(QByteArray("\t", 1));
                break;
            case Qt::Key_Escape:
                sendData(QByteArray("\x1b", 1));
                break;
            default:
                QAbstractScrollArea::keyPressEvent(event);
                break;
        }
    }
}

// 以下是连接管理方法的实现（从之前版本复制）
// 连接VT100解析器信号
void TerminalWidget::connectVT100ParserSignals()
{
    if (!m_vt100Parser) return;
    
    connect(m_vt100Parser, &VT100Parser::textReceived, this, &TerminalWidget::onTextReceived);
    connect(m_vt100Parser, &VT100Parser::setAttributes, this, &TerminalWidget::onSetAttributes);
    connect(m_vt100Parser, &VT100Parser::resetAttributes, this, &TerminalWidget::onResetAttributes);
    connect(m_vt100Parser, &VT100Parser::cursorUp, this, &TerminalWidget::onCursorUp);
    connect(m_vt100Parser, &VT100Parser::cursorDown, this, &TerminalWidget::onCursorDown);
    connect(m_vt100Parser, &VT100Parser::cursorForward, this, &TerminalWidget::onCursorForward);
    connect(m_vt100Parser, &VT100Parser::cursorBackward, this, &TerminalWidget::onCursorBackward);
    connect(m_vt100Parser, &VT100Parser::cursorPosition, this, &TerminalWidget::onCursorPosition);
    connect(m_vt100Parser, &VT100Parser::cursorHome, this, &TerminalWidget::onCursorHome);
    connect(m_vt100Parser, &VT100Parser::carriageReturn, this, &TerminalWidget::onCarriageReturn);
    connect(m_vt100Parser, &VT100Parser::lineFeed, this, &TerminalWidget::onLineFeed);
    connect(m_vt100Parser, &VT100Parser::clearScreen, this, &TerminalWidget::onClearScreen);
    connect(m_vt100Parser, &VT100Parser::clearLine, this, &TerminalWidget::onClearLine);
}

void TerminalWidget::onSetAttributes(const QVector<int> &params)
{
    for (int i = 0; i < params.size(); ++i) {
        int param = params[i];
        switch (param) {
            case 0: // 重置所有属性
                m_currentAttributes = Normal;
                m_currentForeground = m_defaultForeground;
                m_currentBackground = m_defaultBackground;
                break;
            case 1: // 粗体
                m_currentAttributes |= Bold;
                break;
            case 4: // 下划线
                m_currentAttributes |= Underline;
                break;
            case 5: // 闪烁
                m_currentAttributes |= Blink;
                break;
            case 7: // 反色
                m_currentAttributes |= Reverse;
                break;
            case 22: // 正常亮度（取消粗体）
                m_currentAttributes &= ~Bold;
                break;
            case 24: // 取消下划线
                m_currentAttributes &= ~Underline;
                break;
            case 25: // 取消闪烁
                m_currentAttributes &= ~Blink;
                break;
            case 27: // 取消反色
                m_currentAttributes &= ~Reverse;
                break;
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                // 标准前景色
                m_currentForeground = ansiColorToQColor(param - 30, false);
                break;
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                // 标准背景色
                m_currentBackground = ansiColorToQColor(param - 40, true);
                break;
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                // 明亮前景色
                m_currentForeground = ansiColorToQColor(param - 90 + 8, false);
                break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                // 明亮背景色
                m_currentBackground = ansiColorToQColor(param - 100 + 8, true);
                break;
            case 38: // 扩展前景色
            case 48: // 扩展背景色
                // 暂时不支持扩展颜色
                i++; // 跳过下一个参数
                if (i < params.size() && params[i] == 5) {
                    i++; // 跳过颜色类型
                    if (i < params.size()) {
                        // 256色索引，简化处理
                        int colorIndex = params[i];
                        QColor color = ansi256ColorToQColor(colorIndex);
                        if (param == 38) {
                            m_currentForeground = color;
                        } else {
                            m_currentBackground = color;
                        }
                    }
                } else if (i < params.size() && params[i] == 2) {
                    // RGB颜色，跳过三个参数
                    i += 3;
                }
                break;
            default:
                // 忽略未知参数
                break;
        }
    }
}

void TerminalWidget::onResetAttributes()
{
    m_currentAttributes = Normal;
    m_currentForeground = m_defaultForeground;
    m_currentBackground = m_defaultBackground;
}

static QColor ansiColorToQColor(int code, bool isBright)
{
    static const QColor standardColors[] = {
        Qt::black,      // 0: 黑
        Qt::red,        // 1: 红
        Qt::green,      // 2: 绿
        Qt::yellow,     // 3: 黄
        Qt::blue,       // 4: 蓝
        Qt::magenta,    // 5: 洋红
        Qt::cyan,       // 6: 青
        Qt::white,      // 7: 白
    };
    
    static const QColor brightColors[] = {
        QColor(128, 128, 128),  // 8: 灰（亮黑）
        QColor(255, 128, 128),  // 9: 亮红
        QColor(128, 255, 128),  // 10: 亮绿
        QColor(255, 255, 128),  // 11: 亮黄
        QColor(128, 128, 255),  // 12: 亮蓝
        QColor(255, 128, 255),  // 13: 亮洋红
        QColor(128, 255, 255),  // 14: 亮青
        QColor(255, 255, 255),  // 15: 亮白
    };
    
    if (code >= 0 && code < 8) {
        return standardColors[code];
    } else if (code >= 8 && code < 16) {
        return brightColors[code - 8];
    } else if (code >= 16 && code < 232) {
        // 216种颜色
        int r = (code - 16) / 36;
        int g = ((code - 16) % 36) / 6;
        int b = (code - 16) % 6;
        r = r > 0 ? 55 + r * 40 : 0;
        g = g > 0 ? 55 + g * 40 : 0;
        b = b > 0 ? 55 + b * 40 : 0;
        return QColor(r, g, b);
    } else if (code >= 232 && code < 256) {
        // 灰度
        int gray = (code - 232) * 10 + 8;
        return QColor(gray, gray, gray);
    }
    
    return isBright ? Qt::white : Qt::black;
}

static QColor ansi256ColorToQColor(int code)
{
    return ansiColorToQColor(code, false);
}

void TerminalWidget::setConnectionSettings(const ConnectionDialog::ConnectionSettings &settings)
{
    m_connectionSettings = settings;
    qDebug() << "TerminalWidget: 设置连接参数，类型 =" << settings.type
             << "会话名称 =" << settings.sessionName;
}

bool TerminalWidget::openConnection()
{
    if (m_isConnected) {
        closeConnection();
    }
    
    bool success = false;
    switch (m_connectionSettings.type) {
        case ConnectionDialog::Serial:
            success = openSerialConnection();
            break;
        case ConnectionDialog::Telnet:
            success = openTelnetConnection();
            break;
        case ConnectionDialog::SSH:
            success = openSshConnection();
            break;
        default:
            emit connectionError("未知的连接类型");
            return false;
    }
    
    if (success) {
        m_isConnected = true;
        emit connectionOpened();
    }
    
    return success;
}

void TerminalWidget::closeConnection()
{
    if (!m_isConnected) return;
    
    switch (m_connectionSettings.type) {
        case ConnectionDialog::Serial:
            closeSerialConnection();
            break;
        case ConnectionDialog::Telnet:
            closeTelnetConnection();
            break;
        case ConnectionDialog::SSH:
            closeSshConnection();
            break;
    }
    
    m_isConnected = false;
    emit connectionClosed();
}

bool TerminalWidget::isConnected() const
{
    return m_isConnected;
}

void TerminalWidget::sendData(const QByteArray &data)
{
    if (!m_isConnected) {
        emit connectionError("未连接");
        return;
    }
    
    switch (m_connectionSettings.type) {
        case ConnectionDialog::Serial:
            if (m_serialPort && m_serialPort->isOpen()) {
                qint64 bytesWritten = m_serialPort->write(data);
                if (bytesWritten == data.size()) {
                    emit dataSent(data);
                } else {
                    emit connectionError("串口发送失败");
                }
            }
            break;
        case ConnectionDialog::Telnet:
            if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
                qint64 bytesWritten = m_tcpSocket->write(data);
                if (bytesWritten == data.size()) {
                    emit dataSent(data);
                } else {
                    emit connectionError("网络发送失败");
                }
            }
            break;
        case ConnectionDialog::SSH:
            if (m_sshProcess && m_sshProcess->state() == QProcess::Running) {
                m_sshProcess->write(data);
                emit dataSent(data);
            }
            break;
    }
}

void TerminalWidget::sendText(const QString &text)
{
    sendData(text.toUtf8());
}

ConnectionDialog::ConnectionSettings TerminalWidget::getConnectionSettings() const
{
    return m_connectionSettings;
}

bool TerminalWidget::openSerialConnection()
{
    if (m_serialPort) {
        delete m_serialPort;
    }
    
    m_serialPort = new QSerialPort(this);
    m_serialPort->setPortName(m_connectionSettings.portName);
    m_serialPort->setBaudRate(m_connectionSettings.baudRate);
    m_serialPort->setDataBits(m_connectionSettings.dataBits);
    m_serialPort->setParity(m_connectionSettings.parity);
    m_serialPort->setStopBits(m_connectionSettings.stopBits);
    m_serialPort->setFlowControl(m_connectionSettings.flowControl);
    
    connect(m_serialPort, &QSerialPort::readyRead, this, &TerminalWidget::onSerialDataReady);
    connect(m_serialPort, &QSerialPort::errorOccurred, this, &TerminalWidget::onSerialError);
    
    if (m_serialPort->open(QIODevice::ReadWrite)) {
        qDebug() << "串口连接已打开:" << m_connectionSettings.portName
                 << "波特率:" << m_connectionSettings.baudRate;
        return true;
    } else {
        QString error = QString("无法打开串口 %1: %2")
                          .arg(m_connectionSettings.portName)
                          .arg(m_serialPort->errorString());
        emit connectionError(error);
        delete m_serialPort;
        m_serialPort = nullptr;
        return false;
    }
}

void TerminalWidget::closeSerialConnection()
{
    if (m_serialPort) {
        m_serialPort->close();
        disconnect(m_serialPort, &QSerialPort::readyRead, this, &TerminalWidget::onSerialDataReady);
        disconnect(m_serialPort, &QSerialPort::errorOccurred, this, &TerminalWidget::onSerialError);
        delete m_serialPort;
        m_serialPort = nullptr;
    }
}

bool TerminalWidget::openTelnetConnection()
{
    if (m_tcpSocket) {
        delete m_tcpSocket;
    }
    
    m_tcpSocket = new QTcpSocket(this);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &TerminalWidget::onTcpDataReady);
    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &TerminalWidget::onTcpError);
    connect(m_tcpSocket, &QTcpSocket::connected, this, [this]() {
        qDebug() << "Telnet连接已建立:" << m_connectionSettings.host << ":" << m_connectionSettings.port;
    });
    
    m_tcpSocket->connectToHost(m_connectionSettings.host, m_connectionSettings.port);
    
    if (m_tcpSocket->waitForConnected(5000)) {
        return true;
    } else {
        QString error = QString("无法连接到Telnet服务器 %1:%2: %3")
                          .arg(m_connectionSettings.host)
                          .arg(m_connectionSettings.port)
                          .arg(m_tcpSocket->errorString());
        emit connectionError(error);
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
        return false;
    }
}

void TerminalWidget::closeTelnetConnection()
{
    if (m_tcpSocket) {
        m_tcpSocket->disconnectFromHost();
        if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
            m_tcpSocket->waitForDisconnected(1000);
        }
        disconnect(m_tcpSocket, &QTcpSocket::readyRead, this, &TerminalWidget::onTcpDataReady);
        disconnect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &TerminalWidget::onTcpError);
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
    }
}

bool TerminalWidget::openSshConnection()
{
    if (m_sshProcess) {
        delete m_sshProcess;
        m_sshProcess = nullptr;
    }
    
    m_sshProcess = new QProcess(this);
    m_sshProcess->setProcessChannelMode(QProcess::MergedChannels);
    
    connect(m_sshProcess, &QProcess::readyRead, this, &TerminalWidget::onSshProcessReadyRead);
    connect(m_sshProcess, &QProcess::errorOccurred, this, &TerminalWidget::onSshProcessErrorOccurred);
    connect(m_sshProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::CrashExit || exitCode != 0) {
                    emit connectionError(QString("SSH进程异常退出，代码: %1").arg(exitCode));
                }
                closeConnection();
            });
    
    QString program = "ssh";
    QStringList arguments;
    
    arguments << "-o" << "StrictHostKeyChecking=no"
              << "-o" << "LogLevel=ERROR";
    
    if (!m_connectionSettings.username.isEmpty()) {
        arguments << QString("%1@%2").arg(m_connectionSettings.username, m_connectionSettings.host);
    } else {
        arguments << m_connectionSettings.host;
    }
    
    arguments << "-p" << QString::number(m_connectionSettings.port);
    
    if (m_connectionSettings.useSshKey && !m_connectionSettings.sshKeyPath.isEmpty()) {
        arguments << "-i" << m_connectionSettings.sshKeyPath;
    }
    
    qDebug() << "启动SSH进程:" << program << arguments;
    
    m_sshProcess->start(program, arguments);
    
    if (m_sshProcess->waitForStarted(5000)) {
        qDebug() << "SSH进程已启动，PID:" << m_sshProcess->processId();
        return true;
    } else {
        QString error = QString("无法启动SSH进程: %1").arg(m_sshProcess->errorString());
        emit connectionError(error);
        delete m_sshProcess;
        m_sshProcess = nullptr;
        return false;
    }
}

void TerminalWidget::closeSshConnection()
{
    if (m_sshProcess) {
        if (m_sshProcess->state() == QProcess::Running) {
            m_sshProcess->terminate();
            if (!m_sshProcess->waitForFinished(1000)) {
                m_sshProcess->kill();
            }
        }
        disconnect(m_sshProcess, &QProcess::readyRead, this, &TerminalWidget::onSshProcessReadyRead);
        disconnect(m_sshProcess, &QProcess::errorOccurred, this, &TerminalWidget::onSshProcessErrorOccurred);
        delete m_sshProcess;
        m_sshProcess = nullptr;
    }
}

void TerminalWidget::onSshProcessReadyRead()
{
    if (m_sshProcess && m_sshProcess->state() == QProcess::Running) {
        QByteArray data = m_sshProcess->readAll();
        processReceivedData(data);
    }
}

void TerminalWidget::onSshProcessErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::UnknownError) return;
    
    QString errorMsg = QString("SSH进程错误: %1").arg(m_sshProcess ? m_sshProcess->errorString() : "未知错误");
    emit connectionError(errorMsg);
    
    if (error == QProcess::FailedToStart || error == QProcess::Crashed) {
        closeConnection();
    }
}

void TerminalWidget::onSerialDataReady()
{
    if (m_serialPort && m_serialPort->isOpen()) {
        QByteArray data = m_serialPort->readAll();
        processReceivedData(data);
    }
}

void TerminalWidget::onTcpDataReady()
{
    if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QByteArray data = m_tcpSocket->readAll();
        processReceivedData(data);
    }
}

void TerminalWidget::processReceivedData(const QByteArray &data)
{
    emit dataReceived(data);
    
    if (m_vt100Parser) {
        m_vt100Parser->processData(data);
    }
}

void TerminalWidget::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;
    
    QString errorMsg = QString("串口错误: %1").arg(m_serialPort->errorString());
    emit connectionError(errorMsg);
    
    if (error == QSerialPort::ResourceError) {
        closeConnection();
    }
}

void TerminalWidget::onTcpError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::UnknownSocketError) return;
    
    QString errorMsg = QString("网络错误: %1").arg(m_tcpSocket->errorString());
    emit connectionError(errorMsg);
    
    if (error == QAbstractSocket::ConnectionRefusedError ||
        error == QAbstractSocket::RemoteHostClosedError ||
        error == QAbstractSocket::NetworkError) {
        closeConnection();
    }
}

// VT100解析器信号处理
void TerminalWidget::onTextReceived(const QString &text)
{
    writeText(text);
}

void TerminalWidget::onCursorUp(int count)
{
    int newRow = m_cursorRow - count;
    if (newRow < 0) newRow = 0;
    setCursor(m_cursorColumn, newRow);
}

void TerminalWidget::onCursorDown(int count)
{
    int newRow = m_cursorRow + count;
    if (newRow >= m_terminalRows) newRow = m_terminalRows - 1;
    setCursor(m_cursorColumn, newRow);
}

void TerminalWidget::onCursorForward(int count)
{
    int newCol = m_cursorColumn + count;
    if (newCol >= m_columns) newCol = m_columns - 1;
    setCursor(newCol, m_cursorRow);
}

void TerminalWidget::onCursorBackward(int count)
{
    int newCol = m_cursorColumn - count;
    if (newCol < 0) newCol = 0;
    setCursor(newCol, m_cursorRow);
}

void TerminalWidget::onCursorPosition(int row, int column)
{
    int actualRow = qMax(0, row - 1);
    int actualCol = qMax(0, column - 1);
    setCursor(actualCol, actualRow);
}

void TerminalWidget::onCursorHome()
{
    setCursor(0, 0);
}

void TerminalWidget::onCarriageReturn()
{
    carriageReturn();
}

void TerminalWidget::onLineFeed()
{
    newLine();
}

void TerminalWidget::onClearScreen()
{
    clearScreen();
}

void TerminalWidget::onClearLine()
{
    clearLine();
}