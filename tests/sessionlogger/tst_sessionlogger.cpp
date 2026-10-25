// ============================================================================
// tst_sessionlogger —— 会话日志模块测试
//
// 覆盖：
//   1. 开关日志：文件创建、头部、Session started/ended、重复 start 不死锁
//   2. ANSI 转义过滤：SGR / OSC 标题 / 备用屏幕（vim/less）内容不落盘
//   3. 行缓冲：\n、\r 原地刷新、退格、UTF-8 中文完整性
//   4. 文本方向：只记录接收数据，不记录发送数据
//   5. 非法日志目录、未 start 直接 stop、文件名净化
// ============================================================================
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "session/sessionlogger.h"

class TestSessionLogger : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void startStopWritesHeaderAndEvents();
    void onlyReceivedDataIsLogged();
    void ansiSequencesAreStripped();
    void alternateScreenContentIsSuppressed();
    void carriageReturnAndBackspaceFlushLines();
    void utf8TextIsPreserved();
    void startWhileLoggingRestartsWithoutDeadlock();
    void stopWithoutStartIsNoop();
    void invalidLogDirectoryFails();
    void fileNameSanitization();
    void timestampsUseConfiguredFormat();
    void pendingLineIsFlushedOnStop();

private:
    SessionLogger::LogOptions defaultOptions() const;

    QTemporaryDir m_tempDir;
};

void TestSessionLogger::initTestCase()
{
    QVERIFY2(m_tempDir.isValid(), qPrintable(m_tempDir.errorString()));
}

SessionLogger::LogOptions TestSessionLogger::defaultOptions() const
{
    SessionLogger::LogOptions options;
    options.fileNamePattern = QStringLiteral("%c_%n_%t.log");
    options.overwriteExisting = false;
    options.encoding = QStringLiteral("utf-8");
    options.enableTimestamp = true;
    options.timestampFormat = QStringLiteral("[%Y-%m-%d %h:%M:%s]");
    return options;
}

void TestSessionLogger::startStopWritesHeaderAndEvents()
{
    SessionLogger logger(QStringLiteral("session-1"), defaultOptions());
    QVERIFY(!logger.isLogging());
    QVERIFY(logger.currentLogFile().isEmpty());

    QVERIFY(logger.startLogging(QStringLiteral("Test Session"), m_tempDir.path(),
                                QStringLiteral("SSH"), QStringLiteral("example.test")));
    QVERIFY(logger.isLogging());

    const QString path = logger.currentLogFile();
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::exists(path));
    QCOMPARE(QFileInfo(path).absolutePath(), QDir(m_tempDir.path()).absolutePath());
    QVERIFY(QFileInfo(path).fileName().startsWith(QStringLiteral("ssh_Test Session_")));

    logger.stopLogging();
    QVERIFY(!logger.isLogging());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("# NTerminal Session Log")));
    QVERIFY(content.contains(QStringLiteral("# Session: Test Session")));
    QVERIFY(content.contains(QStringLiteral("# Started:")));
    QVERIFY(content.contains(QStringLiteral("Session started: Test Session")));
    QVERIFY(content.contains(QStringLiteral("Session ended")));
}

void TestSessionLogger::onlyReceivedDataIsLogged()
{
    SessionLogger logger(QStringLiteral("session-2"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("Direction"), m_tempDir.path(),
                                QStringLiteral("serial")));

    logger.logData(QByteArray("received-line\n"), true);
    logger.logData(QByteArray("sent-line\n"), false);   // 发送方向不记录
    logger.stopLogging();

    QFile file(logger.currentLogFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("received-line")));
    QVERIFY(!content.contains(QStringLiteral("sent-line")));
}

