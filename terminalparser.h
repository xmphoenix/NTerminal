#ifndef TERMINALPARSER_H
#define TERMINALPARSER_H

#include <QObject>
#include <QVector>

class TerminalParser : public QObject
{
    Q_OBJECT

public:
    enum ParserState {
        StateGround,
        StateEscape,
        StateCsiEntry,
        StateCsiParam,
        StateCsiIntermediate,
        StateCsiIgnore,
        StateOscString,
        StateDcsEntry,
        StateDcsParam,
        StateDcsIntermediate,
        StateDcsPassthrough,
        StateDcsIgnore,
        StateSosPmApcString,
        StateUtf8
    };

    explicit TerminalParser(QObject *parent = nullptr);
    ~TerminalParser();

    void processByte(unsigned char ch);
    void processData(const QByteArray &data);

signals:
    // 文本和基本控制
    void textReceived(const QString &text);
    void bell();
    void backspace();
    void horizontalTab();
    void lineFeed();
    void verticalTab();
    void formFeed();
    void carriageReturn();
    
    // 光标移动
    void cursorUp(int count = 1);
    void cursorDown(int count = 1);
    void cursorForward(int count = 1);
    void cursorBackward(int count = 1);
    void cursorPosition(int row, int column);
    void cursorNextLine(int count = 1);       // CNL
    void cursorPreviousLine(int count = 1);   // CPL
    void cursorHorizontalAbsolute(int col);   // CHA
    void verticalPositionAbsolute(int row);   // VPA
    void cursorHome();
    void cursorSave();
    void cursorRestore();
    
    // 编辑操作
    void clearScreen();
    void eraseDown();
    void eraseUp();
    void clearLine();
    void eraseEndOfLine();
    void eraseStartOfLine();
    void insertLines(int count);
    void deleteLines(int count);
    void scrollUp(int count);
    void scrollDown(int count);
    void deleteChars(int count = 1);
    void eraseChars(int count = 1);
    void insertChars(int count = 1);
    void setScrollRegion(int top, int bottom);

    // 制表符操作
    void tabSet();                // HTS - 设置当前列为制表位
    void tabClear(int param);     // TBC - 清除制表位
    void cursorForwardTab(int count);   // CHT - 前进到下N个制表位
    void cursorBackwardTab(int count);  // CBT - 后退到上N个制表位
    
    // 字符属性
    void setAttributes(const QVector<int> &params);
    void resetAttributes();
    void setForegroundColor256(int colorIndex);
    void setBackgroundColor256(int colorIndex);
    
    // 模式设置
    void setMode(int param);
    void resetMode(int param);
    void setPrivateMode(int param);
    void resetPrivateMode(int param);
    
    // 键盘和终端状态
    void deviceStatusReport();           // ESC [ 5 n - 设备状态报告请求
    void cursorPositionReportRequest();  // ESC [ 6 n - 光标位置报告请求
    void cursorPositionReport(int row, int column);
    void primaryDeviceAttributes();      // ESC [ c - 主设备属性查询
    void secondaryDeviceAttributes();    // ESC [ > c - 次设备属性查询
    
    // 窗口标题和颜色
    void setWindowTitle(const QString &title);
    void setForegroundColor(int color);
    void setBackgroundColor(int color);
    void resetColors();

    // 剪贴板操作
    void clipboardRead();                          // OSC 52 读取剪贴板请求
    void clipboardWrite(const QString &data);      // OSC 52 写入剪贴板

    // 字符集
    void setCharacterSet(int gset, char charset);  // 设置 G0/G1/G2/G3 字符集

    // 错误和调试
    void parseError(const QString &error);

private:
    void groundState(unsigned char ch);
    void escapeState(unsigned char ch);
    void csiEntryState(unsigned char ch);
    void csiParamState(unsigned char ch);
    void csiIntermediateState(unsigned char ch);
    void csiIgnoreState(unsigned char ch);
    void oscStringState(unsigned char ch);
    void dcsEntryState(unsigned char ch);
    void dcsParamState(unsigned char ch);
    void dcsIntermediateState(unsigned char ch);
    void dcsPassthroughState(unsigned char ch);
    void dcsIgnoreState(unsigned char ch);

    void processCsiCommand(unsigned char ch);
    void processPrivateCsiCommand(unsigned char ch);
    void processEscapeCommand(unsigned char ch);
    void processOscCommand();
    void processDcsCommand();
    void processCharsetCommand(unsigned char ch);  // 处理字符集切换

    void resetCsi();
    void resetOsc();
    void resetDcs();

    ParserState m_state;
    QVector<int> m_csiParams;
    QString m_csiIntermediate;
    QString m_oscBuffer;
    QByteArray m_utf8Buffer;
    int m_utf8Remaining;
    bool m_privateMode;

    // DCS 状态
    QVector<int> m_dcsParams;
    QString m_dcsIntermediate;
    QByteArray m_dcsData;

    // 字符集状态
    enum CharsetState {
        CharsetNone,
        CharsetG0,
        CharsetG1,
        CharsetG2,
        CharsetG3
    };
    CharsetState m_charsetState;
    char m_g0Charset;  // G0 字符集 ('B' = ASCII, '0' = DEC graphics)
    char m_g1Charset;  // G1 字符集
    bool m_useG1;      // 当前是否使用 G1 字符集

    // 输入验证和安全
    static const int MAX_CSI_PARAMS = 32;
    static const int MAX_OSC_LENGTH = 4096;
    static const int MAX_DCS_LENGTH = 65536;

    void validateAndResetIfNeeded();
};

#endif // TERMINALPARSER_H