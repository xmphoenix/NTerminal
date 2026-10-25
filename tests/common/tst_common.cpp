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
    void settingsSerialEnumMappings();
    void settingsNumericBoundaries();
    void settingsJsonContractShape();
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
    QVERIFY(s.rdpDomain.isEmpty());
    QCOMPARE(s.rdpUseNla, false);
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
        { Common::ConnectionType::SCP, "SCP" },
        { Common::ConnectionType::RDP, "RDP" },
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
    s.rdpDomain = "EXAMPLE";
    s.rdpUseNla = true;

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
    QCOMPARE(r.rdpDomain, s.rdpDomain);
    QCOMPARE(r.rdpUseNla, s.rdpUseNla);
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

// settingsSerialEnumMappings：逐项锁定串口枚举的持久化映射，
// 含现行短名、历史枚举名和非法值回落，防止契约静默漂移。
void TestCommon::settingsSerialEnumMappings()
{
    struct DataBitsRow { QJsonValue value; QSerialPort::DataBits expected; };
    const DataBitsRow dataBitsRows[] = {
        { 5, QSerialPort::Data5 },
        { 6, QSerialPort::Data6 },
        { 7, QSerialPort::Data7 },
        { 8, QSerialPort::Data8 },
        { 4, QSerialPort::Data8 },      // 非法值回落
        { 9, QSerialPort::Data8 },
        { QString("7"), QSerialPort::Data7 },   // 历史字符串格式
        { QString("8"), QSerialPort::Data8 },
        { QString("abc"), QSerialPort::Data8 },
        { QJsonValue::Undefined, QSerialPort::Data8 },
        { QJsonValue::Null, QSerialPort::Data8 },
    };
    for (const DataBitsRow &row : dataBitsRows) {
        QJsonObject in;
        in["dataBits"] = row.value;
        QCOMPARE(Common::ConnectionSettings::fromJson(in).dataBits, row.expected);
    }

    struct ParityRow { const char *text; QSerialPort::Parity expected; };
    const ParityRow parityRows[] = {
        { "None", QSerialPort::NoParity },
        { "Even", QSerialPort::EvenParity },
        { "Odd", QSerialPort::OddParity },
        { "Space", QSerialPort::SpaceParity },
        { "Mark", QSerialPort::MarkParity },
        { "NoParity", QSerialPort::NoParity },
        { "EvenParity", QSerialPort::EvenParity },
        { "OddParity", QSerialPort::OddParity },
        { "SpaceParity", QSerialPort::SpaceParity },
        { "MarkParity", QSerialPort::MarkParity },
        { "bogus", QSerialPort::NoParity },
    };
    for (const ParityRow &row : parityRows) {
        QJsonObject in;
        in["parity"] = row.text;
        QCOMPARE(Common::ConnectionSettings::fromJson(in).parity, row.expected);

        Common::ConnectionSettings out;
        out.parity = row.expected;
        // 现行输出始终使用短名
        const QString encoded = out.toJson()["parity"].toString();
        QVERIFY2(encoded == "None" || encoded == "Even" || encoded == "Odd"
                     || encoded == "Space" || encoded == "Mark",
                 qPrintable(encoded));
    }

    struct StopRow { const char *text; QSerialPort::StopBits expected; };
    const StopRow stopRows[] = {
        { "1", QSerialPort::OneStop },
        { "1.5", QSerialPort::OneAndHalfStop },
        { "2", QSerialPort::TwoStop },
        { "bogus", QSerialPort::OneStop },
    };
    for (const StopRow &row : stopRows) {
        QJsonObject in;
        in["stopBits"] = row.text;
        QCOMPARE(Common::ConnectionSettings::fromJson(in).stopBits, row.expected);
    }

    struct FlowRow { const char *text; QSerialPort::FlowControl expected; };
    const FlowRow flowRows[] = {
        { "None", QSerialPort::NoFlowControl },
        { "Hardware", QSerialPort::HardwareControl },
        { "Software", QSerialPort::SoftwareControl },
        { "NoFlowControl", QSerialPort::NoFlowControl },
        { "HardwareControl", QSerialPort::HardwareControl },
        { "SoftwareControl", QSerialPort::SoftwareControl },
        { "bogus", QSerialPort::NoFlowControl },
    };
    for (const FlowRow &row : flowRows) {
        QJsonObject in;
        in["flowControl"] = row.text;
        QCOMPARE(Common::ConnectionSettings::fromJson(in).flowControl, row.expected);
    }
}

// settingsNumericBoundaries：sessions.json 可被用户手工编辑，
// 非法端口必须回落而不是被截断成任意端口，非法波特率必须回落为默认值。
void TestCommon::settingsNumericBoundaries()
{
    struct PortRow { QJsonValue value; quint16 expected; };
    const PortRow portRows[] = {
        { 22, 22 },
        { 23, 23 },
        { 1, 1 },
        { 65535, 65535 },
        { 0, 23 },
        { -1, 23 },
        { 65536, 23 },
        { 70000, 23 },
        { QJsonValue::Undefined, 23 },
        { QJsonValue::Null, 23 },
    };
    for (const PortRow &row : portRows) {
        QJsonObject in;
        in["port"] = row.value;
        QCOMPARE(Common::ConnectionSettings::fromJson(in).port, row.expected);
    }

    struct BaudRow { QJsonValue value; qint32 expected; };
    const BaudRow baudRows[] = {
        { 9600, 9600 },
        { 115200, 115200 },
        { 921600, 921600 },
        { 0, 115200 },
        { -9600, 115200 },
        { QJsonValue::Undefined, 115200 },
    };
    for (const BaudRow &row : baudRows) {
        QJsonObject in;
        in["baudRate"] = row.value;
        QCOMPARE(Common::ConnectionSettings::fromJson(in).baudRate, row.expected);
    }
}

// settingsJsonContractShape：字段集合是跨进程/持久化契约，增删字段需同步测试。
void TestCommon::settingsJsonContractShape()
{
    Common::ConnectionSettings s;
    const QJsonObject json = s.toJson();
    const QStringList expectedKeys = {
        "sessionName", "type", "portName", "baudRate", "dataBits",
        "parity", "stopBits", "flowControl", "host", "port",
        "username", "password", "useSshKey", "sshKeyPath",
        "rdpDomain", "rdpUseNla",
    };
    QCOMPARE(json.keys().size(), expectedKeys.size());
    for (const QString &key : expectedKeys) {
        QVERIFY2(json.contains(key), qPrintable(key));
    }

    // 未知字段被忽略，不影响已知字段解析
    QJsonObject withUnknown;
    withUnknown["host"] = "example.test";
    withUnknown["futureField"] = 42;
    withUnknown["nested"] = QJsonObject{{"x", 1}};
    QCOMPARE(Common::ConnectionSettings::fromJson(withUnknown).host,
             QString("example.test"));

    // 非布尔类型不会被误当作 true
    QJsonObject badBools;
    badBools["useSshKey"] = "yes";
    badBools["rdpUseNla"] = 1;
    const Common::ConnectionSettings b = Common::ConnectionSettings::fromJson(badBools);
    QCOMPARE(b.useSshKey, false);
    QCOMPARE(b.rdpUseNla, false);
}

QTEST_MAIN(TestCommon)
#include "tst_common.moc"
