// ============================================================================
// tst_services —— 本机网络服务测试
//
// 覆盖：
//   1. ServiceUtil：根目录规范化、越界判定、安全路径拼接、安全文件名
//   2. ServerService：启动/停止、配置合并与持久化、日志上限、运行异常
//   3. ServerManager：服务注册表单例与固定顺序
//   4. HTTP：GET/HEAD、Content-Type/Length、404/405/403、目录浏览、
//      index.html 优先、百分号解码、路径穿越
//   5. FTP：登录/匿名/拒绝、目录切换、PASV/EPSV/PORT、LIST/NLST、
//      RETR/STOR/APPE、MKD/RMD/RNFR/RNTO/DELE、只读模式、路径穿越
//   6. TFTP：默认块/多块下载、blksize+tsize 协商、上传、错误码、非法请求
//   7. Telnet：IAC 问候、命令执行与回显、cd、exit
//
// 服务与客户端在同一进程内，等待网络响应时用 QTest::qWait 驱动事件循环，
// 不能用阻塞的 waitForReadyRead，否则服务端槽函数没有机会运行。
// ============================================================================
#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMap>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <functional>
#include "services/ftpserver.h"
#include "services/httpserver.h"
#include "services/servermanager.h"
#include "services/serviceutil.h"
#include "services/serverservice.h"
#include "services/telnetserver.h"
#include "services/tftpserver.h"

namespace {

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QTest::qWait(5);
    }
    return condition();
}

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

quint16 freeTcpPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::Any, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

quint16 freeUdpPort()
{
    QUdpSocket probe;
    if (!probe.bind(QHostAddress::AnyIPv4, 0)) {
        return 0;
    }
    const quint16 port = probe.localPort();
    probe.close();
    return port;
}

// ---------------------------------------------------------------------------
// HTTP 客户端
// ---------------------------------------------------------------------------
struct HttpResponse {
    int status = 0;
    QByteArray headers;
    QByteArray body;

    QByteArray header(const QByteArray &name) const
    {
        for (const QByteArray &line : headers.split('\n')) {
            const int colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0) {
                return line.mid(colon + 1).trimmed();
            }
        }
        return QByteArray();
    }
};

HttpResponse httpRequest(quint16 port, const QByteArray &request, bool *completed = nullptr)
{
    HttpResponse response;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!waitUntil([&socket] {
            return socket.state() == QAbstractSocket::ConnectedState
                   || socket.state() == QAbstractSocket::UnconnectedState;
        })) {
        if (completed) {
            *completed = false;
        }
        return response;
    }
    if (socket.state() != QAbstractSocket::ConnectedState) {
        if (completed) {
            *completed = false;
        }
        return response;
    }

    socket.write(request);
    QByteArray raw;
    const bool closed = waitUntil([&] {
        raw += socket.readAll();
        return socket.state() == QAbstractSocket::UnconnectedState;
    }, 8000);
    raw += socket.readAll();
    if (completed) {
        *completed = closed;
    }

    const int split = raw.indexOf("\r\n\r\n");
    const QByteArray head = split >= 0 ? raw.left(split) : raw;
    response.headers = head;
    response.body = split >= 0 ? raw.mid(split + 4) : QByteArray();
    const QList<QByteArray> statusLine = head.left(head.indexOf('\n')).trimmed().split(' ');
    if (statusLine.size() >= 2) {
        response.status = statusLine.at(1).toInt();
    }
    return response;
}

HttpResponse httpGet(quint16 port, const QByteArray &path, bool head = false,
                     bool *completed = nullptr)
{
    QByteArray request = head ? "HEAD " : "GET ";
    request += path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    return httpRequest(port, request, completed);
}

// ---------------------------------------------------------------------------
// FTP 客户端
// ---------------------------------------------------------------------------
class FtpClient
{
public:
    bool connectTo(quint16 port, QString *error = nullptr)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        if (!waitUntil([this] {
                return m_socket.state() == QAbstractSocket::ConnectedState
                       || m_socket.state() == QAbstractSocket::UnconnectedState;
            })) {
            if (error) {
                *error = m_socket.errorString();
            }
            return false;
        }
        if (m_socket.state() != QAbstractSocket::ConnectedState) {
            if (error) {
                *error = m_socket.errorString();
            }
            return false;
        }
        const QString greeting = readReply();
        if (!greeting.startsWith(QLatin1String("220"))) {
            if (error) {
                *error = greeting;
            }
            return false;
        }
        return true;
    }

    QString command(const QString &line, int timeoutMs = 5000)
    {
        m_socket.write(line.toUtf8() + "\r\n");
        return readReply(timeoutMs);
    }

    QString readReply(int timeoutMs = 5000)
    {
        QStringList lines;
        QElapsedTimer timer;
        timer.start();
        bool peerClosed = false;
        while (timer.elapsed() < timeoutMs) {
            for (;;) {
                const int newline = m_buffer.indexOf("\r\n");
                if (newline < 0) {
                    break;
                }
                const QByteArray raw = m_buffer.left(newline);
                m_buffer.remove(0, newline + 2);
                lines.append(QString::fromUtf8(raw));
                if (raw.size() >= 4 && raw.at(0) >= '0' && raw.at(0) <= '9'
                    && raw.at(1) >= '0' && raw.at(1) <= '9'
                    && raw.at(2) >= '0' && raw.at(2) <= '9') {
                    if (raw.at(3) == '-') {
                        continue;   // 多行应答继续读
                    }
                    if (raw.at(3) == ' ') {
                        return lines.join(QLatin1Char('\n'));
                    }
                }
            }
            if (peerClosed) {
                break;
            }
            if (m_socket.state() == QAbstractSocket::UnconnectedState) {
                // 服务器可能发完应答立刻断开：先取回剩余字节，再解析一次
                m_buffer += m_socket.readAll();
                peerClosed = true;
                continue;
            }
            m_buffer += m_socket.readAll();
            QTest::qWait(5);
        }
        return lines.join(QLatin1Char('\n'));
    }

    QTcpSocket &socket() { return m_socket; }
    bool isConnected() const
    {
        return m_socket.state() == QAbstractSocket::ConnectedState;
    }

private:
    QTcpSocket m_socket;
    QByteArray m_buffer;
};

bool parsePasvPort(const QString &reply, quint16 *port)
{
    static const QRegularExpression numbers(
        QStringLiteral("(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)"));
    const QRegularExpressionMatch match = numbers.match(reply);
    if (!match.hasMatch()) {
        return false;
    }
    *port = quint16(match.captured(5).toInt() * 256 + match.captured(6).toInt());
    return true;
}

bool openPassiveData(FtpClient &client, QTcpSocket &data, quint16 *port,
                     QString *error)
{
    const QString reply = client.command(QStringLiteral("PASV"));
    if (!parsePasvPort(reply, port)) {
        *error = reply;
        return false;
    }
    data.connectToHost(QHostAddress::LocalHost, *port);
    if (!waitUntil([&data] {
            return data.state() == QAbstractSocket::ConnectedState
                   || data.state() == QAbstractSocket::UnconnectedState;
        })) {
        *error = data.errorString();
        return false;
    }
    if (data.state() != QAbstractSocket::ConnectedState) {
        *error = data.errorString();
        return false;
    }
    return true;
}

