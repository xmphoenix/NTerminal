#include "vt100parser.h"
#include <QDebug>

VT100Parser::VT100Parser(QObject *parent)
    : QObject(parent)
    , m_state(StateGround)
    , m_utf8Remaining(0)
    , m_privateMode(false)
{
    resetCsi();
    resetOsc();
}

VT100Parser::~VT100Parser()
{
}

void VT100Parser::processByte(unsigned char ch)
{
    if (ch <= 0x1F || ch == 0x7F) {
        switch (ch) {
            case 0x07: emit bell(); return;
            case 0x08: emit backspace(); return;
            case 0x09: emit horizontalTab(); return;
            case 0x0A: 
                emit lineFeed(); 
                return;
            case 0x0B: emit verticalTab(); return;
            case 0x0C: emit formFeed(); return;
            case 0x0D: 
                emit carriageReturn(); 
                return;
            case 0x1B: m_state = StateEscape; return;
            case 0x7F: return;
            default: return;
        }
    }
    
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
            m_state = StateGround;
            groundState(ch);
            break;
    }
}

void VT100Parser::processData(const QByteArray &data)
{
    for (unsigned char ch : data) {
        processByte(ch);
    }
}

void VT100Parser::groundState(unsigned char ch)
{
    if (ch >= 0x20 && ch <= 0x7E) {
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
    if (ch == '[') {
        m_state = StateCsiEntry;
        resetCsi();
    } else if (ch == ']') {
        m_state = StateOscString;
        resetOsc();
    } else if (ch >= 0x40 && ch <= 0x5F) {
        processEscapeCommand(ch);
        m_state = StateGround;
    } else if (ch >= 0x60 && ch <= 0x7E) {
        processEscapeCommand(ch);
        m_state = StateGround;
    } else {
        m_state = StateGround;
    }
}

void VT100Parser::csiEntryState(unsigned char ch)
{
    if (ch == '?') {
        m_privateMode = true;
        m_state = StateCsiParam;
        return;
    }

    if (ch == '>') {
        // ESC [ > c - Secondary device attributes
        m_state = StateCsiIntermediate;
        m_csiIntermediate.append(QChar(ch));
        return;
    }

    if (ch >= 0x30 && ch <= 0x3F) {
        m_state = StateCsiParam;
        csiParamState(ch);
    } else if (ch >= 0x20 && ch <= 0x2F) {
        m_state = StateCsiIntermediate;
        m_csiIntermediate.append(QChar(ch));
    } else if (ch >= 0x40 && ch <= 0x7E) {
        processCsiCommand(ch);
        m_state = StateGround;
    } else {
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
    // 处理 intermediate 字符
    if (!m_csiIntermediate.isEmpty()) {
        if (m_csiIntermediate == ">") {
            // ESC [ > c - Secondary device attributes
            if (ch == 'c') {
                emit secondaryDeviceAttributes();
            }
            resetCsi();
            return;
        }
    }

    if (m_privateMode) {
        processPrivateCsiCommand(ch);
        return;
    }
    
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
                // 处理 256 色模式
                for (int i = 0; i < m_csiParams.size(); ++i) {
                    int param = m_csiParams[i];
                    
                    // ESC[38;5;n m - 前景色 256 色
                    if (param == 38 && i + 2 < m_csiParams.size() && m_csiParams[i + 1] == 5) {
                        // 256 色前景色
                        int colorIndex = m_csiParams[i + 2];
                        qDebug() << "[VT100Parser] 256 color foreground:" << colorIndex;
                        i += 2;  // 跳过后续参数
                        continue;
                    }
                    // ESC[48;5;n m - 背景色 256 色
                    if (param == 48 && i + 2 < m_csiParams.size() && m_csiParams[i + 1] == 5) {
                        // 256 色背景色
                        int colorIndex = m_csiParams[i + 2];
                        qDebug() << "[VT100Parser] 256 color background:" << colorIndex;
                        i += 2;  // 跳过后续参数
                        continue;
                    }
                }
                emit setAttributes(m_csiParams);
            }
            break;
        case 'h': emit setMode(param1); break;
        case 'l': emit resetMode(param1); break;
        case 'n': 
            // ESC [ 5 n - 设备状态报告请求
            // ESC [ 6 n - 光标位置报告请求
            if (param1 == 5) {
                emit deviceStatusReport();
            } else if (param1 == 6) {
                emit cursorPositionReportRequest();
            }
            break;
        case 'c':
            // ESC [ c - 主设备属性查询
            emit primaryDeviceAttributes();
            break;
        case 'R': emit cursorPositionReport(param1, param2); break;
        case 'S': 
            qDebug() << "[VT100Parser] CSI命令 'S' (Scroll Up)，参数:" << param1;
            emit scrollUp(param1 > 0 ? param1 : 1);
            break;
        case 'T': 
            qDebug() << "[VT100Parser] CSI命令 'T' (Scroll Down)，参数:" << param1;
            emit scrollDown(param1 > 0 ? param1 : 1);
            break;
        case 'L': 
            qDebug() << "[VT100Parser] CSI命令 'L' (Insert Lines)，参数:" << param1;
            emit insertLines(param1 > 0 ? param1 : 1);
            break;
        case 'M': 
            qDebug() << "[VT100Parser] CSI命令 'M' (Delete Lines)，参数:" << param1;
            emit deleteLines(param1 > 0 ? param1 : 1);
            break;
        case 'r': 
            {
                int top = param1 > 0 ? param1 : 1;
                int bottom = param2 > 0 ? param2 : 0;
                qDebug() << "[VT100Parser] CSI命令 'r' (Set Scroll Region)，参数:" << top << bottom;
                emit setScrollRegion(top, bottom);
            }
            break;
        default:
            // 未实现的CSI命令
            qDebug() << "Unimplemented CSI command:" << ch << "params:" << m_csiParams;
            break;
    }
}

void VT100Parser::processPrivateCsiCommand(unsigned char ch)
{
    int param1 = m_csiParams.isEmpty() ? 0 : m_csiParams[0];
    
    switch (ch) {
        case 'h':
            qDebug() << "[VT100Parser] 私有模式设置 (SM ?) 参数:" << param1;
            emit setPrivateMode(param1);
            break;
        case 'l':
            qDebug() << "[VT100Parser] 私有模式重置 (RM ?) 参数:" << param1;
            emit resetPrivateMode(param1);
            break;
        default:
            qDebug() << "[VT100Parser] 未实现的私有CSI命令:" << ch << "params:" << m_csiParams;
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
    m_privateMode = false;
    // 添加默认参数0
    m_csiParams.append(0);
}

void VT100Parser::resetOsc()
{
    m_oscBuffer.clear();
}