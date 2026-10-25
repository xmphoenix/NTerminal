// ============================================================================
// tst_transfer —— 终端文件传输协议测试
//
// 覆盖：
//   1. FileTransfer 基类：元数据、写回调注入、进度/状态信号、只结束一次
//   2. 接收文件名净化（路径穿越、非法字符）
//   3. ASCII：CRLF 转换与分块发送、接收后手动停止保存、非法参数
//   4. XMODEM：CRC 握手 / 校验和回退、空文件、坏块 NAK 与重新同步、取消
//   5. YMODEM：单文件/批量回环、块 0 文件名净化、取消
//   6. ZMODEM：文件信息 + 数据帧 + 结束握手回环、非法参数、取消
//   7. Kermit：参数协商 + F/D/Z/B 回环、非法参数、取消
//
// 所有回环测试都用 setWriter()/onData() 对接两个协议引擎，数据经
// QTimer::singleShot(0) 排队投递，避免同步重入改变状态机时序。
// ============================================================================
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include "transfer/asciitransfer.h"
#include "transfer/filetransfer.h"
#include "transfer/kermittransfer.h"
#include "transfer/xmodemcodec.h"
#include "transfer/xmodemtransfer.h"
#include "transfer/ymodemtransfer.h"
#include "transfer/zmodemtransfer.h"

namespace {

constexpr char SOH = char(0x01);
constexpr char STX = char(0x02);
constexpr char EOT = char(0x04);
constexpr char ACK = char(0x06);
constexpr char NAK = char(0x15);
constexpr char CAN = char(0x18);
constexpr char CRC_REQUEST = 'C';

// 暴露受保护接口的探针，用于测试基类本身
class ProbeTransfer : public FileTransfer
{
public:
    ProbeTransfer(Kind kind, Direction direction, const QStringList &paths)
        : FileTransfer(kind, direction, paths)
    {
    }

    void start() override {}
    void cancel() override { finish(false, QStringLiteral("cancelled")); }
    void onData(const QByteArray &) override {}

    static QString cleanName(const QString &name) { return sanitizeName(name); }
    void pushWrite(const QByteArray &data) { write(data); }
    void pushStatus(const QString &text) { setStatus(text); }
    void pushProgress(qint64 transferred, qint64 total)
    {
        setProgress(transferred, total);
    }
    void reportSuccess(const QString &message) { finish(true, message); }
    void reportFailure(const QString &message) { finish(false, message); }
};

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(data), qint64(data.size()));
    file.close();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

// 把 from 写出的所有字节排队投递给 to；用 to 作为定时器上下文，
// 对象销毁后未投递的消息自动取消。
void linkEngines(FileTransfer *from, FileTransfer *to)
{
    from->setWriter([to](const QByteArray &data) {
        QTimer::singleShot(0, to, [to, data] { to->onData(data); });
    });
}

QByteArray patternBytes(int size)
{
    QByteArray data;
    data.reserve(size);
    for (int i = 0; i < size; ++i) {
        data.append(char(i & 0xFF));
    }
    return data;
}

void verifySuccess(const QSignalSpy &spy)
{
    QCOMPARE(spy.count(), 1);
    const bool success = spy.at(0).at(0).toBool();
    const QString message = spy.at(0).at(1).toString();
    QVERIFY2(success, qPrintable(message));
}

void verifyFailure(const QSignalSpy &spy)
{
    QCOMPARE(spy.count(), 1);
    const bool success = spy.at(0).at(0).toBool();
    const QString message = spy.at(0).at(1).toString();
    QVERIFY2(!success, qPrintable(message));
    QVERIFY(!message.isEmpty());
}

QByteArray ymodemBlock0(const QByteArray &payload)
{
    QByteArray block = payload;
    if (block.size() > 128) {
        block.truncate(128);
    }
    block.append(QByteArray(128 - block.size(), '\0'));
    return XmodemCodec::makeBlock(SOH, 0, block, true);
}

} // namespace

class TestTransfer : public QObject
{
    Q_OBJECT

private slots:
    void metadataAndDirections();
    void writerInjectionAndSignals();
    void finishOnlyOnce();

    void sanitizeName_data();
    void sanitizeName();

    void asciiSendConvertsLineEndingsAndChunks();
    void asciiReceiveCancelSavesFile();
    void asciiRejectsInvalidArguments();