bool readUntilClosed(QTcpSocket &socket, QByteArray *out, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        *out += socket.readAll();
        if (socket.state() == QAbstractSocket::UnconnectedState) {
            *out += socket.readAll();
            return true;
        }
        QTest::qWait(5);
    }
    *out += socket.readAll();
    return socket.state() == QAbstractSocket::UnconnectedState;
}

bool ftpDownload(FtpClient &client, const QString &command, QByteArray *payload,
                 QString *finalReply)
{
    QTcpSocket data;
    quint16 port = 0;
    QString error;
    if (!openPassiveData(client, data, &port, &error)) {
        *finalReply = error;
        return false;
    }
    const QString started = client.command(command);
    if (!started.startsWith(QLatin1String("150"))) {
        *finalReply = started;
        return false;
    }
    if (!readUntilClosed(data, payload)) {
        *finalReply = QStringLiteral("data connection did not close");
        return false;
    }
    *finalReply = client.readReply(8000);
    return finalReply->startsWith(QLatin1String("226"));
}

bool ftpUpload(FtpClient &client, const QString &command, const QByteArray &payload,
               QString *finalReply)
{
    QTcpSocket data;
    quint16 port = 0;
    QString error;
    if (!openPassiveData(client, data, &port, &error)) {
        *finalReply = error;
        return false;
    }
    const QString started = client.command(command);
    if (!started.startsWith(QLatin1String("150"))) {
        *finalReply = started;
        return false;
    }
    data.write(payload);
    data.disconnectFromHost();
    waitUntil([&data] {
        return data.state() == QAbstractSocket::UnconnectedState;
    }, 5000);
    *finalReply = client.readReply(8000);
    return finalReply->startsWith(QLatin1String("226"));
}

// ---------------------------------------------------------------------------
// TFTP 客户端
// ---------------------------------------------------------------------------
void appendU16(QByteArray &data, quint16 value)
{
    data.append(char(value >> 8));
    data.append(char(value & 0xFF));
}

quint16 readU16(const QByteArray &data)
{
    return quint16((quint8(data.at(0)) << 8) | quint8(data.at(1)));
}

QByteArray tftpRequest(quint16 opcode, const QString &name,
                       const QMap<QString, QString> &options = {})
{
    QByteArray packet;
    appendU16(packet, opcode);
    packet += name.toUtf8();
    packet.append('\0');
    packet += "octet";
    packet.append('\0');
    for (auto it = options.begin(); it != options.end(); ++it) {
        packet += it.key().toUtf8();
        packet.append('\0');
        packet += it.value().toUtf8();
        packet.append('\0');
    }
    return packet;
}

QByteArray tftpAck(quint16 block)
{
    QByteArray packet;
    appendU16(packet, 4);
    appendU16(packet, block);
    return packet;
}

QByteArray tftpData(quint16 block, const QByteArray &payload)
{
    QByteArray packet;
    appendU16(packet, 3);
    appendU16(packet, block);
    packet += payload;
    return packet;
}

class TftpClient
{
public:
    bool bind()
    {
        return m_socket.bind(QHostAddress::LocalHost, 0);
    }

    bool send(const QByteArray &packet, quint16 port)
    {
        return m_socket.writeDatagram(packet, QHostAddress::LocalHost, port)
               == packet.size();
    }

    QByteArray receive(int timeoutMs = 5000)
    {
        QByteArray result;
        waitUntil([&] {
            if (!m_socket.hasPendingDatagrams()) {
                return false;
            }
            const QNetworkDatagram datagram = m_socket.receiveDatagram();
            result = datagram.data();
            m_peerPort = datagram.senderPort();
            return true;
        }, timeoutMs);
        return result;
    }

    quint16 peerPort() const { return m_peerPort; }

private:
    QUdpSocket m_socket;
    quint16 m_peerPort = 0;
};

struct TftpTransferResult {
    bool ok = false;
    QByteArray data;
    QByteArray oack;
    QString error;
    quint16 errorCode = 0;
    int blockSize = 512;
};

QMap<QString, QString> parseTftpOptions(const QByteArray &packet, int offset)
{
    QMap<QString, QString> options;
    const QList<QByteArray> fields = packet.mid(offset).split('\0');
    for (int i = 0; i + 1 < fields.size(); i += 2) {
        if (fields.at(i).isEmpty()) {
            break;
        }
        options.insert(QString::fromLatin1(fields.at(i)).toLower(),
                       QString::fromLatin1(fields.at(i + 1)));
    }
    return options;
}

void applyTftpError(const QByteArray &packet, TftpTransferResult *result)
{
    result->errorCode = packet.size() >= 4 ? readU16(packet.mid(2)) : 0;
    result->error = QString::fromUtf8(packet.mid(4)).trimmed();
}

TftpTransferResult tftpDownload(quint16 serverPort, const QString &name,
                                const QMap<QString, QString> &options = {})
{
    TftpTransferResult result;
    TftpClient client;
    if (!client.bind()) {
        result.error = QStringLiteral("无法绑定客户端 UDP 端口");
        return result;
    }
    result.blockSize = options.value(QStringLiteral("blksize")).toInt();
    if (result.blockSize <= 0) {
        result.blockSize = 512;
    }
    client.send(tftpRequest(1, name, options), serverPort);
    QByteArray packet = client.receive();
    if (packet.isEmpty()) {
        result.error = QStringLiteral("没有收到响应");
        return result;
    }
    if (readU16(packet) == 5) {
        applyTftpError(packet, &result);
        return result;
    }
    if (readU16(packet) == 6) {
        result.oack = packet;
        const QMap<QString, QString> negotiated = parseTftpOptions(packet, 2);
        const int negotiatedSize = negotiated.value(QStringLiteral("blksize")).toInt();
        if (negotiatedSize > 0) {
            result.blockSize = negotiatedSize;
        }
        client.send(tftpAck(0), client.peerPort());
        packet = client.receive();
    }

    quint16 expected = 1;
    while (!packet.isEmpty()) {
        const quint16 opcode = readU16(packet);
        if (opcode == 5) {
            applyTftpError(packet, &result);
            return result;
        }
        if (opcode != 3 || packet.size() < 4) {
            result.error = QStringLiteral("意外的数据包");
            return result;
        }
        const quint16 block = readU16(packet.mid(2));
        if (block != expected) {
            result.error = QStringLiteral("块号错误：%1（期望 %2）").arg(block).arg(expected);
            return result;
        }
        const QByteArray payload = packet.mid(4);
        result.data += payload;
        client.send(tftpAck(block), client.peerPort());
        if (payload.size() < result.blockSize) {
            result.ok = true;
            return result;
        }
        ++expected;
        packet = client.receive();
    }
    result.error = QStringLiteral("传输未完成");
    return result;
}

