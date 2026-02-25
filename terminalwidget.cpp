#include "terminalwidget.h"
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
    , m_selecting(false)
    , m_hasSelection(false)
    , m_serialPort(nullptr)
    , m_tcpSocket(nullptr)
    , m_sshProcess(nullptr)
    , m_vt100Parser(nullptr)
    , m_isConnected(false)
{
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // 设置视口也接收鼠标事件
    viewport()->setMouseTracking(true);
    
    // 初始化VT100解析器
    m_vt100Parser = new VT100Parser(this);
    connectVT100ParserSignals();
    
    allocateBuffers();
    updateScrollBars();
}

TerminalWidget::~TerminalWidget()
{
    // 安全释放鼠标捕获，防止在捕获状态下销毁对象
    if (m_selecting) {
        m_selecting = false;
        m_hasSelection = false;
        viewport()->setMouseTracking(false);
    }
    
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
    qDebug() << "[TerminalWidget] writeText - 写入文本:" << text << "长度:" << text.length();
    for (const QChar &ch : text) {
        writeChar(ch);
    }
    viewport()->update();
    ensureCursorVisible();
}

void TerminalWidget::writeChar(QChar ch)
{
    qDebug() << "[TerminalWidget] writeChar - 字符:" << ch << "光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    
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
        
        qDebug() << "[TerminalWidget] writeChar - 写入缓冲区位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
        
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
    ensureCursorVisible();
}

void TerminalWidget::carriageReturn()
{
    qDebug() << "[TerminalWidget] carriageReturn - 设置光标列从" << m_cursorColumn << "到0，行保持" << m_cursorRow;
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
    qDebug() << "[TerminalWidget] clearScreen - 开始清除屏幕，当前光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")"
             << "历史大小:" << m_historySize << "历史起始:" << m_historyStart
             << "滚动位置:" << verticalScrollBar()->value();
    // 1. 清空terminal buffer数据
    for (int row = 0; row < m_terminalRows; ++row) {
        for (int col = 0; col < m_columns; ++col) {
            Cell &cell = m_terminalBuffer[row][col];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
    }
    
    // 2. 清空历史缓冲区（丢弃所有历史数据）
    m_historyStart = 0;
    m_historySize = 0;
    
    // 3. 重置光标位置
    m_cursorColumn = 0;
    m_cursorRow = 0;
    
    // 4. 更新滚动条范围（历史缓冲区已清空，terminal buffer从第0行开始）
    updateScrollBars();
    
    // 5. 将滚动条位置设置为0，使terminal buffer从视口顶部开始显示
    verticalScrollBar()->setValue(0);
    
    // 6. 确保光标可见（光标在terminal buffer的(0,0)位置）
    ensureCursorVisible();
    
    // 7. 清除任何文本选择
    clearSelection();
    
    // 8. 更新视图
    viewport()->update();
    
    qDebug() << "[TerminalWidget] clearScreen - 清除完成，最终光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")"
             << "历史大小:" << m_historySize << "历史起始:" << m_historyStart
             << "滚动位置:" << verticalScrollBar()->value();
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
    
    qDebug() << "[TerminalWidget] updateScrollBars - 历史大小:" << m_historySize 
             << "终端行数:" << m_terminalRows << "总行数:" << totalLines;
    
    // 计算实际可视行数（向下取整，确保只显示完整行）
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    int visibleLines = viewport()->height() / charHeight; // 向下取整，只计算完整行
    if (visibleLines <= 0) visibleLines = 1;
    
    verticalScrollBar()->setRange(0, qMax(0, totalLines - visibleLines));
    verticalScrollBar()->setPageStep(visibleLines);
    verticalScrollBar()->setSingleStep(1);
    
    int charWidth = m_charWidth > 0 ? m_charWidth : 10;
    int visibleColumns = viewport()->width() / charWidth;
    if (visibleColumns <= 0) visibleColumns = 1;
    
    horizontalScrollBar()->setRange(0, qMax(0, m_columns - visibleColumns));
    horizontalScrollBar()->setPageStep(visibleColumns);
    horizontalScrollBar()->setSingleStep(1);
    
    qDebug() << "[TerminalWidget] updateScrollBars - 可见行数:" << visibleLines 
             << "垂直范围: 0 -" << qMax(0, totalLines - visibleLines)
             << "当前值:" << verticalScrollBar()->value()
             << "光标绝对行:" << (m_historySize + m_cursorRow)
             << "光标行:" << m_cursorRow << "光标列:" << m_cursorColumn;
}

