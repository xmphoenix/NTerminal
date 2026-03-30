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
#include <QTimer>

static QColor ansiColorToQColor(int code, bool isBright);
static QColor ansi256ColorToQColor(int code);

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_columns(200)  // 默认200列，足够显示长行
    , m_terminalRows(24)
    , m_historyLines(1000)
    , m_viewportLines(50)
    , m_cursorColumn(0)
    , m_cursorRow(0)
    , m_insertMode(false)
    , m_applicationCursorKeys(false)  // 默认使用普通模式
    , m_useDecGraphics(false)
    , m_g0Charset('B')  // 默认 ASCII
    , m_g1Charset('0')  // 默认 DEC graphics
    , m_useG1(false)
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
    , m_sshClient(nullptr)
    , m_isConnected(false)
    , m_scriptEngine(nullptr)
    , m_telnetLoginState(TelnetLoginNone)
    , m_sshLoginState(SshLoginNone)
{
    qDebug() << "[TerminalWidget] Constructor - Initial columns:" << m_columns;
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 设置视口也接收鼠标事件
    viewport()->setMouseTracking(true);

    // 初始化制表位（每8列一个制表位）
    m_tabStops.resize(m_columns);
    for (int i = 0; i < m_columns; ++i) {
        m_tabStops[i] = (i % DEFAULT_TAB_WIDTH == 0);
    }

    // 初始化 Python 脚本引擎
    m_scriptEngine = new PythonScriptEngine(this);
    connect(m_scriptEngine, &PythonScriptEngine::sendCommand,
            this, &TerminalWidget::onScriptSendCommand);
    connect(m_scriptEngine, &PythonScriptEngine::logMessage,
            this, &TerminalWidget::onScriptLogMessage);
    connect(m_scriptEngine, &PythonScriptEngine::scriptStarted,
            this, &TerminalWidget::onScriptStarted);
    connect(m_scriptEngine, &PythonScriptEngine::scriptStopped,
            this, &TerminalWidget::onScriptStopped);
    connect(m_scriptEngine, &PythonScriptEngine::scriptError,
            this, &TerminalWidget::onScriptError);

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
    for (const QChar &ch : text) {
        writeChar(ch);
    }
    viewport()->update();
    ensureCursorVisible();
}

void TerminalWidget::writeChar(QChar ch)
{
    // qDebug() << "[TerminalWidget] writeChar - 字符:" << ch << "光标位置: (" << m_cursorRow << "," << m_cursorColumn << ") 当前列数:" << m_columns;
    
    // 检查是否需要调整列数以容纳当前光标位置
    // 当光标接近列边界时提前调整，避免不必要的换行
    if (m_cursorColumn >= m_columns - 1) {
        // qDebug() << "[TerminalWidget] writeChar - 光标接近列边界，当前列:" << m_cursorColumn << "最大列:" << m_columns;
        updateScrollBars(); // 可能会调整列数
    }
    
    if (m_cursorColumn >= m_columns) {
        // qDebug() << "[TerminalWidget] writeChar - 光标超过列边界，自动换行: 列" << m_cursorColumn << ">=" << m_columns;
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

        // DEC 图形字符集转换
        QChar displayChar = ch;
        if (m_useDecGraphics && m_g0Charset == '0') {
            // DEC 特殊图形字符集映射
            // 用于绘制表格边框等
            static const QMap<ushort, QChar> decGraphicsMap = {
                {'j', QChar(0x2518)},  // ┘ 右下角
                {'k', QChar(0x2510)},  // ┐ 右上角
                {'l', QChar(0x250C)},  // ┌ 左上角
                {'m', QChar(0x2514)},  // └ 左下角
                {'n', QChar(0x253C)},  // ┼ 十字
                {'q', QChar(0x2500)},  // ─ 水平线
                {'t', QChar(0x251C)},  // ├ 左T
                {'u', QChar(0x2524)},  // ┤ 右T
                {'v', QChar(0x2534)},  // ┴ 下T
                {'w', QChar(0x252C)},  // ┬ 上T
                {'x', QChar(0x2502)},  // │ 垂直线
                {'a', QChar(0x2592)},  // ▒ 方块
                {'`', QChar(0x25C6)},  // ◆ 菱形
                {'~', QChar(0x00B7)},  // · 中点
            };

            if (decGraphicsMap.contains(ch.unicode())) {
                displayChar = decGraphicsMap[ch.unicode()];
            }
        }

        cell.character = displayChar;
        cell.foreground = m_currentForeground;
        cell.background = m_currentBackground;
        cell.attributes = m_currentAttributes;
        
        // qDebug() << "[TerminalWidget] writeChar - 写入缓冲区位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
        
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
    if (m_insertMode != insert) {
        m_insertMode = insert;
        qDebug() << "[TerminalWidget] setInsertMode - 设置为:" << insert;
        viewport()->update(); // 更新光标显示
    }
}

// 缓冲区管理
void TerminalWidget::allocateBuffers()
{
    qDebug() << "[TerminalWidget] allocateBuffers - 分配缓冲区，列数:" << m_columns 
             << "历史行数:" << m_historyLines << "终端行数:" << m_terminalRows;
    
    // 保存旧的历史缓冲区数据
    QVector<QVector<Cell>> oldHistoryBuffer = m_historyBuffer;
    int oldColumns = m_historyBuffer.isEmpty() ? 0 : m_historyBuffer[0].size();
    
    m_historyBuffer.resize(m_historyLines);
    for (int i = 0; i < m_historyLines; ++i) {
        int oldSize = m_historyBuffer[i].size();
        m_historyBuffer[i].resize(m_columns);
        
        // 如果是扩展列数，保留旧数据
        if (i < oldHistoryBuffer.size() && oldColumns > 0) {
            for (int j = 0; j < oldColumns && j < m_columns; ++j) {
                m_historyBuffer[i][j] = oldHistoryBuffer[i][j];
            }
        }
        
        // Initialize新增的列
        for (int j = oldSize; j < m_columns; ++j) {
            m_historyBuffer[i][j].character = QChar(' ');
            m_historyBuffer[i][j].foreground = m_defaultForeground;
            m_historyBuffer[i][j].background = m_defaultBackground;
            m_historyBuffer[i][j].attributes = Normal;
        }
    }
    
    // 保存旧的终端缓冲区数据
    QVector<QVector<Cell>> oldTerminalBuffer = m_terminalBuffer;
    oldColumns = m_terminalBuffer.isEmpty() ? 0 : m_terminalBuffer[0].size();
    
    m_terminalBuffer.resize(m_terminalRows);
    for (int i = 0; i < m_terminalRows; ++i) {
        int oldSize = m_terminalBuffer[i].size();
        m_terminalBuffer[i].resize(m_columns);
        
        // 如果是扩展列数，保留旧数据
        if (i < oldTerminalBuffer.size() && oldColumns > 0) {
            for (int j = 0; j < oldColumns && j < m_columns; ++j) {
                m_terminalBuffer[i][j] = oldTerminalBuffer[i][j];
            }
        }
        
        // Initialize新增的列
        for (int j = oldSize; j < m_columns; ++j) {
            m_terminalBuffer[i][j].character = QChar(' ');
            m_terminalBuffer[i][j].foreground = m_defaultForeground;
            m_terminalBuffer[i][j].background = m_defaultBackground;
            m_terminalBuffer[i][j].attributes = Normal;
        }
    }
    
    qDebug() << "[TerminalWidget] allocateBuffers - 完成，缓冲区大小: 历史" << m_historyBuffer.size() 
             << "x" << (m_historyBuffer.isEmpty() ? 0 : m_historyBuffer[0].size())
             << "终端" << m_terminalBuffer.size() 
             << "x" << (m_terminalBuffer.isEmpty() ? 0 : m_terminalBuffer[0].size());
}

void TerminalWidget::updateScrollBars()
{
    int totalLines = m_historySize + m_terminalRows;
    
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
    
    // 动态调整列数：如果光标位置超过当前列数，扩展列数
    int requiredColumns = qMax(m_cursorColumn + 1, visibleColumns);
    if (requiredColumns > m_columns) {
        int newColumns = qMax(requiredColumns, m_columns + 50); // 至少增加50列
        // qDebug() << "[TerminalWidget] updateScrollBars - 动态扩展列数从" << m_columns << "到" << newColumns
        //          << "（光标列:" << m_cursorColumn << "可见列:" << visibleColumns << "）";
        m_columns = newColumns;
        allocateBuffers(); // 重新分配缓冲区以容纳新列数
    }

    horizontalScrollBar()->setRange(0, qMax(0, m_columns - visibleColumns));
    horizontalScrollBar()->setPageStep(visibleColumns);
    horizontalScrollBar()->setSingleStep(1);

    // qDebug() << "[TerminalWidget] updateScrollBars - 历史大小:" << m_historySize
    //          << "终端行数:" << m_terminalRows << "总行数:" << totalLines
    //          << "可见行数:" << visibleLines << "可见列数:" << visibleColumns
    //          << "当前列数:" << m_columns
    //          << "垂直范围: 0 -" << qMax(0, totalLines - visibleLines)
    //          << "水平范围: 0 -" << qMax(0, m_columns - visibleColumns)
    //          << "光标位置: (" << m_cursorRow << "," << m_cursorColumn << ")";
}

void TerminalWidget::ensureCursorVisible()
{
    int cursorGlobalLine = m_historySize + m_cursorRow;
    int topLine = verticalScrollBar()->value();
    
    int charHeight = m_charHeight > 0 ? m_charHeight : 20;
    int visibleLines = viewport()->height() / charHeight;
    if (visibleLines <= 0) visibleLines = 1;
    
    int maxScroll = verticalScrollBar()->maximum();
    
    if (cursorGlobalLine < topLine) {
        verticalScrollBar()->setValue(qMin(cursorGlobalLine, maxScroll));
    } else if (cursorGlobalLine >= topLine + visibleLines) {
        int newTopLine = cursorGlobalLine - visibleLines + 1;
        verticalScrollBar()->setValue(qMin(newTopLine, maxScroll));
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
            int cursorX = m_cursorColumn * m_charWidth - leftColumn * m_charWidth;
            int cursorY = cursorLineInView * m_charHeight;
            
            painter.setPen(Qt::white);
            
            if (m_insertMode) {
                // 插入模式：绘制竖线光标（宽度2像素）
                painter.fillRect(cursorX, cursorY, 2, m_charHeight, Qt::white);
            } else {
                // 覆盖模式：绘制块状光标
                painter.drawRect(cursorX, cursorY, m_charWidth, m_charHeight);
            }
        }
    }
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    viewport()->update();
    
    // 更新SSH终端尺寸 - 使用m_columns而不是视口计算的列数
    if (m_sshClient && m_sshClient->state() == SshClient::Ready) {
        int rows = viewport()->height() / (m_charHeight > 0 ? m_charHeight : 20);
        if (rows <= 0) rows = 24;
        m_sshClient->setTerminalSize(m_columns, rows);
    }
}