TftpTransferResult tftpUpload(quint16 serverPort, const QString &name,
                              const QByteArray &payload,
                              const QMap<QString, QString> &options = {})
{
    TftpTransferResult result;
    TftpClient client;
    if (!client.bind()) {
        result.error = QStringLiteral("无法绑定客户端 UDP 端口");
        return result;
    }
    result.blockSize = options.value(QStringLiteral("blksize")).toInt();
    if (result.blockSize <= 0) {
        result.blockSize = 512;
    }
    client.send(tftpRequest(2, name, options), serverPort);
    QByteArray packet = client.receive();
    if (packet.isEmpty()) {
        result.error = QStringLiteral("没有收到响应");
        return result;
    }
    const quint16 firstOpcode = readU16(packet);
    if (firstOpcode == 5) {
        applyTftpError(packet, &result);
        return result;
    }
    if (firstOpcode == 6) {
        result.oack = packet;
        const int negotiated = parseTftpOptions(packet, 2)
                                   .value(QStringLiteral("blksize"))
                                   .toInt();
        if (negotiated > 0) {
            result.blockSize = negotiated;
        }
    } else if (firstOpcode != 4 || readU16(packet.mid(2)) != 0) {
        result.error = QStringLiteral("意料之外的应答");
        return result;
    }

    quint16 block = 1;
    int offset = 0;
    while (offset < payload.size()) {
        const QByteArray chunk = payload.mid(offset, result.blockSize);
        client.send(tftpData(block, chunk), client.peerPort());
        offset += chunk.size();
        QByteArray ack = client.receive();
        if (ack.isEmpty()) {
            result.error = QStringLiteral("等待 ACK 超时");
            return result;
        }
        if (readU16(ack) == 5) {
            applyTftpError(ack, &result);
            return result;
        }
        if (readU16(ack) != 4 || readU16(ack.mid(2)) != block) {
            result.error = QStringLiteral("ACK 块号不匹配");
            return result;
        }
        ++block;
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// 测试用 ServerService
// ---------------------------------------------------------------------------
class FakeService : public ServerService
{
    Q_OBJECT

public:
    explicit FakeService(bool failWithoutError = false, QObject *parent = nullptr)
        : ServerService(parent)
        , m_failWithoutError(failWithoutError)
    {
    }

    QString id() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    QString description() const override { return QStringLiteral("测试服务"); }
    int defaultPort() const override { return 45999; }

    int startCalls = 0;
    int stopCalls = 0;

    void emitLog(const QString &text) { log(text); }
    void triggerRuntimeFailure(const QString &text) { failAndStop(text); }

protected:
    QJsonObject defaultConfig() const override
    {
        return QJsonObject{{QStringLiteral("port"), defaultPort()},
                           {QStringLiteral("root"), QStringLiteral("C:/fake-root")},
                           {QStringLiteral("allowWrite"), false}};
    }

    bool startServer() override
    {
        ++startCalls;
        if (m_failWithoutError) {
            return false;
        }
        log(QStringLiteral("fake started"));
        return true;
    }

    void stopServer() override { ++stopCalls; }

private:
    bool m_failWithoutError = false;
};

} // namespace

class TestServices : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void canonicalRoot_data();
    void canonicalRoot();
    void pathInside();
    void safeJoin();
    void safeName_data();
    void safeName();

    void serverServiceLifecycle();
    void serverServiceStartFailure();
    void serverServiceRuntimeFailure();
    void serverServiceLogLimit();
    void serverServiceConfigPersistence();
    void serverManagerRegistry();

    void httpFilesAndMethods();
    void httpDirectoryAndIndex();
    void httpDirectoryListingDisabled();
    void httpStartFailures();

    void ftpLoginAndSession();
    void ftpDirectoryAndDownloads();
    void ftpUploadAndFileManagement();
    void ftpActiveModeDownload();
    void ftpReadOnlyAndTraversal();
    void ftpAnonymousAndUnauthenticated();

    void tftpDownloadDefaultBlocks();
    void tftpDownloadWithOptions();
    void tftpDownloadErrors();
    void tftpUploadDenied();
    void tftpUploadWithOptions();
    void tftpRejectsInvalidRequests();

    void telnetGreetingAndCommand();
    void telnetCdAndExit();
};

void TestServices::initTestCase()
{
    // 隔离 QSettings：服务的配置持久化测试不能读写用户真实配置
    QCoreApplication::setOrganizationName(QStringLiteral("NTerminalTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_services"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings().clear();
}

void TestServices::cleanupTestCase()
{
    QSettings().clear();
}

// ---------------------------------------------------------------------------
// ServiceUtil
// ---------------------------------------------------------------------------
void TestServices::canonicalRoot_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("valid");
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("blank") << QStringLiteral("   ") << false;
    QTest::newRow("missing") << QStringLiteral("C:/no/such/dir/for-test") << false;
    QTest::newRow("file") << QStringLiteral("placeholder") << false;
    QTest::newRow("directory") << QStringLiteral("placeholder-dir") << true;
}

void TestServices::canonicalRoot()
{
    QFETCH(QString, input);
    QFETCH(bool, valid);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    if (input == QLatin1String("placeholder")) {
        input = dir.filePath(QStringLiteral("file.txt"));
        writeFile(input, "x");
    } else if (input == QLatin1String("placeholder-dir")) {
        input = dir.path();
    }
    if (valid) {
        QVERIFY(!ServiceUtil::canonicalRoot(input).isEmpty());
    } else {
        QVERIFY(ServiceUtil::canonicalRoot(input).isEmpty());
    }

    // 有效目录：返回规范化的绝对路径
    const QString root = ServiceUtil::canonicalRoot(dir.path());
    QVERIFY(!root.isEmpty());
    QCOMPARE(root, QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()));
}

void TestServices::pathInside()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = ServiceUtil::canonicalRoot(dir.path());
    QVERIFY(!root.isEmpty());

    QVERIFY(ServiceUtil::pathInside(root, root));
    QVERIFY(ServiceUtil::pathInside(root, root + QStringLiteral("/child")));
    QVERIFY(ServiceUtil::pathInside(root, root + QStringLiteral("/a/b/c")));
    QVERIFY(!ServiceUtil::pathInside(root, root + QStringLiteral("2/child")));
    QVERIFY(!ServiceUtil::pathInside(root, QDir::cleanPath(root + QStringLiteral("/../outside"))));
    QVERIFY(!ServiceUtil::pathInside(QString(), root));
    // 末尾斜杠不影响判定
    QVERIFY(ServiceUtil::pathInside(root + QStringLiteral("/"),
                                    root + QStringLiteral("/child")));
}