    void xmodemLoopback();
    void xmodemEmptyFileLoopback();
    void xmodemChecksumFallbackAgainstScriptedPeer();
    void xmodemReceiverRecoversFromCorruptBlock();
    void xmodemCancelSendsCan();

    void ymodemLoopbackSingleFile();
    void ymodemLoopbackBatch();
    void ymodemReceiverSanitizesIncomingName();
    void ymodemCancelSendsCan();

    void zmodemLoopback();
    void zmodemRejectsInvalidArguments();
    void zmodemCancelSendsCan();

    void kermitLoopback();
    void kermitRejectsInvalidArguments();
    void kermitCancelSendsEot();
};

// ---------------------------------------------------------------------------
// 基类
// ---------------------------------------------------------------------------
void TestTransfer::metadataAndDirections()
{
    struct Entry {
        FileTransfer::Kind kind;
        const char *name;
    };
    const Entry entries[] = {
        {FileTransfer::Kind::Zmodem, "ZMODEM"},
        {FileTransfer::Kind::Ymodem, "YMODEM"},
        {FileTransfer::Kind::Xmodem, "XMODEM"},
        {FileTransfer::Kind::Kermit, "Kermit"},
        {FileTransfer::Kind::Ascii, "ASCII"},
    };

    const QStringList paths{QStringLiteral("C:/tmp/alpha.bin"),
                            QStringLiteral("beta.bin")};
    for (const Entry &entry : entries) {
        ProbeTransfer send(entry.kind, FileTransfer::Direction::Send, paths);
        QCOMPARE(send.protocolName(), QString::fromLatin1(entry.name));
        QCOMPARE(send.directionName(), QStringLiteral("发送"));
        QCOMPARE(send.paths(), paths);
        QCOMPARE(send.fileSummary(), QStringLiteral("alpha.bin, beta.bin"));
        QCOMPARE(send.transferred(), qint64(0));
        QCOMPARE(send.total(), qint64(0));
        QVERIFY(!send.isFinished());

        ProbeTransfer receive(entry.kind, FileTransfer::Direction::Receive,
                              QStringList{QStringLiteral("C:/tmp")});
        QCOMPARE(receive.directionName(), QStringLiteral("接收"));
        QCOMPARE(receive.fileSummary(), QStringLiteral("tmp"));
    }

    ProbeTransfer empty(FileTransfer::Kind::Ascii,
                        FileTransfer::Direction::Send, QStringList());
    QVERIFY(empty.fileSummary().isEmpty());
}

void TestTransfer::writerInjectionAndSignals()
{
    ProbeTransfer probe(FileTransfer::Kind::Ascii,
                        FileTransfer::Direction::Send,
                        QStringList{QStringLiteral("a.txt")});
    QSignalSpy progressSpy(&probe, &FileTransfer::progress);
    QSignalSpy statusSpy(&probe, &FileTransfer::statusChanged);
    QSignalSpy finishedSpy(&probe, &FileTransfer::finished);

    // 没有 writer 时写数据不得崩溃，也不能收到空写入
    probe.pushWrite(QByteArray("ignored"));

    QByteArray wire;
    probe.setWriter([&wire](const QByteArray &data) { wire += data; });
    probe.pushWrite(QByteArrayLiteral("hello"));
    probe.pushWrite(QByteArray());
    QCOMPARE(wire, QByteArrayLiteral("hello"));

    probe.pushProgress(5, 10);
    QCOMPARE(probe.transferred(), qint64(5));
    QCOMPARE(probe.total(), qint64(10));
    QCOMPARE(progressSpy.count(), 1);

    // total < 0 表示大小未知：保留上一次的总量
    probe.pushProgress(7, -1);
    QCOMPARE(probe.transferred(), qint64(7));
    QCOMPARE(probe.total(), qint64(10));
    QCOMPARE(progressSpy.count(), 2);

    probe.pushStatus(QStringLiteral("working"));
    probe.pushStatus(QStringLiteral("working"));
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(probe.statusText(), QStringLiteral("working"));

    probe.reportSuccess(QStringLiteral("done"));
    QVERIFY(probe.isFinished());
    QVERIFY(probe.errorString().isEmpty());
    verifySuccess(finishedSpy);

    // 结束后再次 finish 必须被忽略（成功/失败都不能覆盖第一次结果）
    probe.reportFailure(QStringLiteral("late error"));
    probe.reportSuccess(QStringLiteral("again"));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(probe.errorString().isEmpty());
}