void TerminalWidget::scrollContentsBy(int dx, int dy)
{
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    viewport()->update();
}

// 键盘事件handling
void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "[TerminalWidget] keyPressEvent - 按键:" << event->key() 
             << "文本:" << event->text() << "修饰符:" << event->modifiers()
             << "Connect状态:" << m_isConnected;
    
    // 首先handling终端相关的快捷键
    if (event->modifiers() & Qt::ControlModifier) {
        switch (event->key()) {
            case Qt::Key_C: // Ctrl+C
                if (hasSelection()) {
                    copyToClipboard();
                } else {
                    // 如果没有选择，SendCtrl+C中断signal
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
                    // SendTerminalclear screen sequence（ESC [ 2J）
                    sendData(QByteArray("\x1b[2J", 4));
                } else {
                    // 未Connect时本地清屏
                    clearScreen();
                }
                return;
        }
    }
    
    if (!m_isConnected && event->key() != Qt::Key_Tab && event->key() != Qt::Key_Backtab) {
        qDebug() << "[TerminalWidget] 未Connect且非Tab键，调用父类handling";
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    qDebug() << "[TerminalWidget] 通过Connect检查，继续handling按键";
    
    QString text = event->text();
    if (!text.isEmpty()) {
        qDebug() << "[TerminalWidget] Send文本:" << text << "长度:" << text.length();
        // Send输入的字符到Connect
        sendText(text);
        
        // 本地回显（可选）
        // writeText(text);
    } else {
        // handling特殊键
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
                qDebug() << "[TerminalWidget] handlingTab/Backtab键，Connect状态:" << m_isConnected;
                if (m_isConnected) {
                    qDebug() << "[TerminalWidget] Send制表符到Connect";
                    sendData(QByteArray("\t", 1));
                } else {
                    qDebug() << "[TerminalWidget] 未Connect，Tab键被忽略";
                }
                break;
            case Qt::Key_Escape:
                sendData(QByteArray("\x1b", 1));
                break;
            case Qt::Key_Up:
                qDebug() << "[TerminalWidget] handling上箭头键，修饰符:" << event->modifiers();
                if (m_isConnected) {
                    handleArrowKey(event->key(), event->modifiers());
                }
                break;
            case Qt::Key_Down:
                qDebug() << "[TerminalWidget] handling下箭头键，修饰符:" << event->modifiers();
                if (m_isConnected) {
                    handleArrowKey(event->key(), event->modifiers());
                }
                break;
            case Qt::Key_Right:
                qDebug() << "[TerminalWidget] handling右箭头键，修饰符:" << event->modifiers();
                if (m_isConnected) {
                    handleArrowKey(event->key(), event->modifiers());
                }
                break;
            case Qt::Key_Left:
                qDebug() << "[TerminalWidget] handling左箭头键，修饰符:" << event->modifiers();
                if (m_isConnected) {
                    handleArrowKey(event->key(), event->modifiers());
                }
                break;
            case Qt::Key_Home:
                qDebug() << "[TerminalWidget] handlingHome键，修饰符:" << event->modifiers();
                if (m_isConnected) {
                    handleHomeKey(event->modifiers());
                }
                break;
            case Qt::Key_End:
                qDebug() << "[TerminalWidget] handlingEnd键，修饰符:" << event->modifiers();
                if (m_isConnected) {
                    handleEndKey(event->modifiers());
                }
                break;
            case Qt::Key_PageUp:
                qDebug() << "[TerminalWidget] handlingPageUp键";
                if (m_isConnected) {
                    sendData(QByteArray("\x1b[5~", 4)); // ESC [ 5 ~
                }
                break;
            case Qt::Key_PageDown:
                qDebug() << "[TerminalWidget] handlingPageDown键";
                if (m_isConnected) {
                    sendData(QByteArray("\x1b[6~", 4)); // ESC [ 6 ~
                }
                break;
            case Qt::Key_Insert:
                qDebug() << "[TerminalWidget] handlingInsert键";
                if (m_isConnected) {
                    sendData(QByteArray("\x1b[2~", 4)); // ESC [ 2 ~
                }
                break;
            case Qt::Key_Delete:
                qDebug() << "[TerminalWidget] handlingDelete键";
                if (m_isConnected) {
                    sendData(QByteArray("\x1b[3~", 4)); // ESC [ 3 ~
                }
                break;
            default:
                QAbstractScrollArea::keyPressEvent(event);
                break;
        }
    }
}