void TestServices::safeJoin()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = ServiceUtil::canonicalRoot(dir.path());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub"))));
    writeFile(dir.filePath(QStringLiteral("file.txt")), "data");

    QVERIFY(ServiceUtil::safeJoin(QString(), QStringLiteral("file.txt")).isEmpty());
    QCOMPARE(ServiceUtil::safeJoin(root, QString()), root);
    QCOMPARE(ServiceUtil::safeJoin(root, QStringLiteral("/")), root);
    QCOMPARE(ServiceUtil::safeJoin(root, QStringLiteral("file.txt")),
             QDir(root).filePath(QStringLiteral("file.txt")));
    // safeJoin 不做路径归一化：任何 ".." 组件都直接拒绝
    QVERIFY(ServiceUtil::safeJoin(root, QStringLiteral("sub/../file.txt")).isEmpty());
    QCOMPARE(ServiceUtil::safeJoin(root, QStringLiteral("\\sub\\")),
             QDir(root).filePath(QStringLiteral("sub")));

    // allowMissing=false 要求目标存在，上传时才允许不存在
    QVERIFY(ServiceUtil::safeJoin(root, QStringLiteral("missing.txt")).isEmpty());
    QCOMPARE(ServiceUtil::safeJoin(root, QStringLiteral("missing.txt"), true),
             QDir(root).filePath(QStringLiteral("missing.txt")));

    // 越界与盘符/数据流一律拒绝
    QVERIFY(ServiceUtil::safeJoin(root, QStringLiteral("../outside.txt"), true).isEmpty());
    QVERIFY(ServiceUtil::safeJoin(root, QStringLiteral("sub/../../outside.txt"), true).isEmpty());
    QVERIFY(ServiceUtil::safeJoin(root, QStringLiteral("..\\outside.txt"), true).isEmpty());
    QVERIFY(ServiceUtil::safeJoin(root, QStringLiteral("C:/Windows/win.ini"), true).isEmpty());
    QVERIFY(ServiceUtil::safeJoin(root, QStringLiteral("file.txt:stream"), true).isEmpty());
}

void TestServices::safeName_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("safe");
    QTest::newRow("plain") << "file.txt" << true;
    QTest::newRow("unicode") << QString::fromUtf8("固件.bin") << true;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("dot") << "." << false;
    QTest::newRow("dotdot") << ".." << false;
    QTest::newRow("slash") << "a/b" << false;
    QTest::newRow("backslash") << "a\\b" << false;
    QTest::newRow("colon") << "c:file" << false;
}

void TestServices::safeName()
{
    QFETCH(QString, name);
    QFETCH(bool, safe);
    QCOMPARE(ServiceUtil::safeName(name), safe);
}

// ---------------------------------------------------------------------------
// ServerService / ServerManager
// ---------------------------------------------------------------------------
void TestServices::serverServiceLifecycle()
{
    FakeService service;
    QSignalSpy runningSpy(&service, &ServerService::runningChanged);
    QSignalSpy errorSpy(&service, &ServerService::errorOccurred);

    QVERIFY(!service.isRunning());
    QVERIFY(service.start(QJsonObject{{QStringLiteral("port"), 46001}}));
    QVERIFY(service.isRunning());
    QCOMPARE(service.startCalls, 1);
    QCOMPARE(runningSpy.count(), 1);
    QCOMPARE(runningSpy.at(0).at(0).toBool(), true);
    QCOMPARE(service.port(), 46001);
    QCOMPARE(service.config().value(QStringLiteral("root")).toString(),
             QStringLiteral("C:/fake-root"));

    // 运行中重复启动是空操作：不重新调用 startServer，不覆盖配置
    QVERIFY(service.start(QJsonObject{{QStringLiteral("port"), 46002}}));
    QCOMPARE(service.startCalls, 1);
    QCOMPARE(service.port(), 46001);
    QCOMPARE(runningSpy.count(), 1);

    service.stop();
    QVERIFY(!service.isRunning());
    QCOMPARE(service.stopCalls, 1);
    QCOMPARE(runningSpy.count(), 2);
    QCOMPARE(runningSpy.at(1).at(0).toBool(), false);
    QVERIFY(errorSpy.isEmpty());

    // 未运行时 stop 是空操作
    service.stop();
    QCOMPARE(service.stopCalls, 1);
    QCOMPARE(runningSpy.count(), 2);

    // 停止后可再次启动
    QVERIFY(service.start(QJsonObject{{QStringLiteral("port"), 46003}}));
    QCOMPARE(service.startCalls, 2);
    QCOMPARE(service.port(), 46003);
    service.stop();
}

void TestServices::serverServiceStartFailure()
{
    FakeService service(true);
    QSignalSpy runningSpy(&service, &ServerService::runningChanged);
    QSignalSpy errorSpy(&service, &ServerService::errorOccurred);

    QVERIFY(!service.start(QJsonObject()));
    QVERIFY(!service.isRunning());
    QCOMPARE(runningSpy.count(), 0);
    QCOMPARE(service.startCalls, 1);
    // startServer 未设置错误时使用通用提示，并记录日志
    QCOMPARE(service.errorString(), QStringLiteral("服务启动失败"));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(service.recentLog().join(QLatin1Char('\n')).contains(QStringLiteral("启动失败")));

    // 失败后仍可重试
    QVERIFY(!service.start(QJsonObject()));
    QCOMPARE(service.startCalls, 2);
    QCOMPARE(errorSpy.count(), 2);
}

void TestServices::serverServiceRuntimeFailure()
{
    FakeService service;
    QSignalSpy runningSpy(&service, &ServerService::runningChanged);
    QSignalSpy errorSpy(&service, &ServerService::errorOccurred);
    QVERIFY(service.start(QJsonObject()));

    service.triggerRuntimeFailure(QStringLiteral("端口异常"));
    QVERIFY(!service.isRunning());
    QCOMPARE(service.stopCalls, 1);
    QCOMPARE(service.errorString(), QStringLiteral("端口异常"));
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(runningSpy.count(), 2);

    // 已停止时再报异常：只记录错误，不再停止/发运行状态信号
    service.triggerRuntimeFailure(QStringLiteral("延迟错误"));
    QCOMPARE(service.stopCalls, 1);
    QCOMPARE(errorSpy.count(), 2);
    QCOMPARE(runningSpy.count(), 2);
}

void TestServices::serverServiceLogLimit()
{
    FakeService service;
    QSignalSpy logSpy(&service, &ServerService::logMessage);
    for (int i = 0; i < 250; ++i) {
        service.emitLog(QStringLiteral("log-%1").arg(i));
    }
    QCOMPARE(logSpy.count(), 250);
    QCOMPARE(service.recentLog().size(), 200);
    QVERIFY(service.recentLog().first().contains(QStringLiteral("log-50")));
    QVERIFY(service.recentLog().last().contains(QStringLiteral("log-249")));
}

void TestServices::serverServiceConfigPersistence()
{
    {
        FakeService service;
        QVERIFY(service.start(QJsonObject{{QStringLiteral("port"), 46011},
                                          {QStringLiteral("root"), QStringLiteral("C:/saved")}}));
        service.stop();
    }

    FakeService reloaded;
    reloaded.loadConfig();
    QCOMPARE(reloaded.port(), 46011);
    QCOMPARE(reloaded.config().value(QStringLiteral("root")).toString(),
             QStringLiteral("C:/saved"));
    QCOMPARE(reloaded.config().value(QStringLiteral("allowWrite")).toBool(), false);

    // 保存的配置只覆盖已知字段，其余回落默认值
    QSettings settings;
    settings.beginGroup(QStringLiteral("NetworkServers"));
    settings.setValue(QStringLiteral("fake"),
                      QStringLiteral("{\"port\":46012,\"unknown\":true}"));
    settings.endGroup();
    settings.sync();
    FakeService partial;
    partial.loadConfig();
    QCOMPARE(partial.port(), 46012);
    QCOMPARE(partial.config().value(QStringLiteral("root")).toString(),
             QStringLiteral("C:/fake-root"));
    // 未知字段原样保留，不会被默认配置丢掉
    QCOMPARE(partial.config().value(QStringLiteral("unknown")).toBool(), true);

    // 损坏的 JSON 不应崩溃，直接使用默认配置
    settings.beginGroup(QStringLiteral("NetworkServers"));
    settings.setValue(QStringLiteral("fake"), QStringLiteral("{broken"));
    settings.endGroup();
    settings.sync();
    FakeService corrupted;
    corrupted.loadConfig();
    QCOMPARE(corrupted.port(), 45999);
}