void TestTransfer::finishOnlyOnce()
{
    ProbeTransfer probe(FileTransfer::Kind::Xmodem,
                        FileTransfer::Direction::Send, QStringList());
    QSignalSpy spy(&probe, &FileTransfer::finished);
    probe.reportFailure(QStringLiteral("boom"));
    probe.reportSuccess(QStringLiteral("ignored"));
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(0).toBool());
    QCOMPARE(probe.errorString(), QStringLiteral("boom"));
}

// ---------------------------------------------------------------------------
// 文件名净化
// ---------------------------------------------------------------------------
void TestTransfer::sanitizeName_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "report.txt" << "report.txt";
    QTest::newRow("posix path") << "dir/sub/report.txt" << "report.txt";
    QTest::newRow("windows path") << "C:\\Windows\\evil.txt" << "evil.txt";
    QTest::newRow("traversal") << "..\\..\\evil.txt" << "evil.txt";
    QTest::newRow("parent") << ".." << "";
    QTest::newRow("current") << "." << "";
    QTest::newRow("blank") << "   " << "";
    QTest::newRow("control and invalid")
        << "a\x01<>:\"|?*b.txt" << "ab.txt";
    QTest::newRow("unicode") << QString::fromUtf8("固件 v1.2.bin")
                             << QString::fromUtf8("固件 v1.2.bin");
}

void TestTransfer::sanitizeName()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(ProbeTransfer::cleanName(input), expected);
}

// ---------------------------------------------------------------------------
// ASCII
// ---------------------------------------------------------------------------
void TestTransfer::asciiSendConvertsLineEndingsAndChunks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray payload;
    for (int i = 0; i < 20; ++i) {
        payload += "line-";
        payload += QByteArray::number(i);
        payload += "\r\nunix-line\nold-mac\r";
    }
    QVERIFY(payload.size() > 512);   // 至少跨 3 个 256 字节分块

    const QString source = dir.filePath(QStringLiteral("mixed.txt"));
    writeFile(source, payload);

    QByteArray expected = payload;
    expected.replace("\r\n", "\n");
    expected.replace('\r', '\n');
    expected.replace("\n", "\r\n");

    AsciiTransfer sender(FileTransfer::Direction::Send,
                         QStringList{source});
    QByteArray wire;
    sender.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&sender, &FileTransfer::finished);
    sender.start();
    QVERIFY(!sender.isFinished());

    QTRY_VERIFY_WITH_TIMEOUT(sender.isFinished(), 15000);
    verifySuccess(spy);
    QCOMPARE(wire, expected);
    QCOMPARE(sender.transferred(), qint64(expected.size()));
    QCOMPARE(sender.total(), qint64(expected.size()));
    QCOMPARE(spy.at(0).at(1).toString(),
             QStringLiteral("ASCII 发送完成：%1 字节").arg(expected.size()));
}

void TestTransfer::asciiReceiveCancelSavesFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("capture.txt"));

    AsciiTransfer receiver(FileTransfer::Direction::Receive,
                           QStringList{target});
    QSignalSpy spy(&receiver, &FileTransfer::finished);
    QSignalSpy progressSpy(&receiver, &FileTransfer::progress);
    receiver.start();
    QVERIFY(!receiver.isFinished());

    receiver.onData(QByteArrayLiteral("first line\n"));
    receiver.onData(QByteArrayLiteral("second line\n"));
    QCOMPARE(receiver.transferred(), qint64(23));
    QVERIFY(!receiver.isFinished());
    QVERIFY(progressSpy.count() >= 2);

    // 文本接收没有结束标记，必须由用户停止并保存
    receiver.cancel();
    QVERIFY(receiver.isFinished());
    verifySuccess(spy);   // 接收方向的取消 = 正常收尾保存
    QCOMPARE(readFile(target),
             QByteArrayLiteral("first line\nsecond line\n"));

    // 结束后再来的数据必须丢弃
    receiver.onData(QByteArrayLiteral("late"));
    QCOMPARE(readFile(target),
             QByteArrayLiteral("first line\nsecond line\n"));
}

