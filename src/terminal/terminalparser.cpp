#include "terminal/terminalparser.h"
#include <QDebug>

TerminalParser::TerminalParser(QObject *parent)
    : QObject(parent)
    , m_state(StateGround)
    , m_utf8Remaining(0)
    , m_privateMode(false)
    , m_charsetState(CharsetNone)
    , m_g0Charset('B')  // 默认 ASCII
    , m_g1Charset('0')  // 默认 DEC graphics
    , m_useG1(false)
{
    resetCsi();
    resetOsc();
    resetDcs();
}

TerminalParser::~TerminalParser()
{
}

void TerminalParser::processByte(unsigned char ch)
{
    // Special handling for OSC state - BEL and ESC have special meaning
    if (m_state == StateOscString) {
        oscStringState(ch);
        return;
    }

    // Handle control characters (except when in special states)
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
            // Already handled above
            break;
        case StateDcsEntry:
            dcsEntryState(ch);
            break;
        case StateDcsParam:
            dcsParamState(ch);
            break;
        case StateDcsIntermediate:
            dcsIntermediateState(ch);
            break;
        case StateDcsPassthrough:
            dcsPassthroughState(ch);
            break;
        case StateDcsIgnore:
            dcsIgnoreState(ch);
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

void TerminalParser::processData(const QByteArray &data)
{
    for (unsigned char ch : data) {
        processByte(ch);
    }
}

void TerminalParser::groundState(unsigned char ch)
{
    // 处理字符集切换命令
    if (m_charsetState != CharsetNone) {
        processCharsetCommand(ch);
        m_charsetState = CharsetNone;
        return;
    }

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

void TerminalParser::escapeState(unsigned char ch)
{
    if (ch == '[') {
        m_state = StateCsiEntry;
        resetCsi();
    } else if (ch == ']') {
        m_state = StateOscString;
        resetOsc();
    } else if (ch == 'P') {
        // ESC P - DCS (Device Control String)
        m_state = StateDcsEntry;
        resetDcs();
    } else if (ch == '(') {
        // ESC ( - 设置 G0 字符集
        m_charsetState = CharsetG0;
        m_state = StateGround;  // 下一个字符将被 processCharsetCommand 处理
    } else if (ch == ')') {
        // ESC ) - 设置 G1 字符集
        m_charsetState = CharsetG1;
        m_state = StateGround;
    } else if (ch >= 0x30 && ch <= 0x7E) {
        // DEC 标准：ESC 后 0x30-0x7E 范围为两字符转义序列的最终字符
        // （含 '7'/'8' 即 DECSC/DECRC；未识别的走 processEscapeCommand 的 default 分支）
        processEscapeCommand(ch);
        m_state = StateGround;
    } else {
        m_state = StateGround;
    }
}

void TerminalParser::csiEntryState(unsigned char ch)
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

void TerminalParser::csiParamState(unsigned char ch)
{
    if (ch >= 0x30 && ch <= 0x3F) {
        // 参数字符
        if (ch == ';') {
            if (m_csiParams.size() >= MAX_CSI_PARAMS) {
                // 参数过多，进入忽略状态
                qDebug() << "[TerminalParser] CSI 参数过多，忽略";
                m_state = StateCsiIgnore;
                return;
            }
            m_csiParams.append(0); // 默认值
        } else if (ch >= '0' && ch <= '9') {
            if (m_csiParams.isEmpty()) {
                m_csiParams.append(0);
            }
            // 防止参数值溢出
            if (m_csiParams.last() < 100000) {
                m_csiParams.last() = m_csiParams.last() * 10 + (ch - '0');
            }
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

void TerminalParser::csiIntermediateState(unsigned char ch)
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

void TerminalParser::csiIgnoreState(unsigned char ch)
{
    if (ch >= 0x40 && ch <= 0x7E) {
        // 命令字符，结束忽略状态
        m_state = StateGround;
    }
    // 其他字符继续忽略
}

void TerminalParser::oscStringState(unsigned char ch)
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
        // 检查 OSC 缓冲区长度
        if (m_oscBuffer.length() >= MAX_OSC_LENGTH) {
            qDebug() << "[TerminalParser] OSC 缓冲区溢出，重置";
            m_state = StateGround;
            resetOsc();
            return;
        }
        m_oscBuffer.append(QChar(ch));
    }
}

void TerminalParser::processCsiCommand(unsigned char ch)
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
        case 'E':
            // CNL - Cursor Next Line
            qDebug() << "[TerminalParser] CSI命令 'E' (Cursor Next Line)，参数:" << param1;
            emit cursorNextLine(param1 > 0 ? param1 : 1);
            break;
        case 'F':
            // CPL - Cursor Previous Line
            qDebug() << "[TerminalParser] CSI命令 'F' (Cursor Previous Line)，参数:" << param1;
            emit cursorPreviousLine(param1 > 0 ? param1 : 1);
            break;
        case 'G':
            // CHA - Cursor Horizontal Absolute
            qDebug() << "[TerminalParser] CSI命令 'G' (Cursor Horizontal Absolute)，参数:" << param1;
            emit cursorHorizontalAbsolute(param1 > 0 ? param1 : 1);
            break;
        case 'd':
            // VPA - Vertical Position Absolute
            qDebug() << "[TerminalParser] CSI命令 'd' (Vertical Position Absolute)，参数:" << param1;
            emit verticalPositionAbsolute(param1 > 0 ? param1 : 1);
            break;
        case 'H': emit cursorPosition(param1, param2); break;
        case 'J': 
            qDebug() << "[TerminalParser] CSI命令 'J'，参数:" << param1;
            if (param1 == 0) emit eraseDown(); 
            else if (param1 == 1) emit eraseUp();
            else if (param1 == 2) emit clearScreen();
            break;
        case 'K': 
            qDebug() << "[TerminalParser] CSI命令 'K'，参数:" << param1;
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
                        qDebug() << "[TerminalParser] 256 color foreground:" << colorIndex;
                        emit setForegroundColor256(colorIndex);
                        i += 2;  // 跳过后续参数
                        continue;
                    }
                    // ESC[48;5;n m - 背景色 256 色
                    if (param == 48 && i + 2 < m_csiParams.size() && m_csiParams[i + 1] == 5) {
                        // 256 色背景色
                        int colorIndex = m_csiParams[i + 2];
                        qDebug() << "[TerminalParser] 256 color background:" << colorIndex;
                        emit setBackgroundColor256(colorIndex);
                        i += 2;  // 跳过后续参数
                        continue;
                    }
                    // ESC[38;2;r;g;b m - 前景色 RGB 真彩色
                    if (param == 38 && i + 4 < m_csiParams.size() && m_csiParams[i + 1] == 2) {
                        int r = m_csiParams[i + 2];
                        int g = m_csiParams[i + 3];
                        int b = m_csiParams[i + 4];
                        qDebug() << "[TerminalParser] True color foreground:" << r << g << b;
                        i += 4;  // 跳过后续参数
                        continue;
                    }
                    // ESC[48;2;r;g;b m - 背景色 RGB 真彩色
                    if (param == 48 && i + 4 < m_csiParams.size() && m_csiParams[i + 1] == 2) {
                        int r = m_csiParams[i + 2];
                        int g = m_csiParams[i + 3];
                        int b = m_csiParams[i + 4];
                        qDebug() << "[TerminalParser] True color background:" << r << g << b;
                        i += 4;  // 跳过后续参数
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
            qDebug() << "[TerminalParser] CSI命令 'S' (Scroll Up)，参数:" << param1;
            emit scrollUp(param1 > 0 ? param1 : 1);
            break;
        case 'T': 
            qDebug() << "[TerminalParser] CSI命令 'T' (Scroll Down)，参数:" << param1;
            emit scrollDown(param1 > 0 ? param1 : 1);
            break;
        case 'L': 
            qDebug() << "[TerminalParser] CSI命令 'L' (Insert Lines)，参数:" << param1;
            emit insertLines(param1 > 0 ? param1 : 1);
            break;
        case 'M': 
            qDebug() << "[TerminalParser] CSI命令 'M' (Delete Lines)，参数:" << param1;
            emit deleteLines(param1 > 0 ? param1 : 1);
            break;
        case 'r':
            {
                int top = param1 > 0 ? param1 : 1;
                int bottom = param2 > 0 ? param2 : 0;
                qDebug() << "[TerminalParser] CSI命令 'r' (Set Scroll Region)，参数:" << top << bottom;
                emit setScrollRegion(top, bottom);
            }
            break;
        case '@':
            // ICH - Insert Character
            qDebug() << "[TerminalParser] CSI命令 '@' (Insert Chars)，参数:" << param1;
            emit insertChars(param1 > 0 ? param1 : 1);
            break;
        case 'P':
            // DCH - Delete Character
            qDebug() << "[TerminalParser] CSI命令 'P' (Delete Chars)，参数:" << param1;
            emit deleteChars(param1 > 0 ? param1 : 1);
            break;
        case 'X':
            // ECH - Erase Character
            qDebug() << "[TerminalParser] CSI命令 'X' (Erase Chars)，参数:" << param1;
            emit eraseChars(param1 > 0 ? param1 : 1);
            break;
        case 'I':
            // CHT - Cursor Forward Tab
            qDebug() << "[TerminalParser] CSI命令 'I' (Cursor Forward Tab)，参数:" << param1;
            emit cursorForwardTab(param1 > 0 ? param1 : 1);
            break;
        case 'Z':
            // CBT - Cursor Backward Tab
            qDebug() << "[TerminalParser] CSI命令 'Z' (Cursor Backward Tab)，参数:" << param1;
            emit cursorBackwardTab(param1 > 0 ? param1 : 1);
            break;
        case 'g':
            // TBC - Tab Clear
            qDebug() << "[TerminalParser] CSI命令 'g' (Tab Clear)，参数:" << param1;
            emit tabClear(param1);
            break;
        default:
            // 未实现的CSI命令
            qDebug() << "Unimplemented CSI command:" << ch << "params:" << m_csiParams;
            break;
    }
}

void TerminalParser::processPrivateCsiCommand(unsigned char ch)
{
    int param1 = m_csiParams.isEmpty() ? 0 : m_csiParams[0];
    
    switch (ch) {
        case 'h':
            qDebug() << "[TerminalParser] 私有模式设置 (SM ?) 参数:" << param1;
            emit setPrivateMode(param1);
            break;
        case 'l':
            qDebug() << "[TerminalParser] 私有模式重置 (RM ?) 参数:" << param1;
            emit resetPrivateMode(param1);
            break;
        default:
            qDebug() << "[TerminalParser] 未实现的私有CSI命令:" << ch << "params:" << m_csiParams;
            break;
    }
}

void TerminalParser::processEscapeCommand(unsigned char ch)
{
    switch (ch) {
        case 'M': emit cursorUp(); break;
        case 'D': emit cursorDown(); break;
        case 'E': emit cursorDown(); emit carriageReturn(); break;
        case '7': emit cursorSave(); break;
        case '8': emit cursorRestore(); break;
        case 'c': emit resetAttributes(); break;
        case 'H':
            // HTS - Horizontal Tab Set
            qDebug() << "[TerminalParser] ESC H (Tab Set)";
            emit tabSet();
            break;
        default:
            // 未实现的转义命令
            qDebug() << "Unimplemented escape command:" << ch;
            break;
    }
}

void TerminalParser::processOscCommand()
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
        case 52: // 剪贴板操作
            {
                // OSC 52 格式: ESC ] 52 ; c ; <base64_data> BEL/ST
                // c 是剪贴板标识符 (通常是 'c' 或 's')
                // 如果 data 为空或 '?'，表示读取剪贴板
                int sepIndex2 = dataStr.indexOf(';');
                if (sepIndex2 > 0) {
                    QString clipboardId = dataStr.left(sepIndex2);
                    QString clipboardData = dataStr.mid(sepIndex2 + 1);

                    if (clipboardData.isEmpty() || clipboardData == "?") {
                        // 读取剪贴板请求
                        qDebug() << "[TerminalParser] OSC 52 读取剪贴板请求";
                        emit clipboardRead();
                    } else {
                        // 写入剪贴板 (base64 编码)
                        QByteArray decoded = QByteArray::fromBase64(clipboardData.toUtf8());
                        QString text = QString::fromUtf8(decoded);
                        qDebug() << "[TerminalParser] OSC 52 写入剪贴板:" << text.left(50);
                        emit clipboardWrite(text);
                    }
                }
            }
            break;
        default:
            qDebug() << "Unimplemented OSC command:" << oscNum << dataStr;
            break;
    }
}