void TestSessionLogger::ansiSequencesAreStripped()
{
    SessionLogger logger(QStringLiteral("session-3"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("Ansi"), m_tempDir.path(),
                                QStringLiteral("ssh")));

    logger.logData(QByteArray("\x1b[31m红色文本\x1b[0m\n"), true);
    logger.logData(QByteArray("\x1b]0;窗口标题\x07"), true);
    logger.logData(QByteArray("\x1b[1;32mgreen\x1b(B\x1b[m\n"), true);
    logger.stopLogging();

    QFile file(logger.currentLogFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("红色文本")));
    QVERIFY(content.contains(QStringLiteral("green")));
    QVERIFY(!content.contains(QStringLiteral("窗口标题")));
    QVERIFY(!content.contains(QChar(0x1b)));
    // SGR 参数不应混入正文
    QVERIFY(!content.contains(QStringLiteral("[31m")));
    QVERIFY(!content.contains(QStringLiteral("1;32m")));
}

void TestSessionLogger::alternateScreenContentIsSuppressed()
{
    SessionLogger logger(QStringLiteral("session-4"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("Alt"), m_tempDir.path(),
                                QStringLiteral("ssh")));

    logger.logData(QByteArray("before\n"), true);
    logger.logData(QByteArray("\x1b[?1049h"), true);   // 进入备用屏幕（vim/less）
    logger.logData(QByteArray("hidden-vim-content\n"), true);
    logger.logData(QByteArray("\x1b[2J\x1b[H"), true);
    logger.logData(QByteArray("\x1b[?1049l"), true);   // 退出备用屏幕
    logger.logData(QByteArray("after\n"), true);
    logger.stopLogging();

    QFile file(logger.currentLogFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("before")));
    QVERIFY(content.contains(QStringLiteral("after")));
    QVERIFY(!content.contains(QStringLiteral("hidden-vim-content")));
}

void TestSessionLogger::carriageReturnAndBackspaceFlushLines()
{
    SessionLogger logger(QStringLiteral("session-5"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("Edits"), m_tempDir.path(),
                                QStringLiteral("serial")));

    logger.logData(QByteArray("progress 10%\rprogress 20%\rprogress 100%\n"), true);
    logger.logData(QByteArray("abcX\b\n"), true);      // 退格删除 X
    logger.logData(QByteArray("one\rtwo\n"), true);
    logger.stopLogging();

    QFile file(logger.currentLogFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());

    // 每个 \r 都把当前行落盘
    QVERIFY(content.contains(QStringLiteral("progress 10%")));
    QVERIFY(content.contains(QStringLiteral("progress 20%")));
    QVERIFY(content.contains(QStringLiteral("progress 100%")));
    QVERIFY(content.contains(QStringLiteral("abc")));
    QVERIFY(!content.contains(QStringLiteral("abcX")));
    QVERIFY(content.contains(QStringLiteral("one")));
    QVERIFY(content.contains(QStringLiteral("two")));
}

void TestSessionLogger::utf8TextIsPreserved()
{
    SessionLogger logger(QStringLiteral("session-6"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("UTF8"), m_tempDir.path(),
                                QStringLiteral("serial")));

    // 多字节字符必须整行按 UTF-8 解码，不能逐字节转 QChar 变成乱码
    logger.logData(QString::fromUtf8("中文输出：温度 25℃，状态正常\n").toUtf8(), true);
    logger.logData(QString::fromUtf8("emoji 😀 test\n").toUtf8(), true);
    // 退格删除一个多字节字符
    logger.logData(QString::fromUtf8("中文X\b\n").toUtf8(), true);
    logger.stopLogging();

    QFile file(logger.currentLogFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("中文输出：温度 25℃，状态正常")));
    QVERIFY(content.contains(QStringLiteral("emoji 😀 test")));
    QVERIFY(content.contains(QStringLiteral("中文")));
    QVERIFY(!content.contains(QStringLiteral("中文X")));
    QVERIFY(!content.contains(QStringLiteral("ä¸­æ")));   // 典型 UTF-8 误码
}