// 以下是Connect管理方法的实现（从之前版本复制）

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
                // standard前景色
                m_currentForeground = ansiColorToQColor(param - 30, false);
                break;
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                // standard背景色
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
                        // 256色索引，简化handling
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
    qDebug() << "[TerminalWidget] setConnectionSettings - Connect类型:" << settings.type
             << "主机:" << settings.host << "端口:" << settings.port
             << "用户名:" << settings.username << "密码:" << (settings.password.isEmpty() ? "空" : "已设置");
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
            emit connectionError("未知的Connect类型");
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
             << "内容:" << data.toHex() << "Connect状态:" << m_isConnected
             << "Connect类型:" << m_connectionSettings.type;
    
    if (!m_isConnected) {
        qDebug() << "[TerminalWidget] sendData失败：未Connect";
        emit connectionError("未Connect");
        return;
    }
    
    switch (m_connectionSettings.type) {
        case ConnectionDialog::Serial:
            qDebug() << "[TerminalWidget] 通过串口Send数据，串口状态:" 
                     << (m_serialPort ? "存在" : "null") 
                     << (m_serialPort && m_serialPort->isOpen() ? "已打开" : "未打开");
            if (m_serialPort && m_serialPort->isOpen()) {
                qint64 bytesWritten = m_serialPort->write(data);
                qDebug() << "[TerminalWidget] 串口写入字节数:" << bytesWritten << "期望:" << data.size();
                if (bytesWritten == data.size()) {
                    qDebug() << "[TerminalWidget] 串口Send成功";
                    emit dataSent(data);
                } else {
                    qDebug() << "[TerminalWidget] 串口Send失败";
                    emit connectionError("串口Send失败");
                }
            } else {
                qDebug() << "[TerminalWidget] 串口不可用，无法Send数据";
            }
            break;
        case ConnectionDialog::Telnet:
            qDebug() << "[TerminalWidget] 通过TelnetSend数据，Socket状态:" 
                     << (m_tcpSocket ? "存在" : "null")
                     << (m_tcpSocket ? m_tcpSocket->state() : QAbstractSocket::UnconnectedState);
            if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
                qint64 bytesWritten = m_tcpSocket->write(data);
                qDebug() << "[TerminalWidget] Telnet写入字节数:" << bytesWritten << "期望:" << data.size();
                if (bytesWritten == data.size()) {
                    qDebug() << "[TerminalWidget] TelnetSend成功";
                    emit dataSent(data);
                } else {
                    qDebug() << "[TerminalWidget] TelnetSend失败";
                    emit connectionError("网络Send失败");
                }
            } else {
                qDebug() << "[TerminalWidget] TelnetConnect不可用，无法Send数据";
            }
            break;
        case ConnectionDialog::SSH:
            // 优先使用libssh客户端
            if (m_sshClient && m_sshClient->state() == SshClient::Ready) {
                qDebug() << "[TerminalWidget] 通过libsshSend数据，大小:" << data.size();
                qint64 bytesWritten = m_sshClient->write(data);
                if (bytesWritten > 0) {
                    qDebug() << "[TerminalWidget] libsshSend成功";
                    emit dataSent(data);
                } else {
                    qDebug() << "[TerminalWidget] libsshSend失败:" << m_sshClient->lastError();
                }
            }
            // 备选：使用QProcess
            else if (m_sshProcess && m_sshProcess->state() == QProcess::Running) {
                qDebug() << "[TerminalWidget] 通过SSH进程Send数据，大小:" << data.size();
                m_sshProcess->write(data);
                emit dataSent(data);
            } else {
                qDebug() << "[TerminalWidget] SSHConnect不可用，无法Send数据";
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
    qDebug() << "[TerminalWidget] openTelnetConnection - 开始ConnectTelnet服务器"
             << m_connectionSettings.host << ":" << m_connectionSettings.port;
    
    if (m_tcpSocket) {
        qDebug() << "[TerminalWidget] openTelnetConnection - 删除旧Socket";
        delete m_tcpSocket;
    }
    
    m_tcpSocket = new QTcpSocket(this);
    
    // 重置自动登录状态
    m_telnetLoginState = TelnetLoginNone;
    m_receivedBuffer.clear();
    
    // Connectsignal
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &TerminalWidget::onTcpDataReady);
    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &TerminalWidget::onTcpError);
    connect(m_tcpSocket, &QTcpSocket::connected, this, [this]() {
        qDebug() << "[TerminalWidget] TelnetConnect成功 - Socket已Connect";
        // 如果配置了用户名和密码，启动自动登录
        if (!m_connectionSettings.username.isEmpty() && !m_connectionSettings.password.isEmpty()) {
            qDebug() << "[TerminalWidget] 启动自动登录流程，用户名:" << m_connectionSettings.username;
            m_telnetLoginState = TelnetLoginWaitPrompt;
        }
    });
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, [this]() {
        qDebug() << "[TerminalWidget] TelnetConnect断开 - 服务器关闭Connect";
        m_isConnected = false;
        m_telnetLoginState = TelnetLoginNone;
        emit connectionClosed();
    });
    connect(m_tcpSocket, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        qDebug() << "[TerminalWidget] Telnet状态变化:" << state;
    });
    
    m_tcpSocket->connectToHost(m_connectionSettings.host, m_connectionSettings.port);
    
    if (m_tcpSocket->waitForConnected(5000)) {
        qDebug() << "[TerminalWidget] openTelnetConnection - Connect成功";
        return true;
    } else {
        QString error = QString("无法Connect到Telnet服务器 %1:%2: %3")
                          .arg(m_connectionSettings.host)
                          .arg(m_connectionSettings.port)
                          .arg(m_tcpSocket->errorString());
        qDebug() << "[TerminalWidget] openTelnetConnection - Connect失败:" << error;
        emit connectionError(error);
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
        return false;
    }
}