void TerminalParser::resetCsi()
{
    m_csiParams.clear();
    m_csiIntermediate.clear();
    m_privateMode = false;
    // 添加默认参数0
    m_csiParams.append(0);
}

void TerminalParser::resetOsc()
{
    m_oscBuffer.clear();
}

void TerminalParser::processCharsetCommand(unsigned char ch)
{
    int gset = 0;
    switch (m_charsetState) {
        case CharsetG0: gset = 0; m_g0Charset = ch; break;
        case CharsetG1: gset = 1; m_g1Charset = ch; break;
        case CharsetG2: gset = 2; break;
        case CharsetG3: gset = 3; break;
        default: return;
    }

    qDebug() << "[TerminalParser] 设置字符集 G" << gset << "为:" << QChar(ch);
    emit setCharacterSet(gset, ch);
}

void TerminalParser::dcsEntryState(unsigned char ch)
{
    if (ch >= 0x30 && ch <= 0x3F) {
        m_state = StateDcsParam;
        dcsParamState(ch);
    } else if (ch >= 0x20 && ch <= 0x2F) {
        m_state = StateDcsIntermediate;
        m_dcsIntermediate.append(QChar(ch));
    } else if (ch >= 0x40 && ch <= 0x7E) {
        m_state = StateDcsPassthrough;
    } else {
        m_state = StateDcsIgnore;
    }
}