void TerminalWidget::ensureCursorVisible()
{
    qDebug() << "[TerminalWidget] ensureCursorVisible - 开始，历史大小:" << m_historySize
             << "光标行:" << m_cursorRow << "光标列:" << m_cursorColumn;
    int cursorGlobalLine = m_historySize + m_cursorRow;
    int topLine = verticalScrollBar()->value();
    
    // 计算实际可视行数（向下取整，确保只显示完整行）
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    int visibleLines = viewport()->height() / charHeight; // 向下取整，只计算完整行
    if (visibleLines <= 0) visibleLines = 1;
    
    // 获取当前滚动条范围以确保不设置超出范围的值
    int maxScroll = verticalScrollBar()->maximum();
    
    if (cursorGlobalLine < topLine) {
        // 光标在可视区域上方，滚动到光标位置
        verticalScrollBar()->setValue(qMin(cursorGlobalLine, maxScroll));
    } else if (cursorGlobalLine >= topLine + visibleLines) {
        // 光标在可视区域下方，滚动使光标在底部可见
        int newTopLine = cursorGlobalLine - visibleLines + 1;
        verticalScrollBar()->setValue(qMin(newTopLine, maxScroll));
    }
    
    qDebug() << "[TerminalWidget] ensureCursorVisible - 结束，光标绝对行:" << cursorGlobalLine
             << "topLine:" << topLine << "新topLine:" << verticalScrollBar()->value()
             << "可见行数:" << visibleLines << "最大滚动:" << maxScroll
             << "调整:" << (cursorGlobalLine < topLine ? "向上滚动" : (cursorGlobalLine >= topLine + visibleLines ? "向下滚动" : "无需调整"));
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
    qDebug() << "[TerminalWidget] appendHistoryLine - 开始，历史大小:" << m_historySize 
             << "历史起始:" << m_historyStart << "历史最大行数:" << m_historyLines;
    
    if (m_historySize < m_historyLines) {
        m_historySize++;
    } else {
        m_historyStart = (m_historyStart + 1) % m_historyLines;
    }
    
    qDebug() << "[TerminalWidget] appendHistoryLine - 更新后，历史大小:" << m_historySize 
             << "历史起始:" << m_historyStart;
    
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
    
    // 更新滚动条范围，因为历史大小可能已更改
    updateScrollBars();
}

