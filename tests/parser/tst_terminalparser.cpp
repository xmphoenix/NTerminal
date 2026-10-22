// ============================================================================
// TerminalParser 单元测试（VT100/ANSI DEC 状态机）
//
// 参考规范：
//   https://vt100.net/emu/dec_ansi_parser
//   https://www2.ccs.neu.edu/research/gpc/VonaUtils/vona/terminal/vtansi.htm
//
// 测试直接对解析器信号做断言（解析器为纯逻辑 QObject，无 UI 依赖）。
// 注意：按当前实现，无参数 CSI 序列的参数值为 0（resetCsi 预置 0），
// 如 "\x1b[A" → cursorUp(0)，由终端代理侧负责将 0 视为 1 处理。
// ============================================================================

#include <QtTest>
#include "terminal/terminalparser.h"

class TestTerminalParser : public QObject
{
    Q_OBJECT

private slots:
    // ---- Ground：普通文本与控制字符 ----
    void plainAsciiText();
    void controlCharacters();
    void utf8MultibyteCharacter();
    void delCharacterIsIgnored();

    // ---- CSI：光标移动 ----
    void cursorMovementWithParams();
    void cursorMovementDefaults();
    void cursorPosition();
    void relativePositioningCommands();

    // ---- CSI：擦除 ----
    void eraseDisplayModes();
    void eraseLineModes();

    // ---- CSI：SGR 属性 ----
    void setGraphicsAttributes();
    void sgr256ColorForeground();
    void sgr256ColorBackground();

    // ---- CSI：其他 ----
    void privateModes();
    void scrollRegion();
    void deviceQueries();
    void lineAndCharEditing();

    // ---- ESC：两字符转义命令 ----
    void escCommands();

    // ---- OSC：操作系统命令 ----
    void setWindowTitleOsc();
    void clipboardOsc52();

    // ---- 字符集 ----
    void charsetDesignation();

    // ---- 健壮性 ----
    void splitSequenceAcrossChunks();
    void dcsPassthroughRecovery();
    void unknownCsiIsIgnoredSafely();
};

// ---- Ground ---------------------------------------------------------------

void TestTerminalParser::plainAsciiText()
{
    TerminalParser parser;
    QSignalSpy textSpy(&parser, &TerminalParser::textReceived);

    parser.processData("AB");

    QCOMPARE(textSpy.count(), 2);
    QCOMPARE(textSpy.at(0).at(0).toString(), QStringLiteral("A"));
    QCOMPARE(textSpy.at(1).at(0).toString(), QStringLiteral("B"));
}

void TestTerminalParser::controlCharacters()
{
    TerminalParser parser;
    QSignalSpy bellSpy(&parser, &TerminalParser::bell);
    QSignalSpy bsSpy(&parser, &TerminalParser::backspace);
    QSignalSpy tabSpy(&parser, &TerminalParser::horizontalTab);
    QSignalSpy lfSpy(&parser, &TerminalParser::lineFeed);
    QSignalSpy vtSpy(&parser, &TerminalParser::verticalTab);
    QSignalSpy ffSpy(&parser, &TerminalParser::formFeed);
    QSignalSpy crSpy(&parser, &TerminalParser::carriageReturn);

    parser.processData(QByteArray("\x07\x08\x09\x0A\x0B\x0C\x0D"));

    QCOMPARE(bellSpy.count(), 1);
    QCOMPARE(bsSpy.count(), 1);
    QCOMPARE(tabSpy.count(), 1);
    QCOMPARE(lfSpy.count(), 1);
    QCOMPARE(vtSpy.count(), 1);
    QCOMPARE(ffSpy.count(), 1);
    QCOMPARE(crSpy.count(), 1);
}

void TestTerminalParser::utf8MultibyteCharacter()
{
    TerminalParser parser;
    QSignalSpy textSpy(&parser, &TerminalParser::textReceived);

    // "中" = E4 B8 AD（3 字节 UTF-8）→ 应合并为一次 textReceived
    parser.processData("中");

    QCOMPARE(textSpy.count(), 1);
    QCOMPARE(textSpy.at(0).at(0).toString(), QStringLiteral("中"));
}