void TestServices::serverManagerRegistry()
{
    ServerManager *manager = ServerManager::instance();
    QVERIFY(manager);
    const QList<ServerService *> first = manager->services();
    const QStringList expected{QStringLiteral("tftp"), QStringLiteral("ftp"),
                               QStringLiteral("http"), QStringLiteral("telnet"),
                               QStringLiteral("nfs")};
    QCOMPARE(first.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        QVERIFY(first.at(i));
        QCOMPARE(first.at(i)->id(), expected.at(i));
        QCOMPARE(manager->service(expected.at(i)), first.at(i));
    }

    // 单例：再次访问得到同一批对象
    const QList<ServerService *> second = manager->services();
    QCOMPARE(second, first);
    QCOMPARE(manager->service(QStringLiteral("no-such-service")), nullptr);
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
void TestServices::httpFilesAndMethods()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray content = QByteArrayLiteral("hello http\n");
    writeFile(dir.filePath(QStringLiteral("hello.txt")), content);
    writeFile(dir.filePath(QStringLiteral("hello world.txt")), "space");

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    HttpServer server;
    QSignalSpy runningSpy(&server, &ServerService::runningChanged);
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("listDirectory"), true}}),
             qPrintable(server.errorString()));
    QVERIFY(server.isRunning());
    QCOMPARE(runningSpy.count(), 1);

    bool completed = false;
    HttpResponse response = httpGet(port, "/hello.txt?ignored=1", false, &completed);
    QVERIFY(completed);
    QCOMPARE(response.status, 200);
    QCOMPARE(response.body, content);
    QCOMPARE(response.header("Content-Type"), QByteArray("text/plain; charset=utf-8"));
    QCOMPARE(response.header("Content-Length"), QByteArray::number(content.size()));
    QCOMPARE(response.header("Server"), QByteArray("NTerminal"));

    // HEAD：头部一致，但不发送正文
    response = httpGet(port, "/hello.txt", true);
    QCOMPARE(response.status, 200);
    QCOMPARE(response.body, QByteArray());
    QCOMPARE(response.header("Content-Length"), QByteArray::number(content.size()));

    // 百分号解码
    response = httpGet(port, "/hello%20world.txt");
    QCOMPARE(response.status, 200);
    QCOMPARE(response.body, QByteArray("space"));

    response = httpGet(port, "/missing.txt");
    QCOMPARE(response.status, 404);
    QVERIFY(response.body.contains("Not Found"));

    response = httpRequest(port, "POST /hello.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    QCOMPARE(response.status, 405);
    QVERIFY(response.body.contains("Only GET and HEAD"));

    // 路径穿越（明文与百分号编码）必须 403
    response = httpGet(port, "/../outside.txt");
    QCOMPARE(response.status, 403);
    response = httpGet(port, "/%2e%2e/outside.txt");
    QCOMPARE(response.status, 403);

    response = httpRequest(port, "BAD\r\n\r\n");
    QCOMPARE(response.status, 400);

    server.stop();
    QVERIFY(!server.isRunning());
    QCOMPARE(runningSpy.count(), 2);
}

void TestServices::httpDirectoryAndIndex()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("docs"))));
    writeFile(dir.filePath(QStringLiteral("docs/inside.txt")), "inside");
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("site"))));
    writeFile(dir.filePath(QStringLiteral("site/index.html")),
              "<!doctype html><h1>index page</h1>");

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    HttpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("listDirectory"), true}}),
             qPrintable(server.errorString()));

    HttpResponse response = httpGet(port, "/");
    QCOMPARE(response.status, 200);
    QVERIFY(response.header("Content-Type").startsWith("text/html"));
    QVERIFY(response.body.contains("docs/"));
    QVERIFY(response.body.contains("site/"));

    response = httpGet(port, "/docs/");
    QCOMPARE(response.status, 200);
    QVERIFY(response.body.contains("inside.txt"));
    QVERIFY(response.body.contains("../"));

    // 目录优先 index.html；带不带末尾斜杠都可以
    response = httpGet(port, "/site/");
    QCOMPARE(response.status, 200);
    QVERIFY(response.body.contains("index page"));
    QVERIFY(response.header("Content-Type").contains("text/html"));
    response = httpGet(port, "/site");
    QCOMPARE(response.status, 200);
    QVERIFY(response.body.contains("index page"));

    server.stop();
}

void TestServices::httpDirectoryListingDisabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("visible.txt")), "visible");

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    HttpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("listDirectory"), false}}),
             qPrintable(server.errorString()));

    HttpResponse response = httpGet(port, "/");
    QCOMPARE(response.status, 403);
    QVERIFY(response.body.contains("目录浏览已关闭"));

    // 明确请求文件仍然允许
    response = httpGet(port, "/visible.txt");
    QCOMPARE(response.status, 200);
    QCOMPARE(response.body, QByteArray("visible"));

    server.stop();
}

void TestServices::httpStartFailures()
{
    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);

    {
        HttpServer server;
        QSignalSpy errorSpy(&server, &ServerService::errorOccurred);
        QVERIFY(!server.start(QJsonObject{{QStringLiteral("port"), int(port)}}));
        QVERIFY(server.errorString().contains(QStringLiteral("根目录")));
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!server.isRunning());
    }
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        HttpServer server;
        QVERIFY(!server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                          {QStringLiteral("port"), 0}}));
        QVERIFY(server.errorString().contains(QStringLiteral("端口")));
    }
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::Any, port));
        HttpServer server;
        QVERIFY(!server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                          {QStringLiteral("port"), int(port)}}));
        QVERIFY2(server.errorString().contains(QStringLiteral("无法监听端口")),
                 qPrintable(server.errorString()));
    }
}