void TerminalWidget::scrollTerminalUp()
{
    appendHistoryLine();
    ensureCursorVisible();
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
    int visibleColumns = viewport()->width() / m_charWidth; // 向下取整，只计算完整列
    int visibleLines = viewport()->height() / m_charHeight; // 向下取整，只计算完整行
    
    qDebug() << "[TerminalWidget] paintEvent - topLine:" << topLine << "leftColumn:" << leftColumn
             << "visibleLines:" << visibleLines << "visibleColumns:" << visibleColumns
             << "历史大小:" << m_historySize << "终端行数:" << m_terminalRows
             << "光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")"
             << "总行数:" << (m_historySize + m_terminalRows)
             << "空白行数:" << topLine
             << "显示范围: 行" << topLine << "-" << (topLine + visibleLines - 1);
    
    // 确保至少显示一行和一列
    if (visibleColumns <= 0) visibleColumns = 1;
    if (visibleLines <= 0) visibleLines = 1;
    
    int totalLines = m_historySize + m_terminalRows;
    
    for (int line = topLine; line < topLine + visibleLines && line < totalLines; ++line) {
        int lineInView = line - topLine;
        
        // 绘制选择背景（如果有）
        drawSelectionBackground(painter, line, lineInView, topLine, leftColumn, visibleLines, visibleColumns);
        
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
    qDebug() << "[TerminalWidget] keyPressEvent - 按键:" << event->key() 
             << "文本:" << event->text() << "修饰符:" << event->modifiers()
             << "连接状态:" << m_isConnected;
    
    // 首先处理终端相关的快捷键
    if (event->modifiers() & Qt::ControlModifier) {
        switch (event->key()) {
            case Qt::Key_C: // Ctrl+C
                if (hasSelection()) {
                    copyToClipboard();
                } else {
                    // 如果没有选择，发送Ctrl+C中断信号
                    if (m_isConnected) {
                        sendData(QByteArray("\x03", 1)); // ASCII ETX (Ctrl+C)
                    }
                }
                return;
            case Qt::Key_V: // Ctrl+V
                if (QApplication::clipboard()->text().isEmpty()) {
                    QAbstractScrollArea::keyPressEvent(event);
                } else {
                    // 粘贴剪贴板文本
                    QString text = QApplication::clipboard()->text();
                    sendText(text);
                }
                return;
            case Qt::Key_A: // Ctrl+A
                m_selectionStart = {0, 0};
                m_selectionEnd = {m_historySize + m_terminalRows - 1, m_columns - 1};
                m_hasSelection = true;
                viewport()->update();
                return;
            case Qt::Key_L: // Ctrl+L
                if (m_isConnected) {
                    // 发送VT100清屏序列（ESC [ 2J）
                    sendData(QByteArray("\x1b[2J", 4));
                } else {
                    // 未连接时本地清屏
                    clearScreen();
                }
                return;
        }
    }
    
    if (!m_isConnected && event->key() != Qt::Key_Tab && event->key() != Qt::Key_Backtab) {
        qDebug() << "[TerminalWidget] 未连接且非Tab键，调用父类处理";
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    qDebug() << "[TerminalWidget] 通过连接检查，继续处理按键";
    
    QString text = event->text();
    if (!text.isEmpty()) {
        qDebug() << "[TerminalWidget] 发送文本:" << text << "长度:" << text.length();
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
            case Qt::Key_Backtab:
                qDebug() << "[TerminalWidget] 处理Tab/Backtab键，连接状态:" << m_isConnected;
                if (m_isConnected) {
                    qDebug() << "[TerminalWidget] 发送制表符到连接";
                    sendData(QByteArray("\t", 1));
                } else {
                    qDebug() << "[TerminalWidget] 未连接，Tab键被忽略";
                }
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
    connect(m_vt100Parser, &VT100Parser::eraseDown, this, &TerminalWidget::onEraseDown);
    connect(m_vt100Parser, &VT100Parser::eraseUp, this, &TerminalWidget::onEraseUp);
    connect(m_vt100Parser, &VT100Parser::clearLine, this, &TerminalWidget::onClearLine);
    connect(m_vt100Parser, &VT100Parser::eraseEndOfLine, this, &TerminalWidget::onEraseEndOfLine);
    connect(m_vt100Parser, &VT100Parser::eraseStartOfLine, this, &TerminalWidget::onEraseStartOfLine);
    connect(m_vt100Parser, &VT100Parser::backspace, this, &TerminalWidget::onBackspace);
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
    qDebug() << "[TerminalWidget] sendData - 数据大小:" << data.size() 
             << "内容:" << data.toHex() << "连接状态:" << m_isConnected
             << "连接类型:" << m_connectionSettings.type;
    
    if (!m_isConnected) {
        qDebug() << "[TerminalWidget] sendData失败：未连接";
        emit connectionError("未连接");
        return;
    }
    
    switch (m_connectionSettings.type) {
        case ConnectionDialog::Serial:
            qDebug() << "[TerminalWidget] 通过串口发送数据，串口状态:" 
                     << (m_serialPort ? "存在" : "null") 
                     << (m_serialPort && m_serialPort->isOpen() ? "已打开" : "未打开");
            if (m_serialPort && m_serialPort->isOpen()) {
                qint64 bytesWritten = m_serialPort->write(data);
                qDebug() << "[TerminalWidget] 串口写入字节数:" << bytesWritten << "期望:" << data.size();
                if (bytesWritten == data.size()) {
                    qDebug() << "[TerminalWidget] 串口发送成功";
                    emit dataSent(data);
                } else {
                    qDebug() << "[TerminalWidget] 串口发送失败";
                    emit connectionError("串口发送失败");
                }
            } else {
                qDebug() << "[TerminalWidget] 串口不可用，无法发送数据";
            }
            break;
        case ConnectionDialog::Telnet:
            qDebug() << "[TerminalWidget] 通过Telnet发送数据，Socket状态:" 
                     << (m_tcpSocket ? "存在" : "null")
                     << (m_tcpSocket ? m_tcpSocket->state() : QAbstractSocket::UnconnectedState);
            if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
                qint64 bytesWritten = m_tcpSocket->write(data);
                qDebug() << "[TerminalWidget] Telnet写入字节数:" << bytesWritten << "期望:" << data.size();
                if (bytesWritten == data.size()) {
                    qDebug() << "[TerminalWidget] Telnet发送成功";
                    emit dataSent(data);
                } else {
                    qDebug() << "[TerminalWidget] Telnet发送失败";
                    emit connectionError("网络发送失败");
                }
            } else {
                qDebug() << "[TerminalWidget] Telnet连接不可用，无法发送数据";
            }
            break;
        case ConnectionDialog::SSH:
            qDebug() << "[TerminalWidget] 通过SSH发送数据，进程状态:" 
                     << (m_sshProcess ? "存在" : "null")
                     << (m_sshProcess ? m_sshProcess->state() : QProcess::NotRunning);
            if (m_sshProcess && m_sshProcess->state() == QProcess::Running) {
                qDebug() << "[TerminalWidget] SSH写入数据，大小:" << data.size();
                m_sshProcess->write(data);
                qDebug() << "[TerminalWidget] SSH发送完成";
                emit dataSent(data);
            } else {
                qDebug() << "[TerminalWidget] SSH进程不可用，无法发送数据";
            }
            break;
    }
}

void TerminalWidget::sendText(const QString &text)
{
    qDebug() << "[TerminalWidget] sendText - 文本:" << text << "UTF-8长度:" << text.toUtf8().size();
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
    
    m_sshProcess->start(program, arguments);
    
    if (m_sshProcess->waitForStarted(5000)) {
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
        qDebug() << "[TerminalWidget] SSH收到数据，大小:" << data.size() << "内容:" << data.toHex();
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
        qDebug() << "[TerminalWidget] 串口收到数据，大小:" << data.size() << "内容:" << data.toHex();
        processReceivedData(data);
    }
}

void TerminalWidget::onTcpDataReady()
{
    if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QByteArray data = m_tcpSocket->readAll();
        qDebug() << "[TerminalWidget] Telnet收到数据，大小:" << data.size() << "内容:" << data.toHex();
        processReceivedData(data);
    }
}