void TestTerminalParser::delCharacterIsIgnored()
{
    TerminalParser parser;
    QSignalSpy textSpy(&parser, &TerminalParser::textReceived);

    // 注意字符串拼接：避免 "\x7FB" 被贪婪解析为十六进制转义 \x7FB
    parser.processData(QByteArray("A\x7F" "B"));

    // DEL 被忽略，两侧字符正常输出
    QCOMPARE(textSpy.count(), 2);
}

// ---- CSI：光标移动 ---------------------------------------------------------

void TestTerminalParser::cursorMovementWithParams()
{
    TerminalParser parser;
    QSignalSpy upSpy(&parser, &TerminalParser::cursorUp);
    QSignalSpy downSpy(&parser, &TerminalParser::cursorDown);
    QSignalSpy fwdSpy(&parser, &TerminalParser::cursorForward);
    QSignalSpy backSpy(&parser, &TerminalParser::cursorBackward);

    parser.processData("\x1b[5A\x1b[3B\x1b[7C\x1b[2D");

    QCOMPARE(upSpy.count(), 1);
    QCOMPARE(upSpy.at(0).at(0).toInt(), 5);
    QCOMPARE(downSpy.count(), 1);
    QCOMPARE(downSpy.at(0).at(0).toInt(), 3);
    QCOMPARE(fwdSpy.count(), 1);
    QCOMPARE(fwdSpy.at(0).at(0).toInt(), 7);
    QCOMPARE(backSpy.count(), 1);
    QCOMPARE(backSpy.at(0).at(0).toInt(), 2);
}

void TestTerminalParser::cursorMovementDefaults()
{
    TerminalParser parser;
    QSignalSpy upSpy(&parser, &TerminalParser::cursorUp);

    // 无参数：当前实现参数值为 0（由代理侧按 1 处理）
    parser.processData("\x1b[A");

    QCOMPARE(upSpy.count(), 1);
    QCOMPARE(upSpy.at(0).at(0).toInt(), 0);
}