void TerminalParser::dcsParamState(unsigned char ch)
{
    if (ch >= 0x30 && ch <= 0x3F) {
        if (ch == ';') {
            m_dcsParams.append(0);
        } else if (ch >= '0' && ch <= '9') {
            if (m_dcsParams.isEmpty()) {
                m_dcsParams.append(0);
            }
            m_dcsParams.last() = m_dcsParams.last() * 10 + (ch - '0');
        }
    } else if (ch >= 0x20 && ch <= 0x2F) {
        m_state = StateDcsIntermediate;
        m_dcsIntermediate.append(QChar(ch));
    } else if (ch >= 0x40 && ch <= 0x7E) {
        m_state = StateDcsPassthrough;
    } else {
        m_state = StateDcsIgnore;
    }
}

void TerminalParser::dcsIntermediateState(unsigned char ch)
{
    if (ch >= 0x20 && ch <= 0x2F) {
        m_dcsIntermediate.append(QChar(ch));
    } else if (ch >= 0x40 && ch <= 0x7E) {
        m_state = StateDcsPassthrough;
    } else {
        m_state = StateDcsIgnore;
    }
}

void TerminalParser::dcsPassthroughState(unsigned char ch)
{
    if (ch == 0x1B) {
        // ESC - 可能是 DCS 结束序列 (ESC \)
        // 简化处理：直接处理 DCS 命令
        processDcsCommand();
        m_state = StateGround;
    } else if (ch == 0x9C) {
        // ST (String Terminator) - DCS 结束
        processDcsCommand();
        m_state = StateGround;
    } else {
        // 检查 DCS 数据长度
        if (m_dcsData.size() >= MAX_DCS_LENGTH) {
            qDebug() << "[TerminalParser] DCS 数据溢出，忽略";
            m_state = StateDcsIgnore;
            return;
        }
        // 收集 DCS 数据
        m_dcsData.append(ch);
    }
}

void TerminalParser::dcsIgnoreState(unsigned char ch)
{
    if (ch == 0x1B || ch == 0x9C) {
        // ESC 或 ST - DCS 结束
        m_state = StateGround;
    }
    // 其他字符继续忽略
}

void TerminalParser::processDcsCommand()
{
    // 基本的 DCS 命令处理
    // 目前只记录日志，不做实际处理
    qDebug() << "[TerminalParser] DCS 命令 - 参数:" << m_dcsParams
             << "中间字符:" << m_dcsIntermediate
             << "数据长度:" << m_dcsData.size();

    // 未来可以在这里添加 SIXEL 图形支持等
}

void TerminalParser::resetDcs()
{
    m_dcsParams.clear();
    m_dcsIntermediate.clear();
    m_dcsData.clear();
}