// ============================================================================
// tst_launchoptions —— src/app/launchoptions 命令行解析测试
//
// 同一可执行文件按参数分流为 GUI / 会话进程 / 测试窗口三种模式，
// 解析行为必须与历史版本严格一致（会话模式优先，参数从 argv[1] 开始）。
// ============================================================================
#include <QtTest>
#include "app/launchoptions.h"

using App::LaunchMode;
using App::LaunchOptions;

class TestLaunchOptions : public QObject
{
    Q_OBJECT

private slots:
    void noArgumentsDefaultsToGui();
    void shortSessionFlag();
    void longSessionFlag();
    void shortTestFlag();
    void longTestFlag();
    void unknownArgumentsIgnored();
    void sessionTakesPrecedenceOverTest();
    void sessionFlagWithoutValueFallsBack();
};

void TestLaunchOptions::noArgumentsDefaultsToGui()
{
    QCOMPARE(LaunchOptions::parse({}).mode, LaunchMode::Gui);
    QCOMPARE(LaunchOptions::parse(QStringList() << "NTerminal.exe").mode, LaunchMode::Gui);
}

void TestLaunchOptions::shortSessionFlag()
{
    const LaunchOptions opts = LaunchOptions::parse(
        QStringList() << "NTerminal.exe" << "-s" << "terminal_7");
    QCOMPARE(opts.mode, LaunchMode::Session);
    QCOMPARE(opts.sessionId, QString("terminal_7"));
}

void TestLaunchOptions::longSessionFlag()
{
    const LaunchOptions opts = LaunchOptions::parse(
        QStringList() << "NTerminal.exe" << "--session" << "abc123");
    QCOMPARE(opts.mode, LaunchMode::Session);
    QCOMPARE(opts.sessionId, QString("abc123"));
}

void TestLaunchOptions::shortTestFlag()
{
    QCOMPARE(LaunchOptions::parse(QStringList() << "NTerminal.exe" << "-t").mode,
             LaunchMode::Test);
}

void TestLaunchOptions::longTestFlag()
{
    QCOMPARE(LaunchOptions::parse(QStringList() << "NTerminal.exe" << "--test").mode,
             LaunchMode::Test);
}

void TestLaunchOptions::unknownArgumentsIgnored()
{
    QCOMPARE(LaunchOptions::parse(QStringList() << "app" << "--verbose" << "-x").mode,
             LaunchMode::Gui);
}

void TestLaunchOptions::sessionTakesPrecedenceOverTest()
{
    // 与历史行为一致：-s 出现时 break，优先进入会话模式
    const LaunchOptions opts = LaunchOptions::parse(
        QStringList() << "app" << "-t" << "-s" << "id42");
    QCOMPARE(opts.mode, LaunchMode::Session);
    QCOMPARE(opts.sessionId, QString("id42"));
}

void TestLaunchOptions::sessionFlagWithoutValueFallsBack()
{
    // -s 位于末尾且无取值：不满足条件，回落（若前面已有 -t 则 Test，否则 Gui）
    QCOMPARE(LaunchOptions::parse(QStringList() << "app" << "-s").mode, LaunchMode::Gui);
    QCOMPARE(LaunchOptions::parse(QStringList() << "app" << "-t" << "-s").mode,
             LaunchMode::Test);
}

QTEST_MAIN(TestLaunchOptions)
#include "tst_launchoptions.moc"