void TerminalWidget::processReceivedData(const QByteArray &data)
{
    qDebug() << "[TerminalWidget] processReceivedData - 数据大小:" << data.size() 
             << "内容:" << data.toHex() << "ASCII:" << QString(data).left(100);
    
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
    qDebug() << "[TerminalWidget] onTextReceived - 接收文本:" << text << "长度:" << text.length();
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

void TerminalWidget::onBackspace()
{
    qDebug() << "[TerminalWidget] onBackspace - 当前光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    if (m_cursorColumn > 0) {
        m_cursorColumn--;
        // 可选：清除退格位置的字符
        if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows &&
            m_cursorColumn >= 0 && m_cursorColumn < m_columns) {
            Cell &cell = m_terminalBuffer[m_cursorRow][m_cursorColumn];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
        ensureCursorVisible();
        viewport()->update();
        qDebug() << "[TerminalWidget] onBackspace - 退格后光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    }
}

void TerminalWidget::onCarriageReturn()
{
    qDebug() << "[TerminalWidget] onCarriageReturn - 当前光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    carriageReturn();
    qDebug() << "[TerminalWidget] onCarriageReturn - 回车后光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    ensureCursorVisible();
}

void TerminalWidget::onLineFeed()
{
    newLine();
}

void TerminalWidget::onClearScreen()
{
    qDebug() << "[TerminalWidget] onClearScreen - 清除整个屏幕";
    clearScreen();
}

void TerminalWidget::onEraseDown()
{
    qDebug() << "[TerminalWidget] onEraseDown - 清除从光标到屏幕末尾，光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    
    // 如果光标在屏幕顶部，清空历史缓冲区以便新输出从视口顶部开始
    if (m_cursorRow == 0) {
        qDebug() << "[TerminalWidget] onEraseDown - 光标在屏幕顶部，清空历史缓冲区";
        m_historyStart = 0;
        m_historySize = 0;
        updateScrollBars();
        verticalScrollBar()->setValue(0);
    }
    
    // 清除从当前行到terminal buffer末尾的所有行
    for (int row = m_cursorRow; row < m_terminalRows; ++row) {
        for (int col = (row == m_cursorRow ? m_cursorColumn : 0); col < m_columns; ++col) {
            Cell &cell = m_terminalBuffer[row][col];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
    }
    viewport()->update();
}

void TerminalWidget::onEraseUp()
{
    qDebug() << "[TerminalWidget] onEraseUp - 清除从屏幕开始到光标，光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    // 清除从terminal buffer开始到当前行的所有行
    for (int row = 0; row <= m_cursorRow; ++row) {
        int endCol = (row == m_cursorRow ? m_cursorColumn + 1 : m_columns);
        for (int col = 0; col < endCol; ++col) {
            Cell &cell = m_terminalBuffer[row][col];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
    }
    viewport()->update();
}

void TerminalWidget::onEraseEndOfLine()
{
    qDebug() << "[TerminalWidget] onEraseEndOfLine - 清除从光标到行尾，光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        for (int col = m_cursorColumn; col < m_columns; ++col) {
            Cell &cell = m_terminalBuffer[m_cursorRow][col];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
    }
    viewport()->update();
}

void TerminalWidget::onEraseStartOfLine()
{
    qDebug() << "[TerminalWidget] onEraseStartOfLine - 清除从行首到光标，光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        for (int col = 0; col <= m_cursorColumn; ++col) {
            Cell &cell = m_terminalBuffer[m_cursorRow][col];
            cell.character = QChar(' ');
            cell.foreground = m_defaultForeground;
            cell.background = m_defaultBackground;
            cell.attributes = Normal;
        }
    }
    viewport()->update();
}

