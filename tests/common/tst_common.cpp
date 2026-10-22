// ============================================================================
// tst_common —— src/common 公共契约测试
//
// 覆盖：
//   1. Ipc::encodeMessage / decodeHeader 二进制契约（大端 [4B 长度][1B 类型][载荷]）
//   2. Common::ConnectionSettings JSON 序列化往返与稳定字符串契约
//   3. 历史持久化格式（sessions.json 旧格式）的兼容读取
// ============================================================================
#include <QtTest>
#include <QJsonObject>
#include "common/messagetypes.h"
#include "common/connectionsettings.h"

using Ipc::MessageType;
using Ipc::MessageHeader;

class TestCommon : public QObject
{
    Q_OBJECT

private slots:
    void ipcEncodeLayout();
    void ipcDecodeHeader();
    void ipcDecodeHeaderTooShort();
    void settingsDefaults();
    void settingsTypeStrings();
    void settingsSerialEnums();
    void settingsJsonRoundTrip();
    void settingsJsonMissingFields();
    void settingsJsonLegacyFormat();
};

void TestCommon::ipcEncodeLayout()
{
    const QByteArray payload = "ABC";
    const QByteArray msg = Ipc::encodeMessage(MessageType::DataReceived, payload);

    // [4B 大端长度][1B 类型][载荷]
    QCOMPARE(msg.size(), Ipc::HEADER_SIZE + payload.size());
    QCOMPARE(static_cast<quint8>(msg[0]), 0x00);
    QCOMPARE(static_cast<quint8>(msg[1]), 0x00);
    QCOMPARE(static_cast<quint8>(msg[2]), 0x00);
    QCOMPARE(static_cast<quint8>(msg[3]), 0x03);
    QCOMPARE(static_cast<quint8>(msg[4]), static_cast<quint8>(MessageType::DataReceived));
    QCOMPARE(msg.mid(Ipc::HEADER_SIZE), payload);

    // 空载荷
    const QByteArray empty = Ipc::encodeMessage(MessageType::Shutdown, QByteArray());
    QCOMPARE(empty.size(), Ipc::HEADER_SIZE);
}

void TestCommon::ipcDecodeHeader()
{
    struct Row { MessageType type; const char *payload; int wireType; };
    const Row rows[] = {
        { MessageType::DataReceived, "x", 0x01 },
        { MessageType::DataToSend, "y", 0x02 },
        { MessageType::SessionConfig, "{}", 0x08 },
        { MessageType::Shutdown, "", 0xFF },
    };
    for (const Row &row : rows) {
        const QByteArray msg = Ipc::encodeMessage(row.type, row.payload);
        MessageHeader header;
        QVERIFY2(Ipc::decodeHeader(msg, header), row.payload);
        QCOMPARE(header.length, static_cast<quint32>(msg.size() - Ipc::HEADER_SIZE));
        QCOMPARE(static_cast<int>(header.type), row.wireType);
    }
}

void TestCommon::ipcDecodeHeaderTooShort()
{
    MessageHeader header;
    QVERIFY(!Ipc::decodeHeader(QByteArray("ab"), header));
    QVERIFY(!Ipc::decodeHeader(QByteArray(), header));
}

void TestCommon::settingsDefaults()
{
    Common::ConnectionSettings s;
    QCOMPARE(s.type, Common::ConnectionType::Serial);
    QCOMPARE(s.baudRate, 115200);
    QCOMPARE(s.dataBits, QSerialPort::Data8);
    QCOMPARE(s.parity, QSerialPort::NoParity);
    QCOMPARE(s.stopBits, QSerialPort::OneStop);
    QCOMPARE(s.flowControl, QSerialPort::NoFlowControl);
    QCOMPARE(s.port, 23);
    QCOMPARE(s.useSshKey, false);
    QVERIFY(s.sessionName.isEmpty());
}

void TestCommon::settingsTypeStrings()
{
    struct Row { Common::ConnectionType type; const char *name; };
    const Row rows[] = {
        { Common::ConnectionType::Serial, "Serial" },
        { Common::ConnectionType::Telnet, "Telnet" },
        { Common::ConnectionType::SSH, "SSH" },
        { Common::ConnectionType::Rlogin, "Rlogin" },
        { Common::ConnectionType::SFTP, "SFTP" },
        { Common::ConnectionType::FTP, "FTP" },
        { Common::ConnectionType::TFTP, "TFTP" },
    };
    for (const Row &row : rows) {
        Common::ConnectionSettings s;
        s.type = row.type;
        QCOMPARE(s.toJson()["type"].toString(), row.name);

        QJsonObject in;
        in["type"] = row.name;
        QCOMPARE(Common::ConnectionSettings::fromJson(in).type, row.type);
    }

    // 未知字符串回落到 Serial
    QJsonObject bad;
    bad["type"] = "NoSuchProtocol";
    QCOMPARE(Common::ConnectionSettings::fromJson(bad).type, Common::ConnectionType::Serial);
}

