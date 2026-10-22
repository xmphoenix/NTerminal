// ============================================================================
// TerminalProxy 屏幕模型测试
//
// 通过 processTerminalData() 将数据直接注入解析管线（等价于远端数据到达，
// 不经过 IPC 与真实连接），再用只读状态查询接口
// （screenText/cellAt/cursorRow/cursorColumn 等）断言屏幕状态。
//
// TerminalProxy 构造函数不访问全局单例（主题/字体为注入式依赖），
// 因此本测试无需初始化 ThemeManager/FontManager。
// ============================================================================

#include <QtTest>
#include <QRegularExpression>
#include "terminal/terminalproxy.h"
#include "terminal/terminaltheme.h"

class TestTerminalProxy : public QObject
{
    Q_OBJECT

private slots:
    // ---- 初始状态 ----
    void initialState();

    // ---- 文本写入与光标推进 ----
    void textRenderingAdvancesCursor();
    void carriageReturnOverwritesLine();
    void lineFeedPreservesColumn();

    // ---- 定位与清屏 ----
    void cursorPositionIsOneBased();
    void clearScreenEmptiesContent();

    // ---- SGR 属性渲染到 Cell ----
    void boldAttributeStoredInCell();
    void ansiForegroundStoredInCell();
    void ansi256ForegroundStoredInCell();

    // ---- 备用屏幕 ----
    void alternateScreenToggle();

    // ---- 宽字符 ----
    void wideCharacterMarked();

    // ---- 数据注入信号 ----
    void processTerminalDataEmitsSignal();

private:
    // 构造带可区分 ANSI 调色板的主题（16 色各不相同）
    static TerminalTheme makeTestTheme();
};

TerminalTheme TestTerminalProxy::makeTestTheme()
{
    TerminalTheme theme;
    theme.name = QStringLiteral("unit-test");
    theme.id = QStringLiteral("unit-test");
    theme.defaultForeground = Qt::white;
    theme.defaultBackground = Qt::black;
    theme.cursorColor = Qt::white;
    theme.selectionBackground = QColor(0x33, 0x33, 0x88);
    theme.selectionForeground = Qt::white;
    for (int i = 0; i < 16; ++i) {
        theme.ansiColors[i] = QColor(i * 15, 0x80 + i * 4, 255 - i * 15);
    }
    return theme;
}

// ---- 初始状态 ---------------------------------------------------------------

void TestTerminalProxy::initialState()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    QCOMPARE(proxy.columns(), 200);
    QCOMPARE(proxy.rows(), 24);
    QCOMPARE(proxy.cursorRow(), 0);
    QCOMPARE(proxy.cursorColumn(), 0);
    QVERIFY(!proxy.useAlternateScreen());
    QVERIFY(!proxy.isConnected());
    QVERIFY(!proxy.hasSelection());
}

// ---- 文本写入 ---------------------------------------------------------------

void TestTerminalProxy::textRenderingAdvancesCursor()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData("Hello");

    QCOMPARE(proxy.screenText().split('\n').at(0), QStringLiteral("Hello"));
    QCOMPARE(proxy.cursorColumn(), 5);
    QCOMPARE(proxy.cursorRow(), 0);
    QCOMPARE(proxy.cellAt(0, 0).character, QChar('H'));
    QCOMPARE(proxy.cellAt(0, 4).character, QChar('o'));
}

void TestTerminalProxy::carriageReturnOverwritesLine()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData("Hello");
    proxy.processTerminalData("\r");     // 回到行首
    QCOMPARE(proxy.cursorColumn(), 0);

    proxy.processTerminalData("X");      // 覆盖第一个字符
    QCOMPARE(proxy.screenText().split('\n').at(0), QStringLiteral("Xello"));
    QCOMPARE(proxy.cellAt(0, 0).character, QChar('X'));
    QCOMPARE(proxy.cellAt(0, 1).character, QChar('e'));
}