void TestTerminalParser::cursorPosition()
{
    TerminalParser parser;
    QSignalSpy posSpy(&parser, &TerminalParser::cursorPosition);

    parser.processData("\x1b[2;3H");   // CUP：1 起始
    parser.processData("\x1b[H");      // 归位（无参数 → 0,0）

    QCOMPARE(posSpy.count(), 2);
    QCOMPARE(posSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(posSpy.at(0).at(1).toInt(), 3);
    QCOMPARE(posSpy.at(1).at(0).toInt(), 0);
    QCOMPARE(posSpy.at(1).at(1).toInt(), 0);
}

void TestTerminalParser::relativePositioningCommands()
{
    TerminalParser parser;
    QSignalSpy cnlSpy(&parser, &TerminalParser::cursorNextLine);
    QSignalSpy cplSpy(&parser, &TerminalParser::cursorPreviousLine);
    QSignalSpy chaSpy(&parser, &TerminalParser::cursorHorizontalAbsolute);
    QSignalSpy vpaSpy(&parser, &TerminalParser::verticalPositionAbsolute);

    parser.processData("\x1b[2E\x1b[3F\x1b[4G\x1b[5d");

    QCOMPARE(cnlSpy.count(), 1);
    QCOMPARE(cnlSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(cplSpy.count(), 1);
    QCOMPARE(cplSpy.at(0).at(0).toInt(), 3);
    QCOMPARE(chaSpy.count(), 1);
    QCOMPARE(chaSpy.at(0).at(0).toInt(), 4);
    QCOMPARE(vpaSpy.count(), 1);
    QCOMPARE(vpaSpy.at(0).at(0).toInt(), 5);
}

// ---- CSI：擦除 -------------------------------------------------------------

void TestTerminalParser::eraseDisplayModes()
{
    TerminalParser parser;
    QSignalSpy eraseDownSpy(&parser, &TerminalParser::eraseDown);
    QSignalSpy eraseUpSpy(&parser, &TerminalParser::eraseUp);
    QSignalSpy clearSpy(&parser, &TerminalParser::clearScreen);

    parser.processData("\x1b[J");      // 默认 = 0：光标到末尾
    parser.processData("\x1b[0J");
    parser.processData("\x1b[1J");      // 开头到光标
    parser.processData("\x1b[2J");      // 全屏

    QCOMPARE(eraseDownSpy.count(), 2);
    QCOMPARE(eraseUpSpy.count(), 1);
    QCOMPARE(clearSpy.count(), 1);
}

void TestTerminalParser::eraseLineModes()
{
    TerminalParser parser;
    QSignalSpy eolSpy(&parser, &TerminalParser::eraseEndOfLine);
    QSignalSpy solSpy(&parser, &TerminalParser::eraseStartOfLine);
    QSignalSpy clearLineSpy(&parser, &TerminalParser::clearLine);

    parser.processData("\x1b[K");
    parser.processData("\x1b[1K");
    parser.processData("\x1b[2K");

    QCOMPARE(eolSpy.count(), 1);
    QCOMPARE(solSpy.count(), 1);
    QCOMPARE(clearLineSpy.count(), 1);
}

// ---- CSI：SGR --------------------------------------------------------------

void TestTerminalParser::setGraphicsAttributes()
{
    TerminalParser parser;
    QSignalSpy attrSpy(&parser, &TerminalParser::setAttributes);
    QSignalSpy resetSpy(&parser, &TerminalParser::resetAttributes);

    // QVector<int> 经 QSignalSpy 的 QVariant 转换在 Qt5 下不可靠，
    // 直接用类型安全的槽连接捕获参数
    QVector<QVector<int>> captured;
    QObject::connect(&parser, &TerminalParser::setAttributes,
                     [&captured](const QVector<int> &params) { captured.append(params); });

    parser.processData("\x1b[1;31m");   // 粗体 + 红色前景
    parser.processData("\x1b[4m");      // 下划线

    QCOMPARE(attrSpy.count(), 2);
    QCOMPARE(captured.count(), 2);
    QCOMPARE(captured.at(0), (QVector<int>{1, 31}));
    QCOMPARE(captured.at(1), (QVector<int>{4}));
    QCOMPARE(resetSpy.count(), 0);
}

void TestTerminalParser::sgr256ColorForeground()
{
    TerminalParser parser;
    QSignalSpy fgSpy(&parser, &TerminalParser::setForegroundColor256);
    QSignalSpy attrSpy(&parser, &TerminalParser::setAttributes);

    parser.processData("\x1b[38;5;196m");

    QCOMPARE(fgSpy.count(), 1);
    QCOMPARE(fgSpy.at(0).at(0).toInt(), 196);
    // 同时还会发出含完整参数的 setAttributes
    QCOMPARE(attrSpy.count(), 1);
}

void TestTerminalParser::sgr256ColorBackground()
{
    TerminalParser parser;
    QSignalSpy bgSpy(&parser, &TerminalParser::setBackgroundColor256);

    parser.processData("\x1b[48;5;21m");

    QCOMPARE(bgSpy.count(), 1);
    QCOMPARE(bgSpy.at(0).at(0).toInt(), 21);
}

// ---- CSI：其他 -------------------------------------------------------------

void TestTerminalParser::privateModes()
{
    TerminalParser parser;
    QSignalSpy setSpy(&parser, &TerminalParser::setPrivateMode);
    QSignalSpy resetSpy(&parser, &TerminalParser::resetPrivateMode);

    parser.processData("\x1b[?1049h");  // 进入备用屏幕
    parser.processData("\x1b[?25l");    // 隐藏光标
    parser.processData("\x1b[?25h");    // 显示光标
    parser.processData("\x1b[?1049l");  // 退出备用屏幕

    QCOMPARE(setSpy.count(), 2);
    QCOMPARE(setSpy.at(0).at(0).toInt(), 1049);
    QCOMPARE(setSpy.at(1).at(0).toInt(), 25);
    QCOMPARE(resetSpy.count(), 2);
    QCOMPARE(resetSpy.at(0).at(0).toInt(), 25);
    QCOMPARE(resetSpy.at(1).at(0).toInt(), 1049);
}

void TestTerminalParser::scrollRegion()
{
    TerminalParser parser;
    QSignalSpy regionSpy(&parser, &TerminalParser::setScrollRegion);

    parser.processData("\x1b[1;24r");

    QCOMPARE(regionSpy.count(), 1);
    QCOMPARE(regionSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(regionSpy.at(0).at(1).toInt(), 24);
}

void TestTerminalParser::deviceQueries()
{
    TerminalParser parser;
    QSignalSpy dsrSpy(&parser, &TerminalParser::deviceStatusReport);
    QSignalSpy cpsSpy(&parser, &TerminalParser::cursorPositionReportRequest);
    QSignalSpy pdaSpy(&parser, &TerminalParser::primaryDeviceAttributes);

    parser.processData("\x1b[5n");
    parser.processData("\x1b[6n");
    parser.processData("\x1b[c");

    QCOMPARE(dsrSpy.count(), 1);
    QCOMPARE(cpsSpy.count(), 1);
    QCOMPARE(pdaSpy.count(), 1);
}

void TestTerminalParser::lineAndCharEditing()
{
    TerminalParser parser;
    QSignalSpy ilSpy(&parser, &TerminalParser::insertLines);
    QSignalSpy dlSpy(&parser, &TerminalParser::deleteLines);
    QSignalSpy ichSpy(&parser, &TerminalParser::insertChars);
    QSignalSpy dchSpy(&parser, &TerminalParser::deleteChars);
    QSignalSpy echSpy(&parser, &TerminalParser::eraseChars);

    parser.processData("\x1b[2L\x1b[3M\x1b[4@\x1b[5P\x1b[6X");

    QCOMPARE(ilSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(dlSpy.at(0).at(0).toInt(), 3);
    QCOMPARE(ichSpy.at(0).at(0).toInt(), 4);
    QCOMPARE(dchSpy.at(0).at(0).toInt(), 5);
    QCOMPARE(echSpy.at(0).at(0).toInt(), 6);
}

// ---- ESC -------------------------------------------------------------------

void TestTerminalParser::escCommands()
{
    TerminalParser parser;
    QSignalSpy upSpy(&parser, &TerminalParser::cursorUp);
    QSignalSpy downSpy(&parser, &TerminalParser::cursorDown);
    QSignalSpy crSpy(&parser, &TerminalParser::carriageReturn);
    QSignalSpy saveSpy(&parser, &TerminalParser::cursorSave);
    QSignalSpy restoreSpy(&parser, &TerminalParser::cursorRestore);
    QSignalSpy resetSpy(&parser, &TerminalParser::resetAttributes);
    QSignalSpy tabSpy(&parser, &TerminalParser::tabSet);

    // 注意字符串拼接："\x1bD" 会被贪婪解析为 \x1BD（超出 char 范围），
    // 必须写作 "\x1b" "D"，其余同理（后随十六进制字符的场合）
    parser.processData("\x1b" "M");   // RI：光标上移
    parser.processData("\x1b" "D");   // IND：光标下移
    parser.processData("\x1b" "E");   // NEL：下移 + 回车
    parser.processData("\x1b" "7");   // DECSC
    parser.processData("\x1b" "8");   // DECRC
    parser.processData("\x1b" "c");   // RIS（按当前实现发 resetAttributes）
    parser.processData("\x1b" "H");   // HTS

    QCOMPARE(upSpy.count(), 1);
    QCOMPARE(downSpy.count(), 2);       // D + E
    QCOMPARE(crSpy.count(), 1);         // E 附带
    QCOMPARE(saveSpy.count(), 1);
    QCOMPARE(restoreSpy.count(), 1);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(tabSpy.count(), 1);
}

// ---- OSC -------------------------------------------------------------------

void TestTerminalParser::setWindowTitleOsc()
{
    TerminalParser parser;
    QSignalSpy titleSpy(&parser, &TerminalParser::setWindowTitle);

    parser.processData("\x1b]0;My Title\x07");   // BEL 终止

    QCOMPARE(titleSpy.count(), 1);
    QCOMPARE(titleSpy.at(0).at(0).toString(), QStringLiteral("My Title"));
}

void TestTerminalParser::clipboardOsc52()
{
    TerminalParser parser;
    QSignalSpy writeSpy(&parser, &TerminalParser::clipboardWrite);

    // "hello" 的 base64 = aGVsbG8=
    parser.processData("\x1b]52;c;aGVsbG8=\x07");

    QCOMPARE(writeSpy.count(), 1);
    QCOMPARE(writeSpy.at(0).at(0).toString(), QStringLiteral("hello"));
}

// ---- 字符集 ----------------------------------------------------------------

void TestTerminalParser::charsetDesignation()
{
    TerminalParser parser;

    // char 参数经 QVariant 转换在 Qt5/Qt6 间存在差异，
    // 用类型安全的槽连接捕获 (gset, charset)
    QVector<QPair<int, char>> captured;
    QObject::connect(&parser, &TerminalParser::setCharacterSet,
                     [&captured](int gset, char charset) {
                         captured.append(qMakePair(gset, charset));
                     });

    parser.processData("\x1b(B");   // G0 = ASCII
    parser.processData("\x1b(0");   // G0 = DEC 图形
    parser.processData("\x1b)0");   // G1 = DEC 图形

    QCOMPARE(captured.count(), 3);
    QCOMPARE(captured.at(0).first, 0);
    QCOMPARE(captured.at(0).second, 'B');
    QCOMPARE(captured.at(1).first, 0);
    QCOMPARE(captured.at(1).second, '0');
    QCOMPARE(captured.at(2).first, 1);
    QCOMPARE(captured.at(2).second, '0');
}

// ---- 健壮性 ----------------------------------------------------------------

void TestTerminalParser::splitSequenceAcrossChunks()
{
    // 状态机必须跨 processData 调用保持状态（TCP/串口分帧到达）
    TerminalParser parser;
    QSignalSpy clearSpy(&parser, &TerminalParser::clearScreen);

    parser.processData("\x1b[2");
    QCOMPARE(clearSpy.count(), 0);
    parser.processData("J");
    QCOMPARE(clearSpy.count(), 1);
}

void TestTerminalParser::dcsPassthroughRecovery()
{
    // DCS 序列内容应被安全吞掉，结束后解析器回到 Ground
    TerminalParser parser;
    QSignalSpy textSpy(&parser, &TerminalParser::textReceived);

    parser.processData("\x1bP1;2q data payload\x1b\\");
    parser.processData("X");

    // ESC \ 结束后紧跟的 'X' 必须作为普通文本输出
    QCOMPARE(textSpy.count(), 1);
    QCOMPARE(textSpy.at(0).at(0).toString(), QStringLiteral("X"));
}

void TestTerminalParser::unknownCsiIsIgnoredSafely()
{
    // 未知 final 字符：不得崩溃，也不得泄漏到文本输出
    TerminalParser parser;
    QSignalSpy textSpy(&parser, &TerminalParser::textReceived);

    parser.processData("\x1b[9z");   // 'z' 是未知 CSI 终结符
    parser.processData("ok");

    QCOMPARE(textSpy.count(), 2);
    QCOMPARE(textSpy.at(0).at(0).toString(), QStringLiteral("o"));
    QCOMPARE(textSpy.at(1).at(0).toString(), QStringLiteral("k"));
}

QTEST_MAIN(TestTerminalParser)
#include "tst_terminalparser.moc"