void TestTransfer::asciiRejectsInvalidArguments()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        AsciiTransfer sender(FileTransfer::Direction::Send, QStringList());
        QSignalSpy spy(&sender, &FileTransfer::finished);
        sender.start();
        verifyFailure(spy);
        QVERIFY(!sender.errorString().isEmpty());
    }
    {
        AsciiTransfer receiver(FileTransfer::Direction::Receive, QStringList());
        QSignalSpy spy(&receiver, &FileTransfer::finished);
        receiver.start();
        verifyFailure(spy);
    }
    {
        AsciiTransfer sender(FileTransfer::Direction::Send,
                             QStringList{dir.filePath(QStringLiteral("missing.txt"))});
        QSignalSpy spy(&sender, &FileTransfer::finished);
        sender.start();
        verifyFailure(spy);
    }
    {
        AsciiTransfer receiver(
            FileTransfer::Direction::Receive,
            QStringList{dir.filePath(QStringLiteral("no-such-dir/out.txt"))});
        QSignalSpy spy(&receiver, &FileTransfer::finished);
        receiver.start();
        verifyFailure(spy);
    }
}

// ---------------------------------------------------------------------------
// XMODEM
// ---------------------------------------------------------------------------
void TestTransfer::xmodemLoopback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray payload = patternBytes(1024);   // 8 个整块，无 SUB 填充
    const QString source = dir.filePath(QStringLiteral("payload.bin"));
    const QString target = dir.filePath(QStringLiteral("received.bin"));
    writeFile(source, payload);

    XmodemTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    XmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{target});
    linkEngines(&sender, &receiver);
    linkEngines(&receiver, &sender);

    QSignalSpy sendSpy(&sender, &FileTransfer::finished);
    QSignalSpy receiveSpy(&receiver, &FileTransfer::finished);

    receiver.start();
    sender.start();
    QTRY_VERIFY_WITH_TIMEOUT(sender.isFinished() && receiver.isFinished(), 20000);

    verifySuccess(sendSpy);
    verifySuccess(receiveSpy);
    QCOMPARE(readFile(target), payload);
    QCOMPARE(sender.transferred(), qint64(payload.size()));
    QCOMPARE(receiver.transferred(), qint64(payload.size()));
    QVERIFY(sender.errorString().isEmpty());
    QVERIFY(receiver.errorString().isEmpty());
}

void TestTransfer::xmodemEmptyFileLoopback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("empty.bin"));
    const QString target = dir.filePath(QStringLiteral("empty.out"));
    writeFile(source, QByteArray());

    XmodemTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    XmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{target});
    linkEngines(&sender, &receiver);
    linkEngines(&receiver, &sender);
    QSignalSpy sendSpy(&sender, &FileTransfer::finished);
    QSignalSpy receiveSpy(&receiver, &FileTransfer::finished);

    receiver.start();
    sender.start();
    QTRY_VERIFY_WITH_TIMEOUT(sender.isFinished() && receiver.isFinished(), 20000);

    verifySuccess(sendSpy);
    verifySuccess(receiveSpy);
    QVERIFY(QFile::exists(target));
    QCOMPARE(QFileInfo(target).size(), qint64(0));
    QCOMPARE(sender.transferred(), qint64(0));
}

void TestTransfer::xmodemChecksumFallbackAgainstScriptedPeer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray payload = patternBytes(100);   // 单块 + SUB 填充
    const QString source = dir.filePath(QStringLiteral("small.bin"));
    writeFile(source, payload);

    XmodemTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    QByteArray wire;
    sender.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&sender, &FileTransfer::finished);
    sender.start();

    // 接收方用 NAK 启动 => 退回 1 字节累加和校验
    sender.onData(QByteArray(1, NAK));
    QCOMPARE(wire.size(), 132);   // SOH + 序号 + 反码 + 128 + 1
    QCOMPARE(quint8(wire.at(0)), quint8(SOH));
    QCOMPARE(quint8(wire.at(1)), quint8(1));
    QCOMPARE(quint8(wire.at(2)), quint8(0xFE));
    QCOMPARE(quint8(wire.at(wire.size() - 1)),
             XmodemCodec::checksum8(payload + QByteArray(128 - payload.size(), char(0x1A))));
    QVERIFY(!sender.isFinished());

    sender.onData(QByteArray(1, ACK));   // 块已确认 => 发送 EOT
    QCOMPARE(wire.size(), 133);
    QCOMPARE(quint8(wire.at(wire.size() - 1)), quint8(EOT));
    QVERIFY(!sender.isFinished());

    sender.onData(QByteArray(1, ACK));   // EOT 已确认
    QVERIFY(sender.isFinished());
    verifySuccess(spy);
    QCOMPARE(sender.transferred(), qint64(100));
}

