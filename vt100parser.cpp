#include "vt100parser.h"
#include <QDebug>

VT100Parser::VT100Parser(QObject *parent)
    : QObject(parent)
    , m_state(StateGround)
    , m_utf8Remaining(0)
{
    resetCsi();
    resetOsc();
}

VT100Parser::~VT100Parser()
{
}

void VT100Parser::processByte(unsigned char ch)
{
    qDebug() << "[VT100Parser] processByte - 字符:" << QChar(ch) << "ASCII:" << ch << "状态:" << m_state;
    
    if (ch <= 0x1F || ch == 0x7F) {
        // 控制字符处理
        qDebug() << "[VT100Parser] 处理控制字符:" << ch;
        switch (ch) {
            case 0x07: emit bell(); return;
            case 0x08: emit backspace(); return;
            case 0x09: emit horizontalTab(); return;
            case 0x0A: emit lineFeed(); return;
            case 0x0B: emit verticalTab(); return;
            case 0x0C: emit formFeed(); return;
            case 0x0D: 
                qDebug() << "[VT100Parser] 发出carriageReturn信号";
                emit carriageReturn(); 
                return;
            case 0x1B: m_state = StateEscape; return;
            case 0x7F: return; // DEL，忽略
            default: return; // 其他控制字符忽略
        }
    }
    
    // 状态机处理
    qDebug() << "[VT100Parser] 状态机处理，当前状态:" << m_state;
    switch (m_state) {
        case StateGround:
            groundState(ch);
            break;
        case StateEscape:
            escapeState(ch);
            break;
        case StateCsiEntry:
            csiEntryState(ch);
            break;
        case StateCsiParam:
            csiParamState(ch);
            break;
        case StateCsiIntermediate:
            csiIntermediateState(ch);
            break;
        case StateCsiIgnore:
            csiIgnoreState(ch);
            break;
        case StateOscString:
            oscStringState(ch);
            break;
        case StateUtf8:
            // UTF-8解码（简化实现）
            m_utf8Buffer.append(ch);
            m_utf8Remaining--;
            if (m_utf8Remaining <= 0) {
                QString text = QString::fromUtf8(m_utf8Buffer);
                emit textReceived(text);
                m_utf8Buffer.clear();
                m_state = StateGround;
            }
            break;
        default:
            // 其他状态暂时忽略，回到地面状态
            m_state = StateGround;
            groundState(ch);
            break;
    }
}

void VT100Parser::processData(const QByteArray &data)
{
    qDebug() << "[VT100Parser] processData - 数据大小:" << data.size() << "内容:" << data.toHex();
    for (unsigned char ch : data) {
        processByte(ch);
    }
}

void VT100Parser::groundState(unsigned char ch)
{
    if (ch >= 0x20 && ch <= 0x7E) {
        // 可打印ASCII字符
        if (ch == '#') {
            qDebug() << "[VT100Parser] groundState - 特殊处理#字符，ASCII码:" << ch;
        }
        qDebug() << "[VT100Parser] groundState - 发送字符:" << QChar(ch) << "ASCII码:" << ch;
        emit textReceived(QString(QChar(ch)));
    } else if (ch >= 0xA0) {
        // 可能为UTF-8起始字节（简化处理）
        if ((ch & 0xE0) == 0xC0) {
            m_utf8Remaining = 1;
        } else if ((ch & 0xF0) == 0xE0) {
            m_utf8Remaining = 2;
        } else if ((ch & 0xF8) == 0xF0) {
            m_utf8Remaining = 3;
        } else {
            m_utf8Remaining = 0;
        }
        if (m_utf8Remaining > 0) {
            m_utf8Buffer.append(ch);
            m_state = StateUtf8;
        }
    }
    // 其他字符忽略
}

void VT100Parser::escapeState(unsigned char ch)
{
    qDebug() << "[VT100Parser] escapeState - 字符:" << QChar(ch) << "ASCII:" << ch;
    
    if (ch == '[') {
        m_state = StateCsiEntry;
        resetCsi();
        qDebug() << "[VT100Parser] 进入CSI入口状态";
    } else if (ch == ']') {
        m_state = StateOscString;
        resetOsc();
        qDebug() << "[VT100Parser] 进入OSC字符串状态";
    } else if (ch >= 0x40 && ch <= 0x5F) {
        // 其他转义序列
        qDebug() << "[VT100Parser] 处理转义命令:" << QChar(ch);
        processEscapeCommand(ch);
        m_state = StateGround;
    } else if (ch >= 0x60 && ch <= 0x7E) {
        // 两字符转义序列
        qDebug() << "[VT100Parser] 处理两字符转义命令:" << QChar(ch);
        processEscapeCommand(ch);
        m_state = StateGround;
    } else {
        // 无效转义，回到地面状态
        qDebug() << "[VT100Parser] 无效转义，返回地面状态";
        m_state = StateGround;
    }
}

void VT100Parser::csiEntryState(unsigned char ch)
{
    qDebug() << "[VT100Parser] csiEntryState - 字符:" << QChar(ch) << "ASCII:" << ch;
    
    if (ch >= 0x30 && ch <= 0x3F) {
        // 参数字符
        qDebug() << "[VT100Parser] CSI参数字符:" << QChar(ch);
        m_state = StateCsiParam;
        csiParamState(ch);
    } else if (ch >= 0x20 && ch <= 0x2F) {
        // 中间字符
        qDebug() << "[VT100Parser] CSI中间字符:" << QChar(ch);
        m_state = StateCsiIntermediate;
        m_csiIntermediate.append(QChar(ch));
    } else if (ch >= 0x40 && ch <= 0x7E) {
        // 命令字符
        qDebug() << "[VT100Parser] CSI命令字符:" << QChar(ch);
        processCsiCommand(ch);
        m_state = StateGround;
    } else {
        // 无效字符，忽略
        m_state = StateCsiIgnore;
    }
}