void TestTerminalProxy::lineFeedPreservesColumn()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData("AB");
    proxy.processTerminalData("\n");     // LF 仅换行，不回车

    QCOMPARE(proxy.cursorRow(), 1);
    QCOMPARE(proxy.cursorColumn(), 2);   // 列保持
    QCOMPARE(proxy.screenText().split('\n').at(0), QStringLiteral("AB"));
}

// ---- 定位与清屏 -------------------------------------------------------------

void TestTerminalProxy::cursorPositionIsOneBased()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData("A\nB\nC");   // 光标到达第 3 行第 2 列
    proxy.processTerminalData("\x1b[2;3H"); // CUP(2,3) → 0 起始 (1,2)

    QCOMPARE(proxy.cursorRow(), 1);
    QCOMPARE(proxy.cursorColumn(), 2);
}

void TestTerminalProxy::clearScreenEmptiesContent()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData("line one\nline two\nline three");
    QVERIFY(QRegularExpression(QStringLiteral("\\S"))
                .match(proxy.screenText()).hasMatch());

    proxy.processTerminalData("\x1b[2J");   // ED 2：全屏清除

    const QString after = proxy.screenText();
    QVERIFY(!QRegularExpression(QStringLiteral("\\S")).match(after).hasMatch());
}

// ---- SGR 属性 ---------------------------------------------------------------

void TestTerminalProxy::boldAttributeStoredInCell()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData("\x1b[1mB");   // SGR 粗体 + 输出 'B'

    const TerminalProxy::Cell cell = proxy.cellAt(0, 0);
    QCOMPARE(cell.character, QChar('B'));
    QVERIFY(cell.attributes.testFlag(TerminalProxy::Bold));
}

void TestTerminalProxy::ansiForegroundStoredInCell()
{
    TerminalProxy proxy;
    const TerminalTheme theme = makeTestTheme();
    proxy.setTheme(theme);

    proxy.processTerminalData("\x1b[31mR"); // SGR 31 = ANSI 红（索引 1）

    QCOMPARE(proxy.cellAt(0, 0).character, QChar('R'));
    QCOMPARE(proxy.cellAt(0, 0).foreground, theme.ansiColors[1]);
}

void TestTerminalProxy::ansi256ForegroundStoredInCell()
{
    TerminalProxy proxy;
    const TerminalTheme theme = makeTestTheme();
    proxy.setTheme(theme);

    proxy.processTerminalData("\x1b[38;5;196mR");   // xterm 256 色索引 196

    QCOMPARE(proxy.cellAt(0, 0).character, QChar('R'));
    QCOMPARE(proxy.cellAt(0, 0).foreground, theme.ansi256Color(196));
}

// ---- 备用屏幕 ---------------------------------------------------------------

void TestTerminalProxy::alternateScreenToggle()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData("main screen");
    proxy.processTerminalData("\x1b[?1049h");   // 进入备用屏幕（vim 等）
    QVERIFY(proxy.useAlternateScreen());

    proxy.processTerminalData("\x1b[2J");
    // 备用屏内清屏不影响主屏标志
    QVERIFY(proxy.useAlternateScreen());

    proxy.processTerminalData("\x1b[?1049l");   // 退回主屏幕
    QVERIFY(!proxy.useAlternateScreen());
}

// ---- 宽字符 -----------------------------------------------------------------

void TestTerminalProxy::wideCharacterMarked()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    proxy.processTerminalData(QStringLiteral("中").toUtf8());

    const TerminalProxy::Cell cell = proxy.cellAt(0, 0);
    QCOMPARE(cell.character, QChar(0x4E2D));   // '中' = U+4E2D
    QVERIFY(cell.isWide);
}

// ---- 数据注入信号 -----------------------------------------------------------

void TestTerminalProxy::processTerminalDataEmitsSignal()
{
    TerminalProxy proxy;
    proxy.setTheme(makeTestTheme());

    QSignalSpy spy(&proxy, &TerminalProxy::dataReceived);
    proxy.processTerminalData("hi");

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArray("hi"));
}

QTEST_MAIN(TestTerminalProxy)
#include "tst_terminalproxy.moc"