void TestTransfer::xmodemReceiverRecoversFromCorruptBlock()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("recovered.bin"));

    XmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{target});
    QByteArray wire;
    receiver.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&receiver, &FileTransfer::finished);
    receiver.start();
    QCOMPARE(wire, QByteArray(1, CRC_REQUEST));
    wire.clear();

    const QByteArray payload(128, 'A');
    QByteArray corrupt = XmodemCodec::makeBlock(SOH, 1, payload, true);
    corrupt[corrupt.size() - 2] = '\0';
    corrupt[corrupt.size() - 1] = '\0';
    receiver.onData(corrupt);
    QCOMPARE(wire, QByteArray(1, NAK));   // 坏块必须请求重发
    QVERIFY(!receiver.isFinished());
    wire.clear();

    // 发送方重发正确的块：接收方要能从坏块残留中重新同步。
    // 残留字节里可能出现伪造的块头，允许再次回 NAK，但最终必须确认重发的块。
    receiver.onData(XmodemCodec::makeBlock(SOH, 1, payload, true));
    QVERIFY2(wire.endsWith(QByteArray(1, ACK)), qPrintable(wire.toHex()));
    for (const char byte : wire) {
        QVERIFY(byte == ACK || byte == NAK);
    }
    wire.clear();

    receiver.onData(QByteArray(1, EOT));
    QVERIFY(receiver.isFinished());
    verifySuccess(spy);
    QCOMPARE(wire, QByteArray(1, ACK));
    QCOMPARE(readFile(target), payload);
}

void TestTransfer::xmodemCancelSendsCan()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("cancel.bin"));
    writeFile(source, patternBytes(300));

    XmodemTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    QByteArray wire;
    sender.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&sender, &FileTransfer::finished);
    sender.start();
    QVERIFY(wire.isEmpty());   // 等待接收方握手

    sender.cancel();
    QVERIFY(sender.isFinished());
    verifyFailure(spy);
    QCOMPARE(wire, QByteArray(8, CAN));
    QVERIFY(!sender.errorString().isEmpty());
}

// ---------------------------------------------------------------------------
// YMODEM
// ---------------------------------------------------------------------------
void TestTransfer::ymodemLoopbackSingleFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray payload = patternBytes(2000);
    const QString source = dir.filePath(QStringLiteral("single.bin"));
    const QString receiveDir = dir.filePath(QStringLiteral("recv"));
    QVERIFY(QDir().mkpath(receiveDir));
    writeFile(source, payload);

    YmodemTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    YmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{receiveDir});
    linkEngines(&sender, &receiver);
    linkEngines(&receiver, &sender);
    QSignalSpy sendSpy(&sender, &FileTransfer::finished);
    QSignalSpy receiveSpy(&receiver, &FileTransfer::finished);

    receiver.start();
    sender.start();
    QTRY_VERIFY_WITH_TIMEOUT(sender.isFinished() && receiver.isFinished(), 20000);

    verifySuccess(sendSpy);
    verifySuccess(receiveSpy);
    const QString target = QDir(receiveDir).filePath(QStringLiteral("single.bin"));
    QVERIFY(QFile::exists(target));
    QCOMPARE(readFile(target), payload);
    QCOMPARE(sender.transferred(), qint64(payload.size()));
    QCOMPARE(receiver.transferred(), qint64(payload.size()));
}

void TestTransfer::ymodemLoopbackBatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray first = patternBytes(2000);
    const QByteArray second = patternBytes(300);
    const QString firstSource = dir.filePath(QStringLiteral("alpha.bin"));
    const QString secondSource = dir.filePath(QStringLiteral("beta.bin"));
    const QString receiveDir = dir.filePath(QStringLiteral("batch"));
    QVERIFY(QDir().mkpath(receiveDir));
    writeFile(firstSource, first);
    writeFile(secondSource, second);

    YmodemTransfer sender(FileTransfer::Direction::Send,
                          QStringList{firstSource, secondSource});
    YmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{receiveDir});
    linkEngines(&sender, &receiver);
    linkEngines(&receiver, &sender);
    QSignalSpy sendSpy(&sender, &FileTransfer::finished);
    QSignalSpy receiveSpy(&receiver, &FileTransfer::finished);

    receiver.start();
    sender.start();
    QTRY_VERIFY_WITH_TIMEOUT(sender.isFinished() && receiver.isFinished(), 20000);

    verifySuccess(sendSpy);
    verifySuccess(receiveSpy);
    QCOMPARE(readFile(QDir(receiveDir).filePath(QStringLiteral("alpha.bin"))), first);
    QCOMPARE(readFile(QDir(receiveDir).filePath(QStringLiteral("beta.bin"))), second);
    // 批量结束：两个文件都计入完成消息
    QVERIFY(sendSpy.at(0).at(1).toString().contains(QStringLiteral("2 个文件")));
    QVERIFY(receiveSpy.at(0).at(1).toString().contains(QStringLiteral("2 个文件")));
}