void TerminalWidget::closeTelnetConnection()
{
    qDebug() << "[TerminalWidget] closeTelnetConnection - 关闭TelnetConnect";
    if (m_tcpSocket) {
        qDebug() << "[TerminalWidget] closeTelnetConnection - 当前Socket状态:" << m_tcpSocket->state();
        m_tcpSocket->disconnectFromHost();
        if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
            qDebug() << "[TerminalWidget] closeTelnetConnection - 等待断开Connect...";
            m_tcpSocket->waitForDisconnected(1000);
        }
        disconnect(m_tcpSocket, &QTcpSocket::readyRead, this, &TerminalWidget::onTcpDataReady);
        disconnect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &TerminalWidget::onTcpError);
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
        qDebug() << "[TerminalWidget] closeTelnetConnection - Connect已关闭";
    }
}

bool TerminalWidget::openSshConnection()
{
    qDebug() << "[TerminalWidget] openSshConnection - 开始SSHConnect(libssh)"
             << m_connectionSettings.host << ":" << m_connectionSettings.port
             << "用户名:" << m_connectionSettings.username;
    
    // 清理旧Connect
    if (m_sshClient) {
        qDebug() << "[TerminalWidget] openSshConnection - 删除旧SSH客户端";
        delete m_sshClient;
        m_sshClient = nullptr;
    }
    if (m_sshProcess) {
        delete m_sshProcess;
        m_sshProcess = nullptr;
    }
    
    // 重置自动登录状态
    m_sshLoginState = SshLoginNone;
    m_sshReceivedBuffer.clear();
    
    // 创建SSH客户端
    m_sshClient = new SshClient(this);
    
    // Connectsignal
    connect(m_sshClient, &SshClient::connected, this, [this]() {
        qDebug() << "[TerminalWidget] SSH已Connect，开始认证";
        if (!m_sshClient->authenticate()) {
            qDebug() << "[TerminalWidget] SSH认证失败";
        }
    });
    
    connect(m_sshClient, &SshClient::authenticated, this, [this]() {
        qDebug() << "[SSH] 认证成功，打开通道";
        if (m_sshClient->openChannel()) {
            int cols = viewport()->width() / (m_charWidth > 0 ? m_charWidth : 10);
            int rows = viewport()->height() / (m_charHeight > 0 ? m_charHeight : 20);
            if (cols <= 0) cols = 80;
            if (rows <= 0) rows = 24;
            
            int ptyCols = m_columns;
            
            qDebug() << "[SSH] PTY请求 - TERM: xterm-256color 列:" << ptyCols << "行:" << rows;
            
            if (m_sshClient->requestPty("xterm-256color", ptyCols, rows)) {
                if (m_sshClient->requestShell()) {
                    qDebug() << "[SSH] Shell已启动";
                    m_isConnected = true;
                    emit connectionOpened();
                }
            }
        }
    });
    
    connect(m_sshClient, &SshClient::authenticationFailed, this, [this](const QString &error) {
        qDebug() << "[SSH] 认证失败:" << error;
        emit connectionError(error);
        closeConnection();
    });
    
    connect(m_sshClient, &SshClient::ready, this, [this]() {
        qDebug() << "[SSH] Connect就绪";
    });
    
    connect(m_sshClient, &SshClient::dataReceived, this, [this](const QByteArray &data) {
        processReceivedData(data);
    });
    
    connect(m_sshClient, &SshClient::errorOccurred, this, [this](const QString &error) {
        qDebug() << "[TerminalWidget] SSH错误:" << error;
        emit connectionError(error);
    });
    
    connect(m_sshClient, &SshClient::disconnected, this, [this]() {
        qDebug() << "[TerminalWidget] SSHConnect断开";
        m_isConnected = false;
        emit connectionClosed();
    });
    
    // 配置SSHConnect参数
    SshClient::SshSettings settings;
    settings.host = m_connectionSettings.host;
    settings.port = m_connectionSettings.port;
    settings.username = m_connectionSettings.username;
    settings.password = m_connectionSettings.password;
    settings.usePrivateKey = m_connectionSettings.useSshKey;
    settings.privateKeyPath = m_connectionSettings.sshKeyPath;
    settings.privateKeyPassphrase = m_connectionSettings.password; // 使用密码作为密钥密码
    settings.verifyHostKey = false;
    settings.timeout = 30;
    
    // 开始Connect
    if (!m_sshClient->connect(settings)) {
        QString error = QString("SSHConnect失败: %1").arg(m_sshClient->lastError());
        qDebug() << "[TerminalWidget] SSHConnect失败:" << error;
        emit connectionError(error);
        delete m_sshClient;
        m_sshClient = nullptr;
        return false;
    }
    
    return true;
}

void TerminalWidget::closeSshConnection()
{
    qDebug() << "[TerminalWidget] closeSshConnection - 关闭SSHConnect";
    
    // 关闭libssh客户端
    if (m_sshClient) {
        m_sshClient->disconnect();
        delete m_sshClient;
        m_sshClient = nullptr;
    }
    
    // 关闭QProcess（备选方案）
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
        qDebug() << "[TerminalWidget] SSH收到数据，大小:" << data.size();
        
        // 如果正在自动登录流程中，检测登录提示
        if (m_sshLoginState != SshLoginNone && m_sshLoginState != SshLoginDone) {
            qDebug() << "[TerminalWidget] onSshProcessReadyRead - 自动登录状态:" << m_sshLoginState
                     << "密码:" << (m_connectionSettings.password.isEmpty() ? "空" : "已设置");
            m_sshReceivedBuffer += QString::fromUtf8(data);
            checkSshLoginPrompt(m_sshReceivedBuffer);
        }
        
        processReceivedData(data);
    }
}

void TerminalWidget::onSshProcessErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "[TerminalWidget] onSshProcessErrorOccurred - 错误:" << error;
    
    if (error == QProcess::UnknownError) {
        qDebug() << "[TerminalWidget] onSshProcessErrorOccurred - 忽略UnknownError";
        return;
    }
    
    QString errorMsg;
    switch (error) {
        case QProcess::FailedToStart:
            errorMsg = "SSH进程启动失败：找不到ssh命令或权限不足。\n\n请确保系统已安装OpenSSH客户端。\n - Windows: 设置 > 应用 > 可选功能 > 添加OpenSSH客户端\n - Linux: sudo apt install openssh-client";
            break;
        case QProcess::Crashed:
            errorMsg = "SSH进程崩溃";
            break;
        case QProcess::Timedout:
            errorMsg = "SSHConnect超时";
            break;
        case QProcess::WriteError:
            errorMsg = "SSH写入错误";
            break;
        case QProcess::ReadError:
            errorMsg = "SSH读取错误";
            break;
        default:
            errorMsg = QString("SSH未知错误: %1").arg(
                m_sshProcess ? m_sshProcess->errorString() : "未知");
            break;
    }
    
    qDebug() << "[TerminalWidget] onSshProcessErrorOccurred - 错误详情:" << errorMsg;
    emit connectionError(errorMsg);
    
    if (error == QProcess::FailedToStart || error == QProcess::Crashed) {
        m_sshLoginState = SshLoginNone;
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
        
        // handlingTelnet协议协商
        QByteArray processedData = processTelnetProtocol(data);
        
        if (!processedData.isEmpty()) {
            // 如果正在自动登录流程中，检测登录提示
            if (m_telnetLoginState != TelnetLoginNone && m_telnetLoginState != TelnetLoginDone) {
                qDebug() << "[TerminalWidget] onTcpDataReady - 自动登录状态:" << m_telnetLoginState
                         << "用户名:" << m_connectionSettings.username 
                         << "密码:" << (m_connectionSettings.password.isEmpty() ? "空" : "已设置");
                m_receivedBuffer += QString::fromUtf8(processedData);
                checkLoginPrompt(m_receivedBuffer);
            } else {
                qDebug() << "[TerminalWidget] onTcpDataReady - 未启动自动登录，状态:" << m_telnetLoginState
                         << "用户名:" << m_connectionSettings.username 
                         << "密码:" << (m_connectionSettings.password.isEmpty() ? "空" : "已设置");
            }
            
            processReceivedData(processedData);
        }
    }
}

