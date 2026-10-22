// ============================================================================
// Ipc::IpcProtocol 单元测试（本地管道消息编解码）
//
// 帧格式（大端序）：
//   [quint32 payload 长度][quint8 消息类型][payload]
//
// 覆盖：编码字节布局、解码往返、多帧粘包切分、分帧到达、空 payload、
//       JSON 重载、无 socket 时发送的安全性。
// ============================================================================

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "ipc/ipcprotocol.h"

using Ipc::MessageType;

class TestIpcProtocol : public QObject
{
    Q_OBJECT

private slots:
    // ---- 帧字节布局 ----
    void encodeProducesBigEndianHeader();
    void encodeEmptyPayload();

    // ---- 解码往返 ----
    void decodeRoundTrip();
    void decodeRoundTripJson();

    // ---- 粘包与分帧 ----
    void multipleMessagesInOneChunk();
    void messageSplitAcrossChunks();

    // ---- 安全性 ----
    void sendWithoutSocketIsSafe();
    void garbageDataDoesNotEmit();

private:
    static QByteArray frame(MessageType type, const QByteArray &payload);
};

QByteArray TestIpcProtocol::frame(MessageType type, const QByteArray &payload)
{
    return Ipc::IpcProtocol::encodeMessage(type, payload);
}

// ---- 帧字节布局 -------------------------------------------------------------

void TestIpcProtocol::encodeProducesBigEndianHeader()
{
    const QByteArray message = frame(MessageType::DataReceived, "AB");

    // 5 字节头 + 2 字节 payload
    QCOMPARE(message.size(), 7);
    // 大端序长度 = 0x00000002
    QCOMPARE(static_cast<quint8>(message.at(0)), 0);
    QCOMPARE(static_cast<quint8>(message.at(1)), 0);
    QCOMPARE(static_cast<quint8>(message.at(2)), 0);
    QCOMPARE(static_cast<quint8>(message.at(3)), 2);
    // 类型字节
    QCOMPARE(static_cast<quint8>(message.at(4)),
             static_cast<quint8>(MessageType::DataReceived));
    QCOMPARE(message.mid(5), QByteArray("AB"));
}

void TestIpcProtocol::encodeEmptyPayload()
{
    const QByteArray message = frame(MessageType::Shutdown, QByteArray());

    QCOMPARE(message.size(), 5);   // 仅头部
    QCOMPARE(static_cast<quint8>(message.at(3)), 0);
}

// ---- 解码往返 ---------------------------------------------------------------

void TestIpcProtocol::decodeRoundTrip()
{
    Ipc::IpcProtocol protocol;
    QSignalSpy spy(&protocol, &Ipc::IpcProtocol::messageReceived);

    const QByteArray payload = QByteArrayLiteral("\x1b[2J\x1b[Hdone");
    protocol.processData(frame(MessageType::DataReceived, payload));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<MessageType>(), MessageType::DataReceived);
    QCOMPARE(spy.at(0).at(1).toByteArray(), payload);
    QVERIFY(!protocol.hasCompleteMessage());   // 队列已取空
}

void TestIpcProtocol::decodeRoundTripJson()
{
    Ipc::IpcProtocol protocol;
    QSignalSpy spy(&protocol, &Ipc::IpcProtocol::messageReceived);

    QJsonObject config;
    config["type"] = QStringLiteral("SSH");
    config["port"] = 22;
    config["username"] = QStringLiteral("root");

    protocol.processData(Ipc::IpcProtocol::encodeMessage(
        MessageType::SessionConfig, config));

    QCOMPARE(spy.count(), 1);
    const QJsonObject received = QJsonDocument::fromJson(
        spy.at(0).at(1).toByteArray()).object();
    QCOMPARE(received.value("type").toString(), QStringLiteral("SSH"));
    QCOMPARE(received.value("port").toInt(), 22);
    QCOMPARE(received.value("username").toString(), QStringLiteral("root"));
}

// ---- 粘包与分帧 -------------------------------------------------------------

void TestIpcProtocol::multipleMessagesInOneChunk()
{
    Ipc::IpcProtocol protocol;
    QSignalSpy spy(&protocol, &Ipc::IpcProtocol::messageReceived);

    // 两帧拼接成一块到达（粘包），必须按帧边界切开
    const QByteArray twoFrames =
        frame(MessageType::DataReceived, "first") +
        frame(MessageType::ConnectionStatus, "second");

    protocol.processData(twoFrames);

    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).value<MessageType>(), MessageType::DataReceived);
    QCOMPARE(spy.at(0).at(1).toByteArray(), QByteArray("first"));
    QCOMPARE(spy.at(1).at(0).value<MessageType>(), MessageType::ConnectionStatus);
    QCOMPARE(spy.at(1).at(1).toByteArray(), QByteArray("second"));
}

void TestIpcProtocol::messageSplitAcrossChunks()
{
    Ipc::IpcProtocol protocol;
    QSignalSpy spy(&protocol, &Ipc::IpcProtocol::messageReceived);

    const QByteArray message = frame(MessageType::DataToSend, "payload-42");

    // 先到前半段（头部被截断）：不得触发
    protocol.processData(message.left(3));
    QCOMPARE(spy.count(), 0);
    QVERIFY(!protocol.hasCompleteMessage());

    // 再到剩余部分：触发一次完整消息
    protocol.processData(message.mid(3));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<MessageType>(), MessageType::DataToSend);
    QCOMPARE(spy.at(0).at(1).toByteArray(), QByteArray("payload-42"));
}

// ---- 安全性 -----------------------------------------------------------------

void TestIpcProtocol::sendWithoutSocketIsSafe()
{
    Ipc::IpcProtocol protocol;   // 未 setSocket
    // 不得崩溃，仅告警
    protocol.sendMessage(MessageType::DataToSend, QByteArray("x"));
    QVERIFY(true);
}

void TestIpcProtocol::garbageDataDoesNotEmit()
{
    Ipc::IpcProtocol protocol;
    QSignalSpy spy(&protocol, &Ipc::IpcProtocol::messageReceived);

    // 声明长度远超实际数据：不完整，不触发
    QByteArray bogus;
    bogus.append(char(0xFF));
    bogus.append(char(0xFF));
    bogus.append(char(0xFF));
    bogus.append(char(0xFF));
    bogus.append(char(0x01));
    bogus.append("short");

    protocol.processData(bogus);
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestIpcProtocol)
#include "tst_ipcprotocol.moc"