void TerminalWidget::onClearLine()
{
    clearLine();
}

// 文本选择和复制功能实现

TerminalWidget::SelectionPoint TerminalWidget::viewportToBuffer(const QPoint &pos) const
{
    int charWidth = m_charWidth > 0 ? m_charWidth : 10;
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    
    int column = pos.x() / charWidth + horizontalScrollBar()->value();
    int line = pos.y() / charHeight + verticalScrollBar()->value();
    
    // 限制在有效范围内
    column = qMax(0, qMin(column, m_columns - 1));
    line = qMax(0, qMin(line, m_historySize + m_terminalRows - 1));
    
    return {line, column};
}

void TerminalWidget::updateSelection(const QPoint &pos)
{
    SelectionPoint bufferPos = viewportToBuffer(pos);
    
    if (m_selecting) {
        m_selectionEnd = bufferPos;
        normalizeSelection();
        viewport()->update();
    }
}

void TerminalWidget::normalizeSelection() const
{
    // 确保start在end之前（按行和列排序）
    if (m_selectionStart.line > m_selectionEnd.line ||
        (m_selectionStart.line == m_selectionEnd.line && m_selectionStart.column > m_selectionEnd.column)) {
        qSwap(m_selectionStart, m_selectionEnd);
    }
}

void TerminalWidget::drawSelectionBackground(QPainter &painter, int line, int lineInView, int topLine, int leftColumn, int visibleLines, int visibleColumns) const
{
    Q_UNUSED(topLine);
    Q_UNUSED(visibleLines);
    
    if (!m_hasSelection) return;
    
    int charWidth = m_charWidth > 0 ? m_charWidth : 10;
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    
    // 检查这一行是否在选择范围内
    if (line < m_selectionStart.line || line > m_selectionEnd.line) {
        return;
    }
    
    // 计算这一行的选择起始和结束列
    int startCol = (line == m_selectionStart.line) ? m_selectionStart.column : 0;
    int endCol = (line == m_selectionEnd.line) ? m_selectionEnd.column : m_columns - 1;
    
    // 调整为可视范围内的列
    if (endCol < leftColumn || startCol >= leftColumn + visibleColumns) {
        return;
    }
    
    startCol = qMax(startCol, leftColumn);
    endCol = qMin(endCol, leftColumn + visibleColumns - 1);
    
    // 绘制选择背景
    QColor selectionColor = QColor(100, 100, 255, 100); // 半透明蓝色
    painter.fillRect(startCol * charWidth - leftColumn * charWidth,
                     lineInView * charHeight,
                     (endCol - startCol + 1) * charWidth,
                     charHeight,
                     selectionColor);
}