QByteArray TerminalWidget::processTelnetProtocol(const QByteArray &data)
{
    QByteArray result;
    result.reserve(data.size());
    
    for (int i = 0; i < data.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(data[i]);
        
        if (ch == 0xFF) {  // IAC (Interpret As Command)
            if (i + 1 < data.size()) {
                unsigned char cmd = static_cast<unsigned char>(data[++i]);
                
                switch (cmd) {
                    case 0xFF:  // 双0xFF表示真实的0xFF字符
                        result.append(static_cast<char>(0xFF));
                        break;
                    case 0xFB:  // WILL
                    case 0xFC:  // WONT
                    case 0xFD:  // DO
                    case 0xFE:  // DONT
                        if (i + 1 < data.size()) {
                            unsigned char option = static_cast<unsigned char>(data[++i]);
                            qDebug() << "[TerminalWidget] Telnet协商: IAC" 
                                     << (cmd == 0xFB ? "WILL" : cmd == 0xFC ? "WONT" 
                                        : cmd == 0xFD ? "DO" : "DONT")
                                     << "选项:" << option;
                            
                            // 响应Telnet协商：拒绝所有选项
                            QByteArray response;
                            response.append(static_cast<char>(0xFF));  // IAC
                            response.append(static_cast<char>(cmd == 0xFD || cmd == 0xFE ? 0xFC : 0xFE));  // WONT/DONT
                            response.append(static_cast<char>(option));
                            
                            if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
                                m_tcpSocket->write(response);
                                qDebug() << "[TerminalWidget] Telnet协商响应:" << response.toHex();
                            }
                        }
                        break;
                    case 0xF0:  // NOP
                    case 0xF1:  // Data Mark
                    case 0xF2:  // Break
                    case 0xF3:  // Interrupt Process
                    case 0xF4:  // Abort output
                    case 0xF5:  // Are You There
                    case 0xF6:  // Erase character
                    case 0xF7:  // Erase Line
                    case 0xF8:  // Go ahead
                    case 0xF9:  // SB (Subnegotiation Begin)
                    case 0xFA:  // SE (Subnegotiation End)
                        qDebug() << "[TerminalWidget] Telnet命令:" << QString("0x%1").arg(cmd, 2, 16, QChar('0'));
                        // 这些命令暂时忽略
                        break;
                    default:
                        qDebug() << "[TerminalWidget] Telnet未知命令:" << QString("0x%1").arg(cmd, 2, 16, QChar('0'));
                        break;
                }
            }
        } else {
            result.append(static_cast<char>(ch));
        }
    }
    
    return result;
}

void TerminalWidget::processReceivedData(const QByteArray &data)
{
    qDebug() << "[TerminalWidget] processReceivedData - 数据大小:" << data.size() 
             << "内容:" << data.toHex() << "ASCII:" << QString(data).left(100);
    
    emit dataReceived(data);
    
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
    qDebug() << "[TerminalWidget] onTcpError - Socket错误:" << error;
    
    if (error == QAbstractSocket::UnknownSocketError) {
        qDebug() << "[TerminalWidget] onTcpError - 忽略UnknownSocketError";
        return;
    }
    
    QString errorMsg = QString("网络错误: %1").arg(m_tcpSocket->errorString());
    qDebug() << "[TerminalWidget] onTcpError - 错误信息:" << errorMsg;
    emit connectionError(errorMsg);
    
    // According to错误类型决定是否关闭Connect
    if (error == QAbstractSocket::ConnectionRefusedError ||
        error == QAbstractSocket::RemoteHostClosedError ||
        error == QAbstractSocket::NetworkError ||
        error == QAbstractSocket::SocketTimeoutError ||
        error == QAbstractSocket::HostNotFoundError) {
        qDebug() << "[TerminalWidget] onTcpError - 致命错误，关闭Connect";
        closeConnection();
    }
}


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
    // 转换为0-based坐标
    int actualRow = qMax(0, row - 1);
    int actualCol = qMax(0, column - 1);
    
    // 如果请求的位置超出边界，限制在边界内
    // 这样vi可以通过 ESC [ 999 ; 999 H 来查询终端尺寸
    if (actualRow >= m_terminalRows) {
        actualRow = m_terminalRows - 1;
    }
    if (actualCol >= m_columns) {
        actualCol = m_columns - 1;
    }
    
    qDebug() << "[TerminalWidget] onCursorPosition - 请求位置: (" << row << "," << column << ") "
             << "实际位置: (" << actualRow << "," << actualCol << ")";
    
    m_cursorColumn = actualCol;
    m_cursorRow = actualRow;
    viewport()->update();
}

void TerminalWidget::onCursorHome()
{
    setCursor(0, 0);
}

// 光标定位命令实现