void VT100Parser::csiParamState(unsigned char ch)
{
    if (ch >= 0x30 && ch <= 0x3F) {
        // 参数字符
        if (ch == ';') {
            m_csiParams.append(0); // 默认值
        } else if (ch >= '0' && ch <= '9') {
            if (m_csiParams.isEmpty()) {
                m_csiParams.append(0);
            }
            m_csiParams.last() = m_csiParams.last() * 10 + (ch - '0');
        } else if (ch == ':') {
            // 参数分隔符（特殊处理）
            m_csiParams.append(0);
        }
    } else if (ch >= 0x20 && ch <= 0x2F) {
        // 中间字符
        m_state = StateCsiIntermediate;
        m_csiIntermediate.append(QChar(ch));
    } else if (ch >= 0x40 && ch <= 0x7E) {
        // 命令字符
        processCsiCommand(ch);
        m_state = StateGround;
    } else {
        // 无效字符
        m_state = StateCsiIgnore;
    }
}

void VT100Parser::csiIntermediateState(unsigned char ch)
{
    if (ch >= 0x20 && ch <= 0x2F) {
        // 中间字符继续
        m_csiIntermediate.append(QChar(ch));
    } else if (ch >= 0x40 && ch <= 0x7E) {
        // 命令字符
        processCsiCommand(ch);
        m_state = StateGround;
    } else {
        // 无效字符
        m_state = StateCsiIgnore;
    }
}

void VT100Parser::csiIgnoreState(unsigned char ch)
{
    if (ch >= 0x40 && ch <= 0x7E) {
        // 命令字符，结束忽略状态
        m_state = StateGround;
    }
    // 其他字符继续忽略
}

void VT100Parser::oscStringState(unsigned char ch)
{
    if (ch == 0x07 || ch == 0x1B) {
        // OSC字符串结束（BEL或ESC）
        processOscCommand();
        m_state = StateGround;
        if (ch == 0x1B) {
            // 如果是ESC，需要处理后续字符
            // 这里简化处理，直接回到地面状态
        }
    } else {
        m_oscBuffer.append(QChar(ch));
    }
}

void VT100Parser::processCsiCommand(unsigned char ch)
{
    int param1 = m_csiParams.isEmpty() ? 0 : m_csiParams[0];
    int param2 = m_csiParams.size() > 1 ? m_csiParams[1] : 0;
    
    switch (ch) {
        case 'A': emit cursorUp(param1); break;
        case 'B': emit cursorDown(param1); break;
        case 'C': emit cursorForward(param1); break;
        case 'D': emit cursorBackward(param1); break;
        case 'H': emit cursorPosition(param1, param2); break;
        case 'J': 
            qDebug() << "[VT100Parser] CSI命令 'J'，参数:" << param1;
            if (param1 == 0) emit eraseDown(); 
            else if (param1 == 1) emit eraseUp();
            else if (param1 == 2) emit clearScreen();
            break;
        case 'K': 
            qDebug() << "[VT100Parser] CSI命令 'K'，参数:" << param1;
            if (param1 == 0) emit eraseEndOfLine();
            else if (param1 == 1) emit eraseStartOfLine();
            else if (param1 == 2) emit clearLine();
            break;
        case 'm':
            if (m_csiParams.isEmpty()) {
                emit resetAttributes();
            } else {
                emit setAttributes(m_csiParams);
            }
            break;
        case 'h': emit setMode(param1); break;
        case 'l': emit resetMode(param1); break;
        case 'n': 
            if (param1 == 6) emit deviceStatusReport();
            break;
        case 'R': emit cursorPositionReport(param1, param2); break;
        default:
            // 未实现的CSI命令
            qDebug() << "Unimplemented CSI command:" << ch << "params:" << m_csiParams;
            break;
    }
}

void VT100Parser::processEscapeCommand(unsigned char ch)
{
    switch (ch) {
        case 'M': emit cursorUp(); break;
        case 'D': emit cursorDown(); break;
        case 'E': emit cursorDown(); emit carriageReturn(); break;
        case '7': emit cursorSave(); break;
        case '8': emit cursorRestore(); break;
        case 'c': emit resetAttributes(); break;
        default:
            // 未实现的转义命令
            qDebug() << "Unimplemented escape command:" << ch;
            break;
    }
}

void VT100Parser::processOscCommand()
{
    if (m_oscBuffer.isEmpty()) return;
    
    // OSC命令格式: <数字>;<字符串>
    int sepIndex = m_oscBuffer.indexOf(';');
    if (sepIndex <= 0) return;
    
    QString numStr = m_oscBuffer.left(sepIndex);
    QString dataStr = m_oscBuffer.mid(sepIndex + 1);
    
    bool ok;
    int oscNum = numStr.toInt(&ok);
    if (!ok) return;
    
    switch (oscNum) {
        case 0: // 设置窗口标题
        case 2: // 设置窗口标题（同0）
            emit setWindowTitle(dataStr);
            break;
        case 10: // 设置前景色
        case 11: // 设置背景色
        case 12: // 设置光标颜色
            // 颜色格式可能为 rgb:rrrr/gggg/bbbb 或其他
            // 这里简化处理
            qDebug() << "OSC color command:" << oscNum << dataStr;
            break;
        default:
            qDebug() << "Unimplemented OSC command:" << oscNum << dataStr;
            break;
    }
}

void VT100Parser::resetCsi()
{
    m_csiParams.clear();
    m_csiIntermediate.clear();
    // 添加默认参数0
    m_csiParams.append(0);
}

void VT100Parser::resetOsc()
{
    m_oscBuffer.clear();
}