void TestTransfer::ymodemReceiverSanitizesIncomingName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString receiveDir = dir.filePath(QStringLiteral("safe"));
    QVERIFY(QDir().mkpath(receiveDir));

    YmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{receiveDir});
    QByteArray wire;
    receiver.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&receiver, &FileTransfer::finished);
    receiver.start();
    QCOMPARE(wire, QByteArray(1, CRC_REQUEST));
    wire.clear();

    // 恶意块 0：文件名带路径穿越；声明大小 5 字节
    QByteArray info = QByteArrayLiteral("..\\..\\evil.txt");
    info.append('\0');
    info.append("5 0 100644 0");
    info.append('\0');
    receiver.onData(ymodemBlock0(info));
    QCOMPARE(wire, QByteArrayLiteral("\x06" "C"));
    wire.clear();

    QByteArray data = QByteArrayLiteral("hello");
    data.append(QByteArray(1024 - data.size(), char(0x1A)));
    receiver.onData(XmodemCodec::makeBlock(STX, 1, data, true));
    QCOMPARE(wire, QByteArray(1, ACK));
    wire.clear();

    receiver.onData(QByteArray(1, EOT));
    QCOMPARE(wire, QByteArrayLiteral("\x06" "C"));
    wire.clear();

    // 空文件名的块 0 = 批量结束
    receiver.onData(ymodemBlock0(QByteArray()));
    QVERIFY(receiver.isFinished());
    verifySuccess(spy);
    QCOMPARE(wire, QByteArray(1, ACK));

    const QString inside = QDir(receiveDir).filePath(QStringLiteral("evil.txt"));
    QVERIFY(QFile::exists(inside));
    QCOMPARE(readFile(inside), QByteArrayLiteral("hello"));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("evil.txt"))));
}

void TestTransfer::ymodemCancelSendsCan()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString receiveDir = dir.filePath(QStringLiteral("recv"));
    QVERIFY(QDir().mkpath(receiveDir));

    YmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{receiveDir});
    QByteArray wire;
    receiver.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&receiver, &FileTransfer::finished);
    receiver.start();
    QCOMPARE(wire, QByteArray(1, CRC_REQUEST));
    wire.clear();

    receiver.cancel();
    QVERIFY(receiver.isFinished());
    verifyFailure(spy);
    QCOMPARE(wire, QByteArray(8, CAN));
}

// ---------------------------------------------------------------------------
// ZMODEM
// ---------------------------------------------------------------------------
void TestTransfer::zmodemLoopback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray payload = patternBytes(5000);   // 跨多个数据帧
    const QString source = dir.filePath(QStringLiteral("zfile.bin"));
    const QString receiveDir = dir.filePath(QStringLiteral("zrecv"));
    QVERIFY(QDir().mkpath(receiveDir));
    writeFile(source, payload);

    ZmodemTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    ZmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{receiveDir});
    linkEngines(&sender, &receiver);
    linkEngines(&receiver, &sender);
    QSignalSpy sendSpy(&sender, &FileTransfer::finished);
    QSignalSpy receiveSpy(&receiver, &FileTransfer::finished);

    receiver.start();
    sender.start();
    QTRY_VERIFY_WITH_TIMEOUT(sender.isFinished() && receiver.isFinished(), 25000);

    verifySuccess(sendSpy);
    verifySuccess(receiveSpy);
    const QString target = QDir(receiveDir).filePath(QStringLiteral("zfile.bin"));
    QVERIFY(QFile::exists(target));
    QCOMPARE(readFile(target), payload);
    QCOMPARE(receiver.transferred(), qint64(payload.size()));
}