// 鼠标事件处理
void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    // 检查鼠标是否按下在TerminalWidget视口范围内
    QPoint pos = event->pos();
    bool insideViewport = viewport()->rect().contains(pos);
    
    if (!insideViewport && m_selecting) {
        // 鼠标按下在控件外部，且正在进行选择，清除选择状态
        m_selecting = false;
        m_hasSelection = false;
        viewport()->setMouseTracking(false);
        viewport()->update();
        
        // 让事件继续传递
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    
    if (event->button() == Qt::LeftButton && insideViewport) {
        // 开始选择
        m_selecting = true;
        m_hasSelection = false;
        m_selectionStart = viewportToBuffer(event->pos());
        m_selectionEnd = m_selectionStart;
        
        // 确保我们有焦点
        if (!hasFocus()) {
            setFocus();
        }
        
        // 启用鼠标跟踪以接收鼠标移动事件（在视口上）
        // 注意：我们不使用grabMouse()，避免捕获所有鼠标事件
        viewport()->setMouseTracking(true);
        viewport()->update();
    }
    
    QAbstractScrollArea::mousePressEvent(event);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting) {
        // 检查鼠标是否在视口范围内，避免在外部控件上更新选择
        QPoint pos = event->pos();
        bool insideViewport = viewport()->rect().contains(pos);
        
        if (insideViewport) {
            updateSelection(event->pos());
        }
    }
    
    QAbstractScrollArea::mouseMoveEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // 如果正在进行选择操作，任何鼠标按钮释放都应该结束选择
    if (m_selecting) {
        m_selecting = false;
        
        // 只有左键释放时才确认选择，其他按钮释放时清除选择
        if (event->button() == Qt::LeftButton) {
            m_hasSelection = (m_selectionStart != m_selectionEnd);
        } else {
            m_hasSelection = false;
        }
        
        // 禁用视口的鼠标跟踪
        viewport()->setMouseTracking(false);
        
        viewport()->update();
    }
    
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 双击选择当前单词
        SelectionPoint pos = viewportToBuffer(event->pos());
        
        // 简化实现：选择当前行
        m_selectionStart = {pos.line, 0};
        m_selectionEnd = {pos.line, m_columns - 1};
        m_hasSelection = true;
        
        viewport()->update();
    }
    
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    
    QAction *copyAction = new QAction("复制", &menu);
    copyAction->setEnabled(m_hasSelection);
    connect(copyAction, &QAction::triggered, this, &TerminalWidget::copyToClipboard);
    
    QAction *selectAllAction = new QAction("全选", &menu);
    connect(selectAllAction, &QAction::triggered, this, [this]() {
        m_selectionStart = {0, 0};
        m_selectionEnd = {m_historySize + m_terminalRows - 1, m_columns - 1};
        m_hasSelection = true;
        viewport()->update();
    });
    
    QAction *clearSelectionAction = new QAction("清除选择", &menu);
    clearSelectionAction->setEnabled(m_hasSelection);
    connect(clearSelectionAction, &QAction::triggered, this, &TerminalWidget::clearSelection);
    
    menu.addAction(copyAction);
    menu.addAction(selectAllAction);
    menu.addAction(clearSelectionAction);
    menu.addSeparator();
    
    // 添加其他终端相关操作...
    
    menu.exec(event->globalPos());
}

// 选择相关公共方法
void TerminalWidget::copyToClipboard()
{
    if (!m_hasSelection) return;
    
    QString selectedText = getSelectedText();
    if (!selectedText.isEmpty()) {
        QApplication::clipboard()->setText(selectedText);
    }
}

void TerminalWidget::clearSelection()
{
    m_hasSelection = false;
    viewport()->update();
}

bool TerminalWidget::hasSelection() const
{
    return m_hasSelection;
}