// ---------------------------------------------------------------------------
// FTP
// ---------------------------------------------------------------------------
void TestServices::ftpLoginAndSession()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("hello.txt")), "hello ftp\n");

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    FtpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowAnonymous"), true},
                                      {QStringLiteral("user"), QStringLiteral("tester")},
                                      {QStringLiteral("password"), QStringLiteral("secret")},
                                      {QStringLiteral("allowWrite"), true}}),
             qPrintable(server.errorString()));

    FtpClient client;
    QString error;
    QVERIFY2(client.connectTo(port, &error), qPrintable(error));
    QVERIFY(client.command(QStringLiteral("PWD")).startsWith(QStringLiteral("530")));

    QVERIFY(client.command(QStringLiteral("USER tester")).startsWith(QStringLiteral("331")));
    QVERIFY(client.command(QStringLiteral("PASS wrong")).startsWith(QStringLiteral("530")));
    QVERIFY(client.command(QStringLiteral("PASS secret")).startsWith(QStringLiteral("230")));

    const QString features = client.command(QStringLiteral("FEAT"));
    QVERIFY(features.contains(QStringLiteral("PASV")));
    QVERIFY(features.contains(QStringLiteral("UTF8")));
    QVERIFY(features.contains(QStringLiteral("211 End")));
    QVERIFY(client.command(QStringLiteral("OPTS UTF8 ON")).startsWith(QStringLiteral("200")));
    QVERIFY(client.command(QStringLiteral("OPTS FOO")).startsWith(QStringLiteral("501")));
    QVERIFY(client.command(QStringLiteral("SYST")).startsWith(QStringLiteral("215")));
    QVERIFY(client.command(QStringLiteral("TYPE I")).startsWith(QStringLiteral("200")));
    QVERIFY(client.command(QStringLiteral("MODE S")).startsWith(QStringLiteral("200")));
    QVERIFY(client.command(QStringLiteral("STRU F")).startsWith(QStringLiteral("200")));
    QVERIFY(client.command(QStringLiteral("NOOP")).startsWith(QStringLiteral("200")));
    QVERIFY(client.command(QStringLiteral("PWD")).contains(QStringLiteral("\"/\"")));
    QVERIFY(client.command(QStringLiteral("SIZE hello.txt")).startsWith(QStringLiteral("213 ")));
    const QString modified = client.command(QStringLiteral("MDTM hello.txt"));
    QVERIFY(modified.startsWith(QStringLiteral("213 ")));
    QCOMPARE(modified.mid(4).trimmed().size(), 14);
    QVERIFY(client.command(QStringLiteral("FOO")).startsWith(QStringLiteral("502")));
    QVERIFY(client.command(QStringLiteral("QUIT")).startsWith(QStringLiteral("221")));
    QVERIFY(waitUntil([&client] { return !client.isConnected(); }));

    server.stop();
}

void TestServices::ftpDirectoryAndDownloads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray content = QByteArrayLiteral("download me\n");
    writeFile(dir.filePath(QStringLiteral("hello.txt")), content);
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub"))));
    writeFile(dir.filePath(QStringLiteral("sub/nested.txt")), "nested");

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    FtpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowAnonymous"), true},
                                      {QStringLiteral("allowWrite"), true}}),
             qPrintable(server.errorString()));

    FtpClient client;
    QVERIFY(client.connectTo(port));
    QVERIFY(client.command(QStringLiteral("USER anonymous")).startsWith(QStringLiteral("331")));
    QVERIFY(client.command(QStringLiteral("PASS guest")).startsWith(QStringLiteral("230")));

    QVERIFY(client.command(QStringLiteral("CWD sub")).startsWith(QStringLiteral("250")));
    QVERIFY(client.command(QStringLiteral("PWD")).contains(QStringLiteral("\"/sub\"")));
    QVERIFY(client.command(QStringLiteral("CWD missing")).startsWith(QStringLiteral("550")));
    QVERIFY(client.command(QStringLiteral("SIZE nested.txt")).startsWith(QStringLiteral("213 6")));
    QVERIFY(client.command(QStringLiteral("CDUP")).startsWith(QStringLiteral("250")));
    QVERIFY(client.command(QStringLiteral("PWD")).contains(QStringLiteral("\"/\"")));

    QByteArray listing;
    QString finalReply;
    QVERIFY2(ftpDownload(client, QStringLiteral("LIST"), &listing, &finalReply),
             qPrintable(finalReply));
    QVERIFY(listing.contains("hello.txt"));
    QVERIFY(listing.contains("sub"));

    QByteArray names;
    QVERIFY2(ftpDownload(client, QStringLiteral("NLST"), &names, &finalReply),
             qPrintable(finalReply));
    QVERIFY(names.contains("hello.txt\r\n"));
    QVERIFY(names.contains("sub\r\n"));

    QByteArray payload;
    QVERIFY2(ftpDownload(client, QStringLiteral("RETR hello.txt"), &payload, &finalReply),
             qPrintable(finalReply));
    QCOMPARE(payload, content);

    // 目录不能当文件下载；越界路径直接拒绝
    QVERIFY(client.command(QStringLiteral("RETR sub")).startsWith(QStringLiteral("550")));
    QVERIFY(client.command(QStringLiteral("RETR ../outside.txt")).startsWith(QStringLiteral("550")));
    QVERIFY(client.command(QStringLiteral("LIST ../")).startsWith(QStringLiteral("550")));
    QVERIFY(client.command(QStringLiteral("SIZE missing.txt")).startsWith(QStringLiteral("550")));

    QVERIFY(client.command(QStringLiteral("QUIT")).startsWith(QStringLiteral("221")));
    server.stop();
}

void TestServices::ftpUploadAndFileManagement()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    FtpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowAnonymous"), true},
                                      {QStringLiteral("allowWrite"), true}}),
             qPrintable(server.errorString()));

    FtpClient client;
    QVERIFY(client.connectTo(port));
    QVERIFY(client.command(QStringLiteral("USER ftp")).startsWith(QStringLiteral("331")));
    QVERIFY(client.command(QStringLiteral("PASS ftp")).startsWith(QStringLiteral("230")));

    QString finalReply;
    const QByteArray uploaded = QByteArrayLiteral("uploaded data\n");
    QVERIFY2(ftpUpload(client, QStringLiteral("STOR upload.txt"), uploaded, &finalReply),
             qPrintable(finalReply));
    QCOMPARE(readFile(dir.filePath(QStringLiteral("upload.txt"))), uploaded);

    const QByteArray appended = QByteArrayLiteral("more\n");
    QVERIFY2(ftpUpload(client, QStringLiteral("APPE upload.txt"), appended, &finalReply),
             qPrintable(finalReply));
    QCOMPARE(readFile(dir.filePath(QStringLiteral("upload.txt"))), uploaded + appended);

    QVERIFY(client.command(QStringLiteral("MKD newdir")).startsWith(QStringLiteral("257")));
    QVERIFY(QDir(dir.filePath(QStringLiteral("newdir"))).exists());
    QVERIFY(client.command(QStringLiteral("CWD newdir")).startsWith(QStringLiteral("250")));
    QVERIFY2(ftpUpload(client, QStringLiteral("STOR inside.txt"), QByteArray("inside"),
                       &finalReply),
             qPrintable(finalReply));
    QVERIFY(client.command(QStringLiteral("RNFR inside.txt")).startsWith(QStringLiteral("350")));
    QVERIFY(client.command(QStringLiteral("RNTO renamed.txt")).startsWith(QStringLiteral("250")));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("newdir/renamed.txt"))));
    QVERIFY(client.command(QStringLiteral("DELE renamed.txt")).startsWith(QStringLiteral("250")));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("newdir/renamed.txt"))));
    QVERIFY(client.command(QStringLiteral("CDUP")).startsWith(QStringLiteral("250")));
    QVERIFY(client.command(QStringLiteral("RMD newdir")).startsWith(QStringLiteral("250")));
    QVERIFY(!QDir(dir.filePath(QStringLiteral("newdir"))).exists());
    QVERIFY(client.command(QStringLiteral("DELE upload.txt")).startsWith(QStringLiteral("250")));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("upload.txt"))));

    server.stop();
}