void TestTransfer::zmodemRejectsInvalidArguments()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        ZmodemTransfer sender(FileTransfer::Direction::Send,
                              QStringList{dir.filePath(QStringLiteral("missing.bin"))});
        QSignalSpy spy(&sender, &FileTransfer::finished);
        sender.start();
        verifyFailure(spy);
    }
    {
        ZmodemTransfer sender(FileTransfer::Direction::Send, QStringList());
        QSignalSpy spy(&sender, &FileTransfer::finished);
        sender.start();
        verifyFailure(spy);
    }
    {
        ZmodemTransfer receiver(
            FileTransfer::Direction::Receive,
            QStringList{dir.filePath(QStringLiteral("missing-dir/sub"))});
        QSignalSpy spy(&receiver, &FileTransfer::finished);
        receiver.start();
        verifyFailure(spy);
    }
}

void TestTransfer::zmodemCancelSendsCan()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString receiveDir = dir.filePath(QStringLiteral("recv"));
    QVERIFY(QDir().mkpath(receiveDir));

    ZmodemTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{receiveDir});
    QByteArray wire;
    receiver.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&receiver, &FileTransfer::finished);
    receiver.start();
    QVERIFY(!wire.isEmpty());   // ZRINIT
    wire.clear();

    receiver.cancel();
    QVERIFY(receiver.isFinished());
    verifyFailure(spy);
    QCOMPARE(wire, QByteArray(8, char(0x18)));
}

// ---------------------------------------------------------------------------
// Kermit
// ---------------------------------------------------------------------------
void TestTransfer::kermitLoopback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray payload = patternBytes(700);   // 含控制字符，触发引号编码
    const QString source = dir.filePath(QStringLiteral("kfile.bin"));
    const QString receiveDir = dir.filePath(QStringLiteral("krecv"));
    QVERIFY(QDir().mkpath(receiveDir));
    writeFile(source, payload);

    KermitTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    KermitTransfer receiver(FileTransfer::Direction::Receive,
                            QStringList{receiveDir});
    linkEngines(&sender, &receiver);
    linkEngines(&receiver, &sender);
    QSignalSpy sendSpy(&sender, &FileTransfer::finished);
    QSignalSpy receiveSpy(&receiver, &FileTransfer::finished);

    receiver.start();
    sender.start();
    QTRY_VERIFY_WITH_TIMEOUT(sender.isFinished() && receiver.isFinished(), 25000);

    verifySuccess(sendSpy);
    verifySuccess(receiveSpy);
    const QString target = QDir(receiveDir).filePath(QStringLiteral("kfile.bin"));
    QVERIFY(QFile::exists(target));
    QCOMPARE(readFile(target), payload);
    QCOMPARE(sender.transferred(), qint64(payload.size()));
    QCOMPARE(receiver.transferred(), qint64(payload.size()));
}

void TestTransfer::kermitRejectsInvalidArguments()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        KermitTransfer sender(FileTransfer::Direction::Send, QStringList());
        QSignalSpy spy(&sender, &FileTransfer::finished);
        sender.start();
        verifyFailure(spy);
    }
    {
        KermitTransfer sender(FileTransfer::Direction::Send,
                              QStringList{dir.filePath(QStringLiteral("missing.bin"))});
        QSignalSpy spy(&sender, &FileTransfer::finished);
        sender.start();
        verifyFailure(spy);
    }
    {
        KermitTransfer receiver(
            FileTransfer::Direction::Receive,
            QStringList{dir.filePath(QStringLiteral("missing-dir/sub"))});
        QSignalSpy spy(&receiver, &FileTransfer::finished);
        receiver.start();
        verifyFailure(spy);
    }
}

void TestTransfer::kermitCancelSendsEot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("cancel.bin"));
    writeFile(source, patternBytes(100));

    KermitTransfer sender(FileTransfer::Direction::Send,
                          QStringList{source});
    QByteArray wire;
    sender.setWriter([&wire](const QByteArray &data) { wire += data; });
    QSignalSpy spy(&sender, &FileTransfer::finished);
    sender.start();
    QVERIFY(!wire.isEmpty());   // 初始 S 包
    wire.clear();

    sender.cancel();
    QVERIFY(sender.isFinished());
    verifyFailure(spy);
    QVERIFY(!wire.isEmpty());
    QCOMPARE(quint8(wire.at(0)), quint8(0x01));       // MARK
    // 包体第二字节为类型，取消时发送 B（EOT）
    QVERIFY(wire.contains('B'));
}

QTEST_GUILESS_MAIN(TestTransfer)
#include "tst_transfer.moc"