void TestCommon::settingsSerialEnums()
{
    // 现行契约：dataBits 为整数，parity/flowControl 用短名
    QJsonObject cur;
    cur["dataBits"] = 7;
    cur["parity"] = "Even";
    cur["stopBits"] = "2";
    cur["flowControl"] = "Hardware";
    Common::ConnectionSettings s = Common::ConnectionSettings::fromJson(cur);
    QCOMPARE(s.dataBits, QSerialPort::Data7);
    QCOMPARE(s.parity, QSerialPort::EvenParity);
    QCOMPARE(s.stopBits, QSerialPort::TwoStop);
    QCOMPARE(s.flowControl, QSerialPort::HardwareControl);
}

void TestCommon::settingsJsonRoundTrip()
{
    Common::ConnectionSettings s;
    s.sessionName = "router-console";
    s.type = Common::ConnectionType::SSH;
    s.portName = "COM3";
    s.baudRate = 57600;
    s.dataBits = QSerialPort::Data7;
    s.parity = QSerialPort::EvenParity;
    s.stopBits = QSerialPort::TwoStop;
    s.flowControl = QSerialPort::HardwareControl;
    s.host = "192.168.1.1";
    s.port = 2222;
    s.username = "admin";
    s.password = "secret";
    s.useSshKey = true;
    s.sshKeyPath = "C:/keys/id_rsa";

    const Common::ConnectionSettings r = Common::ConnectionSettings::fromJson(s.toJson());
    QCOMPARE(r.sessionName, s.sessionName);
    QCOMPARE(r.type, s.type);
    QCOMPARE(r.portName, s.portName);
    QCOMPARE(r.baudRate, s.baudRate);
    QCOMPARE(r.dataBits, s.dataBits);
    QCOMPARE(r.parity, s.parity);
    QCOMPARE(r.stopBits, s.stopBits);
    QCOMPARE(r.flowControl, s.flowControl);
    QCOMPARE(r.host, s.host);
    QCOMPARE(r.port, s.port);
    QCOMPARE(r.username, s.username);
    QCOMPARE(r.password, s.password);
    QCOMPARE(r.useSshKey, s.useSshKey);
    QCOMPARE(r.sshKeyPath, s.sshKeyPath);
}

void TestCommon::settingsJsonMissingFields()
{
    const Common::ConnectionSettings s = Common::ConnectionSettings::fromJson(QJsonObject());
    QCOMPARE(s.type, Common::ConnectionType::Serial);
    QCOMPARE(s.baudRate, 115200);
    QCOMPARE(s.dataBits, QSerialPort::Data8);
    QCOMPARE(s.port, 23);
    QCOMPARE(s.useSshKey, false);
}

// settingsJsonLegacyFormat：历史 sessions.json 的字段风格必须继续可读
// （dataBits 为字符串 "5".."8"，parity/flowControl 为 Qt 枚举名风格），
// 保证用户旧配置文件在重构后无损加载。
void TestCommon::settingsJsonLegacyFormat()
{
    QJsonObject legacy;
    legacy["dataBits"] = "8";
    legacy["parity"] = "EvenParity";
    legacy["flowControl"] = "HardwareControl";
    legacy["stopBits"] = "1";

    Common::ConnectionSettings s = Common::ConnectionSettings::fromJson(legacy);
    QCOMPARE(s.dataBits, QSerialPort::Data8);
    QCOMPARE(s.parity, QSerialPort::EvenParity);
    QCOMPARE(s.flowControl, QSerialPort::HardwareControl);
    QCOMPARE(s.stopBits, QSerialPort::OneStop);

    QJsonObject legacy2;
    legacy2["dataBits"] = "7";
    legacy2["parity"] = "OddParity";
    legacy2["flowControl"] = "SoftwareControl";
    Common::ConnectionSettings s2 = Common::ConnectionSettings::fromJson(legacy2);
    QCOMPARE(s2.dataBits, QSerialPort::Data7);
    QCOMPARE(s2.parity, QSerialPort::OddParity);
    QCOMPARE(s2.flowControl, QSerialPort::SoftwareControl);

    QJsonObject legacy3;
    legacy3["parity"] = "NoParity";
    legacy3["flowControl"] = "NoFlowControl";
    Common::ConnectionSettings s3 = Common::ConnectionSettings::fromJson(legacy3);
    QCOMPARE(s3.parity, QSerialPort::NoParity);
    QCOMPARE(s3.flowControl, QSerialPort::NoFlowControl);
}

QTEST_MAIN(TestCommon)
#include "tst_common.moc"