void TestServices::ftpActiveModeDownload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray content = QByteArrayLiteral("active mode payload");
    writeFile(dir.filePath(QStringLiteral("active.txt")), content);

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    FtpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowAnonymous"), true}}),
             qPrintable(server.errorString()));

    FtpClient client;
    QVERIFY(client.connectTo(port));
    QVERIFY(client.command(QStringLiteral("USER anonymous")).startsWith(QStringLiteral("331")));
    QVERIFY(client.command(QStringLiteral("PASS x")).startsWith(QStringLiteral("230")));

    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
    const quint16 dataPort = listener.serverPort();
    QVERIFY(client
                .command(QStringLiteral("PORT 127,0,0,1"))
                .startsWith(QStringLiteral("501")));
    QVERIFY(client
                .command(QStringLiteral("PORT 127,0,0,1,%1,%2")
                             .arg(dataPort / 256)
                             .arg(dataPort % 256))
                .startsWith(QStringLiteral("200")));
    const QString started = client.command(QStringLiteral("RETR active.txt"));
    QVERIFY2(started.startsWith(QStringLiteral("150")), qPrintable(started));
    QVERIFY(waitUntil([&listener] { return listener.hasPendingConnections(); }));
    QTcpSocket *data = listener.nextPendingConnection();
    QVERIFY(data);
    QByteArray payload;
    QVERIFY(readUntilClosed(*data, &payload));
    QCOMPARE(payload, content);
    data->deleteLater();
    QVERIFY(client.readReply(8000).startsWith(QStringLiteral("226")));

    server.stop();
}

void TestServices::ftpReadOnlyAndTraversal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("keep.txt")), "keep");
    writeFile(dir.filePath(QStringLiteral("outside.txt")), "outside");

    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    FtpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowAnonymous"), false},
                                      {QStringLiteral("user"), QStringLiteral("u")},
                                      {QStringLiteral("password"), QStringLiteral("p")},
                                      {QStringLiteral("allowWrite"), false}}),
             qPrintable(server.errorString()));

    FtpClient client;
    QVERIFY(client.connectTo(port));
    QVERIFY(client.command(QStringLiteral("USER u")).startsWith(QStringLiteral("331")));
    QVERIFY(client.command(QStringLiteral("PASS p")).startsWith(QStringLiteral("230")));

    for (const QString &command : {QStringLiteral("STOR x.txt"), QStringLiteral("APPE x.txt"),
                                   QStringLiteral("DELE keep.txt"),
                                   QStringLiteral("MKD newdir")}) {
        const QString reply = client.command(command);
        QVERIFY2(reply.startsWith(QStringLiteral("550")), qPrintable(command + " => " + reply));
        QVERIFY2(reply.contains(QStringLiteral("只读")), qPrintable(reply));
    }
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("keep.txt"))));
    QVERIFY(!QDir(dir.filePath(QStringLiteral("newdir"))).exists());
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("x.txt"))));

    // 只读不影响重命名的第一步，但 RNTO 必须拒绝
    QVERIFY(client.command(QStringLiteral("RNFR keep.txt")).startsWith(QStringLiteral("350")));
    QVERIFY(client.command(QStringLiteral("RNTO moved.txt")).startsWith(QStringLiteral("550")));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("keep.txt"))));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("moved.txt"))));

    // 登录后仍不得越界
    QVERIFY(client.command(QStringLiteral("CWD ..")).startsWith(QStringLiteral("550")));
    QVERIFY(client.command(QStringLiteral("CWD /../")).startsWith(QStringLiteral("550")));
    QVERIFY(client.command(QStringLiteral("RETR ../outside.txt")).startsWith(QStringLiteral("550")));
    QVERIFY(client.command(QStringLiteral("RNFR ../outside.txt")).startsWith(QStringLiteral("550")));

    server.stop();
}

void TestServices::ftpAnonymousAndUnauthenticated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    FtpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowAnonymous"), true},
                                      {QStringLiteral("user"), QStringLiteral("tester")},
                                      {QStringLiteral("password"), QStringLiteral("secret")},
                                      {QStringLiteral("allowWrite"), false}}),
             qPrintable(server.errorString()));

    {
        FtpClient anonymous;
        QVERIFY(anonymous.connectTo(port));
        QVERIFY(anonymous.command(QStringLiteral("USER anonymous"))
                    .startsWith(QStringLiteral("331")));
        QVERIFY(anonymous.command(QStringLiteral("PASS anything"))
                    .startsWith(QStringLiteral("230")));
        QVERIFY(anonymous.command(QStringLiteral("QUIT")).startsWith(QStringLiteral("221")));
    }
    {
        FtpClient account;
        QVERIFY(account.connectTo(port));
        QVERIFY(account.command(QStringLiteral("USER tester")).startsWith(QStringLiteral("331")));
        QVERIFY(account.command(QStringLiteral("PASS secret")).startsWith(QStringLiteral("230")));
        QVERIFY(account.command(QStringLiteral("QUIT")).startsWith(QStringLiteral("221")));
    }
    {
        FtpClient bad;
        QVERIFY(bad.connectTo(port));
        QVERIFY(bad.command(QStringLiteral("USER nobody")).startsWith(QStringLiteral("331")));
        QVERIFY(bad.command(QStringLiteral("PASS secret")).startsWith(QStringLiteral("530")));
        // 未登录时除 USER/PASS/QUIT/FEAT/OPTS 外一律 530
        QVERIFY(bad.command(QStringLiteral("PWD")).startsWith(QStringLiteral("530")));
        QVERIFY(bad.command(QStringLiteral("FOO")).startsWith(QStringLiteral("530")));
    }

    server.stop();
}

// ---------------------------------------------------------------------------
// TFTP
// ---------------------------------------------------------------------------
void TestServices::tftpDownloadDefaultBlocks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray content;
    for (int i = 0; i < 1300; ++i) {
        content.append(char('A' + (i % 26)));
    }
    writeFile(dir.filePath(QStringLiteral("multi.bin")), content);

    const quint16 port = freeUdpPort();
    QVERIFY(port != 0);
    TftpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)}}),
             qPrintable(server.errorString()));

    const TftpTransferResult result =
        tftpDownload(port, QStringLiteral("multi.bin"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.data, content);
    QCOMPARE(result.blockSize, 512);
    QVERIFY(result.oack.isEmpty());

    server.stop();
}