void TestSessionLogger::startWhileLoggingRestartsWithoutDeadlock()
{
    SessionLogger logger(QStringLiteral("session-7"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("First"), m_tempDir.path(),
                                QStringLiteral("ssh")));
    const QString firstFile = logger.currentLogFile();
    logger.logData(QByteArray("first-run\n"), true);

    // 回归：startLogging 曾在持锁状态下调用 stopLogging 造成自锁
    QVERIFY(logger.startLogging(QStringLiteral("Second"), m_tempDir.path(),
                                QStringLiteral("ssh")));
    QVERIFY(logger.isLogging());
    const QString secondFile = logger.currentLogFile();
    logger.logData(QByteArray("second-run\n"), true);
    logger.stopLogging();
    QVERIFY(!logger.isLogging());

    // 会话名不同 => 文件名不同（%n 参与命名），分别校验两次会话的内容
    QVERIFY(firstFile != secondFile);

    QFile first(firstFile);
    QVERIFY(first.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString firstContent = QString::fromUtf8(first.readAll());
    QVERIFY(firstContent.contains(QStringLiteral("first-run")));
    QVERIFY(firstContent.contains(QStringLiteral("Session ended")));
    QCOMPARE(firstContent.count(QStringLiteral("# NTerminal Session Log")), 1);

    QFile second(secondFile);
    QVERIFY(second.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString secondContent = QString::fromUtf8(second.readAll());
    QVERIFY(secondContent.contains(QStringLiteral("second-run")));
    QCOMPARE(secondContent.count(QStringLiteral("# NTerminal Session Log")), 1);
}

void TestSessionLogger::stopWithoutStartIsNoop()
{
    SessionLogger logger(QStringLiteral("session-8"), defaultOptions());
    logger.stopLogging();
    QVERIFY(!logger.isLogging());
    // 不应写出任何文件
    QVERIFY(logger.currentLogFile().isEmpty());
}

void TestSessionLogger::invalidLogDirectoryFails()
{
    const QString filePath = m_tempDir.filePath(QStringLiteral("not-a-directory.txt"));
    QFile blocker(filePath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();

    SessionLogger logger(QStringLiteral("session-9"), defaultOptions());
    QVERIFY(!logger.startLogging(QStringLiteral("Bad"), filePath,
                                 QStringLiteral("ssh")));
    QVERIFY(!logger.isLogging());
}

void TestSessionLogger::fileNameSanitization()
{
    SessionLogger logger(QStringLiteral("session-10"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("a/b:c*?\"<>|"), m_tempDir.path(),
                                QStringLiteral("Serial")));
    const QString fileName = QFileInfo(logger.currentLogFile()).fileName();
    logger.stopLogging();

    QVERIFY(fileName.startsWith(QStringLiteral("serial_")));
    for (const QChar ch : QStringLiteral("/:*?\"<>|")) {
        QVERIFY2(!fileName.contains(ch), qPrintable(fileName));
    }
    QVERIFY(QFileInfo(logger.currentLogFile()).absolutePath()
            == QDir(m_tempDir.path()).absolutePath());
}

void TestSessionLogger::timestampsUseConfiguredFormat()
{
    SessionLogger::LogOptions options = defaultOptions();
    options.timestampFormat = QStringLiteral("[%Y-%m-%d %h:%M:%s]");
    SessionLogger logger(QStringLiteral("session-11"), options);
    QVERIFY(logger.startLogging(QStringLiteral("Time"), m_tempDir.path(),
                                QStringLiteral("ssh")));
    logger.logData(QByteArray("timestamped\n"), true);
    logger.stopLogging();

    QFile file(logger.currentLogFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());

    const QRegularExpression dataLine(
        QStringLiteral("^\\[\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\] timestamped$"),
        QRegularExpression::MultilineOption);
    QVERIFY2(dataLine.match(content).hasMatch(), qPrintable(content));
}

void TestSessionLogger::pendingLineIsFlushedOnStop()
{
    SessionLogger logger(QStringLiteral("session-12"), defaultOptions());
    QVERIFY(logger.startLogging(QStringLiteral("Pending"), m_tempDir.path(),
                                QStringLiteral("serial")));
    logger.logData(QByteArray("no-newline-at-end"), true);
    logger.stopLogging();

    QFile file(logger.currentLogFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("no-newline-at-end")));
}

QTEST_GUILESS_MAIN(TestSessionLogger)
#include "tst_sessionlogger.moc"