QString TerminalWidget::getSelectedText() const
{
    if (!m_hasSelection) return QString();
    
    QString result;
    normalizeSelection();
    
    // 遍历选择范围内的每一行
    for (int line = m_selectionStart.line; line <= m_selectionEnd.line; ++line) {
        int startCol = (line == m_selectionStart.line) ? m_selectionStart.column : 0;
        int endCol = (line == m_selectionEnd.line) ? m_selectionEnd.column : m_columns - 1;
        
        // 获取该行的文本
        QString lineText;
        if (line < m_historySize) {
            // 历史行
            int idx = historyIndex(line);
            if (idx >= 0) {
                for (int col = startCol; col <= endCol && col < m_columns; ++col) {
                    lineText.append(m_historyBuffer[idx][col].character);
                }
            }
        } else {
            // 终端行
            int row = line - m_historySize;
            if (row >= 0 && row < m_terminalRows) {
                for (int col = startCol; col <= endCol && col < m_columns; ++col) {
                    lineText.append(m_terminalBuffer[row][col].character);
                }
            }
        }
        
        // 移除行尾空格
        lineText = lineText.trimmed();
        if (!lineText.isEmpty()) {
            result.append(lineText);
            if (line < m_selectionEnd.line) {
                result.append('\n');
            }
        }
    }
    
    return result;
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    // 确保视口的鼠标跟踪状态正确
    viewport()->setMouseTracking(true);
    // qDebug() << "[TerminalWidget] 视口鼠标跟踪状态已更新:" << viewport()->hasMouseTracking();
    
    // 如果正在选择但丢失了鼠标捕获，确保状态一致
    if (m_selecting) {
        // qDebug() << "[TerminalWidget] 焦点进入时正在选择，但可能丢失了鼠标捕获";
        // 我们不重新捕获鼠标，因为用户可能已经移走了
        // 只是更新显示
        viewport()->update();
    }
    
    QAbstractScrollArea::focusInEvent(event);
    // qDebug() << "[TerminalWidget] focusInEvent 处理完成";
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QString reasonStr;
    switch (event->reason()) {
        case Qt::MouseFocusReason: reasonStr = "MouseFocusReason"; break;
        case Qt::TabFocusReason: reasonStr = "TabFocusReason"; break;
        case Qt::BacktabFocusReason: reasonStr = "BacktabFocusReason"; break;
        case Qt::ActiveWindowFocusReason: reasonStr = "ActiveWindowFocusReason"; break;
        case Qt::PopupFocusReason: reasonStr = "PopupFocusReason"; break;
        case Qt::ShortcutFocusReason: reasonStr = "ShortcutFocusReason"; break;
        case Qt::MenuBarFocusReason: reasonStr = "MenuBarFocusReason"; break;
        case Qt::OtherFocusReason: reasonStr = "OtherFocusReason"; break;
        default: reasonStr = QString("Unknown(%1)").arg(event->reason()); break;
    }
    
    // qDebug() << "[TerminalWidget] focusOutEvent - 原因:" << reasonStr
    //          << "选择状态:" << m_selecting
    //          << "hasFocus:" << hasFocus()
    //          << "viewport hasFocus:" << viewport()->hasFocus();
    
    // 当控件失去焦点时，确保释放鼠标捕获
    if (m_selecting) {
        // qDebug() << "[TerminalWidget] 失去焦点且正在选择，清除选择状态";
        m_selecting = false;
        m_hasSelection = false;
        
        // 禁用视口的鼠标跟踪
        viewport()->setMouseTracking(false);
        // qDebug() << "[TerminalWidget] 视口鼠标跟踪已禁用";
        
        viewport()->update();
    } else {
        // qDebug() << "[TerminalWidget] 失去焦点但不在选择状态";
    }
    
    QAbstractScrollArea::focusOutEvent(event);
    // qDebug() << "[TerminalWidget] focusOutEvent 处理完成";
}

bool TerminalWidget::focusNextPrevChild(bool next)
{
    qDebug() << "[TerminalWidget] focusNextPrevChild - 方向:" << (next ? "下一个" : "上一个") << "返回false阻止焦点转移";
    Q_UNUSED(next);
    return false;
}