void TerminalWidget::onCursorNextLine(int count)
{
    if (count <= 0) count = 1;

    // 移动到下N行的开头
    int newRow = m_cursorRow + count;
    if (newRow >= m_terminalRows) {
        newRow = m_terminalRows - 1;
    }

    m_cursorRow = newRow;
    m_cursorColumn = 0;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalWidget::onCursorPreviousLine(int count)
{
    if (count <= 0) count = 1;

    // 移动到上N行的开头
    int newRow = m_cursorRow - count;
    if (newRow < 0) {
        newRow = 0;
    }

    m_cursorRow = newRow;
    m_cursorColumn = 0;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalWidget::onCursorHorizontalAbsolute(int col)
{
    // 移动到指定列（1-based）
    int actualCol = qMax(0, col - 1);
    if (actualCol >= m_columns) {
        actualCol = m_columns - 1;
    }

    m_cursorColumn = actualCol;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalWidget::onVerticalPositionAbsolute(int row)
{
    // 移动到指定行（1-based）
    int actualRow = qMax(0, row - 1);
    if (actualRow >= m_terminalRows) {
        actualRow = m_terminalRows - 1;
    }

    m_cursorRow = actualRow;
    ensureCursorVisible();
    viewport()->update();
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

void TerminalWidget::onSetMode(int param)
{
    qDebug() << "[TerminalWidget] onSetMode - 参数:" << param;
    // According toTerminalspecification，参数4是插入模式
    if (param == 4) {
        setInsertMode(true);
        qDebug() << "[TerminalWidget] onSetMode - 启用插入模式";
    }
}

void TerminalWidget::onResetMode(int param)
{
    qDebug() << "[TerminalWidget] onResetMode - 参数:" << param;
    // According toTerminalspecification，参数4是插入模式
    if (param == 4) {
        setInsertMode(false);
        qDebug() << "[TerminalWidget] onResetMode - 禁用插入模式（覆盖模式）";
    }
}

// void TerminalWidget::onDeviceStatusReport()
{
    qDebug() << "[TerminalWidget] onDeviceStatusReport - 响应设备状态报告请求";
    // 响应 ESC [ 0 n 表示设备正常
    QByteArray response = "\x1b[0n";
    qDebug() << "[TerminalWidget] onDeviceStatusReport - Send响应:" << response.toHex();
    sendData(response);
}

void TerminalWidget::onCursorPositionReportRequest()
{
    qDebug() << "[TerminalWidget] onCursorPositionReportRequest - 响应光标位置报告请求";
    // 响应 ESC [<行>;<列>R
    // 注意：行和列是从1开始的，而我们的内部坐标是从0开始的
    int row = m_cursorRow + 1; // 转换为1-based
    int column = m_cursorColumn + 1; // 转换为1-based
    QByteArray response = QString("\x1b[%1;%2R").arg(row).arg(column).toUtf8();
    qDebug() << "[TerminalWidget] onCursorPositionReportRequest - Send响应:" << response.toHex() 
             << "光标位置: (" << row << "," << column << ")";
    sendData(response);
}

void TerminalWidget::onPrimaryDeviceAttributes()
{
    qDebug() << "[TerminalWidget] onPrimaryDeviceAttributes - 响应主设备属性查询";
    // According toTerminalstandard，响应 ESC [?6c 表示Terminal兼容终端
    QByteArray response = "\x1b[?6c";
    qDebug() << "[TerminalWidget] onPrimaryDeviceAttributes - Send响应:" << response.toHex();
    sendData(response);
}

void TerminalWidget::checkLoginPrompt(const QString &text)
{
    // 检测登录提示并Send凭据
    QString lowerText = text.toLower();
    
    switch (m_telnetLoginState) {
        case TelnetLoginWaitPrompt:
            // 检测用户名提示 (login:, username:, user: 等)
            if (lowerText.contains("login:") || lowerText.contains("username:") || lowerText.contains("user:")) {
                qDebug() << "[TerminalWidget] checkLoginPrompt - 检测到用户名提��";
                m_telnetLoginState = TelnetLoginSentUser;
                QTimer::singleShot(100, this, &TerminalWidget::sendTelnetCredentials);
                m_receivedBuffer.clear();
            }
            break;
            
        case TelnetLoginSentUser:
            // 已Send用户名，等待密码提示
            if (lowerText.contains("password:") || lowerText.contains("passwd:")) {
                qDebug() << "[TerminalWidget] checkLoginPrompt - 检测到密码提示";
                m_telnetLoginState = TelnetLoginWaitPassword;
                QTimer::singleShot(100, this, &TerminalWidget::sendTelnetCredentials);
                m_receivedBuffer.clear();
            }
            break;
            
        case TelnetLoginWaitPassword:
            // 已Send密码，等待登录结果
            // 检测是否登录成功（通常会有 shell 提示符或欢迎信息）
            if (lowerText.contains("$") || lowerText.contains("#") || 
                lowerText.contains(">") || lowerText.contains("~") ||
                lowerText.contains("welcome") || lowerText.contains("last login")) {
                qDebug() << "[TerminalWidget] checkLoginPrompt - 登录成功";
                m_telnetLoginState = TelnetLoginDone;
                m_receivedBuffer.clear();
            }
            break;
            
        default:
            break;
    }
    
    // 限制缓冲区大小，避免内存无限增长
    if (m_receivedBuffer.size() > 4096) {
        m_receivedBuffer = m_receivedBuffer.right(2048);
    }
}

void TerminalWidget::sendTelnetCredentials()
{
    if (!m_tcpSocket || m_tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "[TerminalWidget] sendTelnetCredentials - Connect已断开";
        return;
    }
    
    switch (m_telnetLoginState) {
        case TelnetLoginSentUser:
            // Send用户名
            qDebug() << "[TerminalWidget] sendTelnetCredentials - Send用户名:" << m_connectionSettings.username;
            m_tcpSocket->write((m_connectionSettings.username + "\r\n").toUtf8());
            break;
            
        case TelnetLoginWaitPassword:
            // Send密码
            qDebug() << "[TerminalWidget] sendTelnetCredentials - Send密码";
            m_tcpSocket->write((m_connectionSettings.password + "\r\n").toUtf8());
            m_telnetLoginState = TelnetLoginSentPassword;
            break;
            
        default:
            break;
    }
}

void TerminalWidget::checkSshLoginPrompt(const QString &text)
{
    QString lowerText = text.toLower();
    
    switch (m_sshLoginState) {
        case SshLoginWaitPrompt:
            // 检测密码提示
            if (lowerText.contains("password:") || lowerText.contains("密码:")) {
                qDebug() << "[TerminalWidget] checkSshLoginPrompt - 检测到密码提示";
                m_sshLoginState = SshLoginWaitPassword;
                QTimer::singleShot(200, this, &TerminalWidget::sendSshCredentials);
                m_sshReceivedBuffer.clear();
            }
            // 检测密钥密码短语提示
            else if (lowerText.contains("enter passphrase for key") || 
                     lowerText.contains("passphrase:")) {
                qDebug() << "[TerminalWidget] checkSshLoginPrompt - 检测到密钥密码短语提示";
                m_sshLoginState = SshLoginWaitPassphrase;
                QTimer::singleShot(200, this, &TerminalWidget::sendSshCredentials);
                m_sshReceivedBuffer.clear();
            }
            // 检测权限拒绝错误
            else if (lowerText.contains("permission denied") || 
                     lowerText.contains("access denied")) {
                qDebug() << "[TerminalWidget] checkSshLoginPrompt - 认证失败";
                emit connectionError("SSH认证失败：用户名或密码错误");
                m_sshLoginState = SshLoginNone;
                closeConnection();
            }
            // 检测Connect成功（出现 shell 提示符）
            else if (lowerText.contains("$") || lowerText.contains("#") || 
                     lowerText.contains(">") || lowerText.contains("~") ||
                     lowerText.contains("welcome") || lowerText.contains("last login")) {
                qDebug() << "[TerminalWidget] checkSshLoginPrompt - SSH登录成功";
                m_sshLoginState = SshLoginDone;
                m_sshReceivedBuffer.clear();
            }
            break;
            
        case SshLoginSentPassword:
        case SshLoginSentPassphrase:
            // 已Send凭据，等待结果
            if (lowerText.contains("permission denied") || 
                lowerText.contains("access denied")) {
                qDebug() << "[TerminalWidget] checkSshLoginPrompt - 认证失败";
                emit connectionError("SSH认证失败：用户名或密码错误");
                m_sshLoginState = SshLoginNone;
                closeConnection();
            }
            // 检测Connect成功
            else if (lowerText.contains("$") || lowerText.contains("#") || 
                     lowerText.contains(">") || lowerText.contains("~") ||
                     lowerText.contains("welcome") || lowerText.contains("last login")) {
                qDebug() << "[TerminalWidget] checkSshLoginPrompt - SSH登录成功";
                m_sshLoginState = SshLoginDone;
                m_sshReceivedBuffer.clear();
            }
            break;
            
        default:
            break;
    }
    
    // 限制缓冲区大小
    if (m_sshReceivedBuffer.size() > 8192) {
        m_sshReceivedBuffer = m_sshReceivedBuffer.right(4096);
    }
}

void TerminalWidget::sendSshCredentials()
{
    if (!m_sshProcess || m_sshProcess->state() != QProcess::Running) {
        qDebug() << "[TerminalWidget] sendSshCredentials - SSH进程未运行";
        return;
    }
    
    switch (m_sshLoginState) {
        case SshLoginWaitPassword:
            // Send密码
            qDebug() << "[TerminalWidget] sendSshCredentials - Send密码";
            m_sshProcess->write((m_connectionSettings.password + "\n").toUtf8());
            m_sshLoginState = SshLoginSentPassword;
            break;
            
        case SshLoginWaitPassphrase:
            // Send密钥密码短语（使用密码作为密钥密码短语）
            qDebug() << "[TerminalWidget] sendSshCredentials - Send密钥密码短语";
            m_sshProcess->write((m_connectionSettings.password + "\n").toUtf8());
            m_sshLoginState = SshLoginSentPassphrase;
            break;
            
        default:
            break;
    }
}

void TerminalWidget::onCarriageReturn()
{
    carriageReturn();
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

// 字符编辑命令实现

void TerminalWidget::onInsertChars(int count)
{
    if (count <= 0) count = 1;

    // 在光标位置插入空白字符
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        QVector<Cell> &line = m_terminalBuffer[m_cursorRow];

        // 从光标位置开始，将后面的字符右移
        for (int i = m_columns - 1; i >= m_cursorColumn + count; --i) {
            if (i - count >= 0 && i - count < m_columns) {
                line[i] = line[i - count];
            }
        }

        // 在光标位置插入空白字符
        Cell emptyCell;
        emptyCell.character = ' ';
        emptyCell.foreground = m_currentForeground;
        emptyCell.background = m_currentBackground;
        emptyCell.attributes = m_currentAttributes;

        for (int i = 0; i < count && m_cursorColumn + i < m_columns; ++i) {
            line[m_cursorColumn + i] = emptyCell;
        }

        viewport()->update();
    }
}

void TerminalWidget::onDeleteChars(int count)
{
    if (count <= 0) count = 1;

    // 删除光标位置的字符
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        QVector<Cell> &line = m_terminalBuffer[m_cursorRow];

        // 从光标位置开始，将后面的字符左移
        for (int i = m_cursorColumn; i < m_columns - count; ++i) {
            if (i + count < m_columns) {
                line[i] = line[i + count];
            }
        }

        // 在行尾填充空白字符
        Cell emptyCell;
        emptyCell.character = ' ';
        emptyCell.foreground = m_currentForeground;
        emptyCell.background = m_currentBackground;
        emptyCell.attributes = m_currentAttributes;

        for (int i = m_columns - count; i < m_columns; ++i) {
            if (i >= 0) {
                line[i] = emptyCell;
            }
        }

        viewport()->update();
    }
}

void TerminalWidget::onEraseChars(int count)
{
    if (count <= 0) count = 1;

    // 擦除光标位置的字符（用空格替换）
    if (m_cursorRow >= 0 && m_cursorRow < m_terminalRows) {
        QVector<Cell> &line = m_terminalBuffer[m_cursorRow];

        Cell emptyCell;
        emptyCell.character = ' ';
        emptyCell.foreground = m_currentForeground;
        emptyCell.background = m_currentBackground;
        emptyCell.attributes = m_currentAttributes;

        for (int i = 0; i < count && m_cursorColumn + i < m_columns; ++i) {
            line[m_cursorColumn + i] = emptyCell;
        }

        viewport()->update();
    }
}

// 制表符处理实现

void TerminalWidget::onTabSet()
{
    // 在当前光标位置设置制表位
    if (m_cursorColumn >= 0 && m_cursorColumn < m_tabStops.size()) {
        m_tabStops[m_cursorColumn] = true;
    }
}

void TerminalWidget::onTabClear(int param)
{
    // param = 0: 清除当前列的制表位
    // param = 3: 清除所有制表位
    if (param == 0) {
        if (m_cursorColumn >= 0 && m_cursorColumn < m_tabStops.size()) {
            m_tabStops[m_cursorColumn] = false;
        }
    } else if (param == 3) {
        m_tabStops.fill(false);
    }
}

void TerminalWidget::onCursorForwardTab(int count)
{
    if (count <= 0) count = 1;

    // 前进到下N个制表位
    int currentCol = m_cursorColumn;
    for (int i = 0; i < count; ++i) {
        // 查找下一个制表位
        bool found = false;
        for (int col = currentCol + 1; col < m_columns; ++col) {
            if (col < m_tabStops.size() && m_tabStops[col]) {
                currentCol = col;
                found = true;
                break;
            }
        }

        // 如果没有找到制表位，移动到行尾
        if (!found) {
            currentCol = m_columns - 1;
            break;
        }
    }

    m_cursorColumn = currentCol;
    ensureCursorVisible();
    viewport()->update();
}

void TerminalWidget::onCursorBackwardTab(int count)
{
    if (count <= 0) count = 1;

    // 后退到上N个制表位
    int currentCol = m_cursorColumn;
    for (int i = 0; i < count; ++i) {
        // 查找上一个制表位
        bool found = false;
        for (int col = currentCol - 1; col >= 0; --col) {
            if (col < m_tabStops.size() && m_tabStops[col]) {
                currentCol = col;
                found = true;
                break;
            }
        }

        // 如果没有找到制表位，移动到行首
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

void TerminalWidget::onClipboardRead()
{
    // OSC 52 读取剪贴板请求
    // 读取系统剪贴板内容并发送回终端
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();

    if (!text.isEmpty()) {
        // 将文本编码为 base64
        QByteArray base64 = text.toUtf8().toBase64();

        // 发送 OSC 52 响应: ESC ] 52 ; c ; <base64> BEL
        QByteArray response = "\033]52;c;" + base64 + "\007";
        sendData(response);
    }
}

void TerminalWidget::onClipboardWrite(const QString &data)
{
    // OSC 52 写入剪贴板
    // 将接收到的文本写入系统剪贴板
    if (!data.isEmpty()) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(data);
        qDebug() << "[TerminalWidget] 剪贴板已更新:" << data.left(50);
    }
}

// 字符集切换实现

void TerminalWidget::onSetCharacterSet(int gset, char charset)
{
    // 设置 G0/G1/G2/G3 字符集
    switch (gset) {
        case 0:
            m_g0Charset = charset;
            break;
        case 1:
            m_g1Charset = charset;
            break;
        default:
            // G2/G3 暂不支持
            break;
    }

    // 如果设置的是 DEC 图形字符集 ('0')，启用图形模式
    m_useDecGraphics = (m_g0Charset == '0' || m_g1Charset == '0');
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

// 鼠标事件handling
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
    // qDebug() << "[TerminalWidget] focusInEvent handling完成";
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
    // qDebug() << "[TerminalWidget] focusOutEvent handling完成";
}

bool TerminalWidget::focusNextPrevChild(bool next)
{
    qDebug() << "[TerminalWidget] focusNextPrevChild - 方向:" << (next ? "下一个" : "上一个") << "返回false阻止焦点转移";
    Q_UNUSED(next);
    return false;
}

// 键盘handling辅助函数实现
void TerminalWidget::handleArrowKey(int key, Qt::KeyboardModifiers modifiers)
{
    QByteArray sequence = getArrowSequence(key, modifiers);
    if (!sequence.isEmpty()) {
        qDebug() << "[TerminalWidget] handleArrowKey - Send序列:" << sequence.toHex();
        sendData(sequence);
    }
}

void TerminalWidget::handleHomeKey(Qt::KeyboardModifiers modifiers)
{
    QByteArray sequence = getHomeSequence(modifiers);
    if (!sequence.isEmpty()) {
        qDebug() << "[TerminalWidget] handleHomeKey - Send序列:" << sequence.toHex();
        sendData(sequence);
    }
}

void TerminalWidget::handleEndKey(Qt::KeyboardModifiers modifiers)
{
    QByteArray sequence = getEndSequence(modifiers);
    if (!sequence.isEmpty()) {
        qDebug() << "[TerminalWidget] handleEndKey - Send序列:" << sequence.toHex();
        sendData(sequence);
    }
}

QByteArray TerminalWidget::getArrowSequence(int key, Qt::KeyboardModifiers modifiers) const
{
    QByteArray baseSequence;
    
    // 确定基础序列（According to应用程序模式）
    if (m_applicationCursorKeys) {
        // 应用程序模式：ESC O A/B/C/D
        switch (key) {
            case Qt::Key_Up:    baseSequence = "\x1bOA"; break;
            case Qt::Key_Down:  baseSequence = "\x1bOB"; break;
            case Qt::Key_Right: baseSequence = "\x1bOC"; break;
            case Qt::Key_Left:  baseSequence = "\x1bOD"; break;
            default: return QByteArray();
        }
    } else {
        // 普通模式：ESC [ A/B/C/D
        switch (key) {
            case Qt::Key_Up:    baseSequence = "\x1b[A"; break;
            case Qt::Key_Down:  baseSequence = "\x1b[B"; break;
            case Qt::Key_Right: baseSequence = "\x1b[C"; break;
            case Qt::Key_Left:  baseSequence = "\x1b[D"; break;
            default: return QByteArray();
        }
    }
    
    // handling修饰键
    if (modifiers & Qt::ControlModifier) {
        // Ctrl+方向键：ESC [ 1;5 A/B/C/D
        QByteArray ctrlSequence;
        switch (key) {
            case Qt::Key_Up:    ctrlSequence = "\x1b[1;5A"; break;
            case Qt::Key_Down:  ctrlSequence = "\x1b[1;5B"; break;
            case Qt::Key_Right: ctrlSequence = "\x1b[1;5C"; break;
            case Qt::Key_Left:  ctrlSequence = "\x1b[1;5D"; break;
            default: return baseSequence;
        }
        qDebug() << "[TerminalWidget] getArrowSequence - Ctrl修饰键，使用序列:" << ctrlSequence.toHex();
        return ctrlSequence;
    } else if (modifiers & Qt::ShiftModifier) {
        // Shift+方向键：ESC [ 1;2 A/B/C/D（用于选择文本）
        QByteArray shiftSequence;
        switch (key) {
            case Qt::Key_Up:    shiftSequence = "\x1b[1;2A"; break;
            case Qt::Key_Down:  shiftSequence = "\x1b[1;2B"; break;
            case Qt::Key_Right: shiftSequence = "\x1b[1;2C"; break;
            case Qt::Key_Left:  shiftSequence = "\x1b[1;2D"; break;
            default: return baseSequence;
        }
        qDebug() << "[TerminalWidget] getArrowSequence - Shift修饰键，使用序列:" << shiftSequence.toHex();
        return shiftSequence;
    }
    
    return baseSequence;
}

QByteArray TerminalWidget::getHomeSequence(Qt::KeyboardModifiers modifiers) const
{
    if (m_applicationCursorKeys) {
        // 应用程序模式：ESC O H
        return "\x1bOH";
    } else {
        // 普通模式：ESC [ H
        return "\x1b[H";
    }
}

QByteArray TerminalWidget::getEndSequence(Qt::KeyboardModifiers modifiers) const
{
    if (m_applicationCursorKeys) {
        // 应用程序模式：ESC O F
        return "\x1bOF";
    } else {
        // 普通模式：ESC [ F
        return "\x1b[F";
    }
}

void TerminalWidget::setApplicationCursorKeys(bool enabled)
{
    if (m_applicationCursorKeys != enabled) {
        m_applicationCursorKeys = enabled;
        qDebug() << "[TerminalWidget] setApplicationCursorKeys - 设置为:" << enabled
                 << "(" << (enabled ? "应用程序模式" : "普通模式") << ")";
    }
}

// Python 脚本引擎实现

bool TerminalWidget::loadScript(const QString &scriptPath)
{
    if (!m_scriptEngine) {
        qDebug() << "[TerminalWidget] Script engine not initialized";
        return false;
    }

    return m_scriptEngine->loadScript(scriptPath);
}

void TerminalWidget::stopScript()
{
    if (m_scriptEngine) {
        m_scriptEngine->stopScript();
    }
}

bool TerminalWidget::isScriptRunning() const
{
    return m_scriptEngine && m_scriptEngine->isRunning();
}

QString TerminalWidget::currentScript() const
{
    return m_scriptEngine ? m_scriptEngine->currentScript() : QString();
}

void TerminalWidget::onScriptSendCommand(const QString &command)
{
    // 脚本请求发送命令到终端
    qDebug() << "[TerminalWidget] Script sending command:" << command.left(50);
    sendText(command);
}

void TerminalWidget::onScriptLogMessage(const QString &level, const QString &message)
{
    // 脚本记录日志
    qDebug() << "[TerminalWidget] Script log [" << level << "]:" << message;
    emit scriptLogMessage(level, message);
}

void TerminalWidget::onScriptStarted()
{
    qDebug() << "[TerminalWidget] Script started";
    emit scriptStarted();
}

void TerminalWidget::onScriptStopped(int exitCode)
{
    qDebug() << "[TerminalWidget] Script stopped with exit code:" << exitCode;
    emit scriptStopped(exitCode);
}

void TerminalWidget::onScriptError(const QString &error)
{
    qDebug() << "[TerminalWidget] Script error:" << error;
    emit scriptError(error);
}