void TestServices::tftpDownloadWithOptions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray content;
    for (int i = 0; i < 1300; ++i) {
        content.append(char(i & 0xFF));
    }
    writeFile(dir.filePath(QStringLiteral("opt.bin")), content);

    const quint16 port = freeUdpPort();
    QVERIFY(port != 0);
    TftpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)}}),
             qPrintable(server.errorString()));

    TftpTransferResult result = tftpDownload(
        port, QStringLiteral("opt.bin"),
        {{QStringLiteral("blksize"), QStringLiteral("1024")},
         {QStringLiteral("tsize"), QStringLiteral("0")}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.data, content);
    QCOMPARE(result.blockSize, 1024);
    QVERIFY(!result.oack.isEmpty());
    QCOMPARE(readU16(result.oack), quint16(6));
    const QMap<QString, QString> options = parseTftpOptions(result.oack, 2);
    QCOMPARE(options.value(QStringLiteral("blksize")), QStringLiteral("1024"));
    QCOMPARE(options.value(QStringLiteral("tsize")), QString::number(content.size()));

    // blksize 小于 8 时使用默认块长
    result = tftpDownload(port, QStringLiteral("opt.bin"),
                          {{QStringLiteral("blksize"), QStringLiteral("4")}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.blockSize, 512);

    server.stop();
}

void TestServices::tftpDownloadErrors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("ok.txt")), "ok");

    const quint16 port = freeUdpPort();
    QVERIFY(port != 0);
    TftpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)}}),
             qPrintable(server.errorString()));

    TftpTransferResult result = tftpDownload(port, QStringLiteral("missing.txt"));
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, quint16(1));
    QVERIFY(result.error.contains(QStringLiteral("不存在")));

    result = tftpDownload(port, QStringLiteral("../outside.txt"));
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, quint16(1));

    // 目录不能当文件传输
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("sub"))));
    result = tftpDownload(port, QStringLiteral("sub"));
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, quint16(1));

    server.stop();
}

void TestServices::tftpUploadDenied()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const quint16 port = freeUdpPort();
    QVERIFY(port != 0);
    TftpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowWrite"), false}}),
             qPrintable(server.errorString()));

    const TftpTransferResult result =
        tftpUpload(port, QStringLiteral("denied.txt"), QByteArray("x"));
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, quint16(2));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("denied.txt"))));

    server.stop();
}

void TestServices::tftpUploadWithOptions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray content;
    for (int i = 0; i < 600; ++i) {
        content.append(char('a' + (i % 26)));
    }

    const quint16 port = freeUdpPort();
    QVERIFY(port != 0);
    TftpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowWrite"), true}}),
             qPrintable(server.errorString()));

    TftpTransferResult result = tftpUpload(
        port, QStringLiteral("upload.bin"), content,
        {{QStringLiteral("blksize"), QStringLiteral("1024")}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.blockSize, 1024);
    QVERIFY(!result.oack.isEmpty());
    QCOMPARE(readU16(result.oack), quint16(6));
    QCOMPARE(parseTftpOptions(result.oack, 2).value(QStringLiteral("blksize")),
             QStringLiteral("1024"));
    QCOMPARE(readFile(dir.filePath(QStringLiteral("upload.bin"))), content);

    // 默认块长、多块上传
    QByteArray large;
    for (int i = 0; i < 1200; ++i) {
        large.append(char(i & 0xFF));
    }
    result = tftpUpload(port, QStringLiteral("default.bin"), large);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.blockSize, 512);
    QCOMPARE(readFile(dir.filePath(QStringLiteral("default.bin"))), large);

    // 上传路径不得越界
    result = tftpUpload(port, QStringLiteral("../escaped.bin"), QByteArray("bad"));
    QVERIFY(!result.ok);
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("escaped.bin"))));

    server.stop();
}

void TestServices::tftpRejectsInvalidRequests()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const quint16 port = freeUdpPort();
    QVERIFY(port != 0);
    TftpServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                      {QStringLiteral("port"), int(port)},
                                      {QStringLiteral("allowWrite"), true}}),
             qPrintable(server.errorString()));

    // 未知操作码
    {
        TftpClient client;
        QVERIFY(client.bind());
        QByteArray garbage;
        appendU16(garbage, 9);
        appendU16(garbage, 0);
        client.send(garbage, port);
        const QByteArray reply = client.receive();
        QVERIFY(!reply.isEmpty());
        QCOMPARE(readU16(reply), quint16(5));
        QCOMPARE(readU16(reply.mid(2)), quint16(4));
    }
    // 不支持的传输模式
    {
        TftpClient client;
        QVERIFY(client.bind());
        QByteArray request;
        appendU16(request, 1);
        request += "ok.txt";
        request.append('\0');
        request += "mail";
        request.append('\0');
        writeFile(dir.filePath(QStringLiteral("ok.txt")), "ok");
        client.send(request, port);
        const QByteArray reply = client.receive();
        QVERIFY(!reply.isEmpty());
        QCOMPARE(readU16(reply), quint16(5));
        QCOMPARE(readU16(reply.mid(2)), quint16(4));
    }

    server.stop();
}

// ---------------------------------------------------------------------------
// Telnet
// ---------------------------------------------------------------------------
void TestServices::telnetGreetingAndCommand()
{
    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    TelnetServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("port"), int(port)}}),
             qPrintable(server.errorString()));

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(waitUntil([&socket] {
        return socket.state() == QAbstractSocket::ConnectedState;
    }));
    QByteArray received;
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return received.contains("> ");
    }, 8000));
    // IAC WILL ECHO / IAC WILL SGA
    QVERIFY(received.startsWith(QByteArray("\xFF\xFB\x01\xFF\xFB\x03", 6)));
    QVERIFY(received.contains("NTerminal Telnet"));

    socket.write("echo telnet-ok\r\n");
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return QString::fromUtf8(received).count(QStringLiteral("> ")) >= 2;
    }, 15000));
    QVERIFY(received.contains("telnet-ok"));

    // 中文与退格处理不崩溃（cmd 执行 echo）
    socket.write("echo 中文\r\n");
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return QString::fromUtf8(received).count(QStringLiteral("> ")) >= 3;
    }, 15000));

    socket.write("exit\r\n");
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return QString::fromUtf8(received).contains(QStringLiteral("再见"))
               && socket.state() == QAbstractSocket::UnconnectedState;
    }, 8000));

    server.stop();
}

void TestServices::telnetCdAndExit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);
    TelnetServer server;
    QVERIFY2(server.start(QJsonObject{{QStringLiteral("port"), int(port)}}),
             qPrintable(server.errorString()));

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(waitUntil([&socket] {
        return socket.state() == QAbstractSocket::ConnectedState;
    }));
    QByteArray received;
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return received.contains("> ");
    }, 8000));

    socket.write("cd no-such-dir-for-test\r\n");
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return QString::fromUtf8(received).contains(QStringLiteral("目录不存在"));
    }, 8000));

    const QByteArray command = "cd \""
                               + QDir::toNativeSeparators(dir.path()).toUtf8()
                               + "\"\r\n";
    socket.write(command);
    const QByteArray nativePath = QDir::toNativeSeparators(dir.path()).toUtf8();
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return received.contains(nativePath + "> ");
    }, 8000));

    socket.write("quit\r\n");
    QVERIFY(waitUntil([&] {
        received += socket.readAll();
        return socket.state() == QAbstractSocket::UnconnectedState;
    }, 8000));

    server.stop();
}

QTEST_GUILESS_MAIN(TestServices)
#include "tst_services.moc"
