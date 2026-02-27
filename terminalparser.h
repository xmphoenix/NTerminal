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
    
    // 字符属性
    void setAttributes(const QVector<int> &params);
    void resetAttributes();
    
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
    
    void processCsiCommand(unsigned char ch);
    void processPrivateCsiCommand(unsigned char ch);
    void processEscapeCommand(unsigned char ch);
    void processOscCommand();
    
    void resetCsi();
    void resetOsc();
    
    ParserState m_state;
    QVector<int> m_csiParams;
    QString m_csiIntermediate;
    QString m_oscBuffer;
    QByteArray m_utf8Buffer;
    int m_utf8Remaining;
    bool m_privateMode;
};

#endif // TERMINALPARSER_H