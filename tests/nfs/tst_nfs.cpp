// ============================================================================
// tst_nfs —— NFSv3 / MOUNTv3 服务测试（TCP，RPC 记录标记 + XDR）
//
// 覆盖：
//   1. MOUNT：NULL / MNT（根目录与拒绝路径）/ DUMP / UMNT / UMNTALL / EXPORT
//   2. NFS：NULL、GETATTR、LOOKUP（含 . / .. / 非法名 / 超长名 / 坏句柄）
//   3. ACCESS / READ（偏移、EOF、目录、超大 count、坏句柄）
//   4. READDIR / READDIRPLUS（排序、cookie 分页、cookieverf、非目录）
//   5. FSSTAT / FSINFO / PATHCONF
//   6. RPC：一次写多条调用、记录分片、RPC 版本不符、未知程序/过程/版本、
//      非 CALL 消息、截断消息、超长记录
//   7. 启动失败：根目录缺失、端口无效、端口被占用
//
// 服务与客户端在同一线程：读应答时必须用 QTest::qWait 驱动事件循环。
// ============================================================================
#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <functional>
#include "services/nfsserver.h"

namespace {

constexpr quint32 kNfsProgram = 100003;
constexpr quint32 kMountProgram = 100005;
constexpr quint32 kVersion3 = 3;

constexpr quint32 kOk = 0;
constexpr quint32 kErrNoEnt = 2;
constexpr quint32 kErrNotDir = 20;
constexpr quint32 kErrIsDir = 21;
constexpr quint32 kErrNameTooLong = 63;
constexpr quint32 kErrStale = 70;
constexpr quint32 kErrBadHandle = 10001;
constexpr quint32 kErrBadCookie = 10003;

constexpr quint32 kAccessRead = 0x0001;
constexpr quint32 kAccessLookup = 0x0002;
constexpr quint32 kAccessModify = 0x0004;
constexpr quint32 kAccessExecute = 0x0020;

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

// ---------------------------------------------------------------------------
// 与生产代码对称的 XDR 编解码（测试侧独立实现，避免只验证自己）
// ---------------------------------------------------------------------------
class XdrWriter
{
public:
    void u32(quint32 value)
    {
        m_data.append(char((value >> 24) & 0xFF));
        m_data.append(char((value >> 16) & 0xFF));
        m_data.append(char((value >> 8) & 0xFF));
        m_data.append(char(value & 0xFF));
    }

    void u64(quint64 value)
    {
        u32(quint32(value >> 32));
        u32(quint32(value & 0xFFFFFFFFu));
    }

    void boolean(bool value) { u32(value ? 1 : 0); }

    void fixedOpaque(const QByteArray &data)
    {
        m_data.append(data);
        m_data.append(QByteArray((4 - (data.size() % 4)) % 4, '\0'));
    }

    void opaque(const QByteArray &data)
    {
        u32(quint32(data.size()));
        fixedOpaque(data);
    }

    void raw(const QByteArray &data) { m_data.append(data); }

    const QByteArray &data() const { return m_data; }

private:
    QByteArray m_data;
};

class XdrReader
{
public:
    explicit XdrReader(const QByteArray &data)
        : m_data(data)
    {
    }

    bool ok() const { return m_ok; }
    int remaining() const { return m_data.size() - m_pos; }
    QByteArray rest() const { return m_data.mid(m_pos); }

    quint32 u32()
    {
        if (m_pos + 4 > m_data.size()) {
            m_ok = false;
            m_pos = m_data.size();
            return 0;
        }
        const quint32 value = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_data.constData() + m_pos));
        m_pos += 4;
        return value;
    }

    quint64 u64()
    {
        const quint64 high = u32();
        const quint64 low = u32();
        return (high << 32) | low;
    }

    bool boolean()
    {
        const bool value = u32() != 0;
        return value && m_ok;
    }

    QByteArray fixedOpaque(int size)
    {
        if (size < 0 || m_pos + size > m_data.size()) {
            m_ok = false;
            m_pos = m_data.size();
            return {};
        }
        const QByteArray out = m_data.mid(m_pos, size);
        m_pos += size;
        m_pos = qMin(m_pos + (4 - (size % 4)) % 4, m_data.size());
        return out;
    }

    QByteArray opaque()
    {
        const quint32 size = u32();
        if (!m_ok || size > quint32(m_data.size())) {
            m_ok = false;
            return {};
        }
        return fixedOpaque(int(size));
    }

private:
    QByteArray m_data;
    int m_pos = 0;
    bool m_ok = true;
};

QByteArray encodeHandle(quint64 id)
{
    QByteArray handle(8, '\0');
    qToBigEndian<quint64>(id, handle.data());
    return handle;
}

quint64 decodeHandle(const QByteArray &handle)
{
    if (handle.size() != 8) {
        return 0;
    }
    return qFromBigEndian<quint64>(
        reinterpret_cast<const uchar *>(handle.constData()));
}

QByteArray makeCall(quint32 xid, quint32 program, quint32 version, quint32 proc,
                    const QByteArray &arguments = {}, quint32 rpcVersion = 2,
                    quint32 messageType = 0)
{
    XdrWriter writer;
    writer.u32(xid);
    writer.u32(messageType);   // CALL / REPLY
    writer.u32(rpcVersion);
    writer.u32(program);
    writer.u32(version);
    writer.u32(proc);
    writer.u32(0);             // AUTH_NULL
    writer.u32(0);             // 凭据长度
    writer.u32(0);             // 校验 flavor
    writer.u32(0);             // 校验长度
    writer.raw(arguments);
    return writer.data();
}

QByteArray frameRecord(const QByteArray &payload, bool last = true)
{
    QByteArray framed(4, '\0');
    qToBigEndian<quint32>((last ? 0x80000000u : 0u) | quint32(payload.size()),
                          framed.data());
    framed.append(payload);
    return framed;
}

struct RpcReply {
    bool ok = false;
    quint32 xid = 0;
    quint32 messageType = 0;
    quint32 replyStat = 0;
    quint32 acceptStat = 0;
    quint32 rejectStat = 0;
    quint32 low = 0;
    quint32 high = 0;
    QByteArray payload;
    QString error;
};

class RpcClient
{
public:
    bool connectTo(quint16 port)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        return waitUntil([this] {
                   return m_socket.state() == QAbstractSocket::ConnectedState
                          || m_socket.state() == QAbstractSocket::UnconnectedState;
               })
               && m_socket.state() == QAbstractSocket::ConnectedState;
    }

    bool sendRaw(const QByteArray &data)
    {
        return m_socket.write(data) == data.size();
    }

    bool sendCall(quint32 xid, quint32 program, quint32 version, quint32 proc,
                  const QByteArray &arguments = {})
    {
        return sendRaw(frameRecord(makeCall(xid, program, version, proc, arguments)));
    }

    RpcReply call(quint32 xid, quint32 program, quint32 version, quint32 proc,
                  const QByteArray &arguments = {})
    {
        RpcReply reply;
        if (!sendCall(xid, program, version, proc, arguments)) {
            reply.error = m_socket.errorString();
            return reply;
        }
        return readReply();
    }

    RpcReply readReply(int timeoutMs = 5000)
    {
        RpcReply reply;
        QElapsedTimer timer;
        timer.start();
        bool peerClosed = false;
        while (timer.elapsed() < timeoutMs) {
            m_buffer += m_socket.readAll();
            if (m_buffer.size() >= 4) {
                const quint32 header = qFromBigEndian<quint32>(
                    reinterpret_cast<const uchar *>(m_buffer.constData()));
                const quint32 length = header & 0x7FFFFFFFu;
                if (m_buffer.size() >= 4 + int(length)) {
                    const QByteArray message = m_buffer.mid(4, int(length));
                    m_buffer.remove(0, 4 + int(length));
                    if (!parseReply(message, &reply)) {
                        reply.error = QStringLiteral("RPC 应答解析失败");
                    }
                    return reply;
                }
            }
            if (peerClosed) {
                break;
            }
            if (m_socket.state() == QAbstractSocket::UnconnectedState) {
                peerClosed = true;
                continue;
            }
            QTest::qWait(5);
        }
        reply.error = QStringLiteral("等待 RPC 应答超时");
        return reply;
    }

    void close() { m_socket.disconnectFromHost(); }

    QTcpSocket &socket() { return m_socket; }
    int bufferedBytes() const { return m_buffer.size(); }

private:
    static bool parseReply(const QByteArray &message, RpcReply *reply)
    {
        XdrReader in(message);
        reply->xid = in.u32();
        reply->messageType = in.u32();
        if (!in.ok() || reply->messageType != 1) {   // 所有应答都是 REPLY
            return false;
        }
        reply->replyStat = in.u32();
        if (reply->replyStat == 0) {   // MSG_ACCEPTED
            in.u32();                     // verf flavor
            const quint32 verfLength = in.u32();
            in.fixedOpaque(int(verfLength));
            reply->acceptStat = in.u32();
            reply->payload = in.rest();
            reply->ok = in.ok();
        } else {                       // MSG_DENIED
            reply->rejectStat = in.u32();
            if (reply->rejectStat == 0) {   // RPC_MISMATCH
                reply->low = in.u32();
                reply->high = in.u32();
            }
            reply->ok = in.ok();
        }
        return reply->ok;
    }

    QTcpSocket m_socket;
    QByteArray m_buffer;
};

// ---------------------------------------------------------------------------
// NFS 应答结构
// ---------------------------------------------------------------------------
struct NfsFattr {
    bool ok = false;
    quint32 type = 0;
    quint32 mode = 0;
    quint32 nlink = 0;
    quint32 uid = 0;
    quint32 gid = 0;
    quint64 size = 0;
    quint64 used = 0;
    quint32 rdevMajor = 0;
    quint32 rdevMinor = 0;
    quint64 fsid = 0;
    quint64 fileid = 0;
    qint64 mtimeSec = 0;
};

NfsFattr parseFattr(XdrReader &in)
{
    NfsFattr attr;
    attr.type = in.u32();
    attr.mode = in.u32();
    attr.nlink = in.u32();
    attr.uid = in.u32();
    attr.gid = in.u32();
    attr.size = in.u64();
    attr.used = in.u64();
    attr.rdevMajor = in.u32();
    attr.rdevMinor = in.u32();
    attr.fsid = in.u64();
    attr.fileid = in.u64();
    in.u32();   // atime.seconds
    in.u32();   // atime.nseconds
    attr.mtimeSec = in.u32();
    in.u32();   // mtime.nseconds
    in.u32();   // ctime.seconds
    in.u32();   // ctime.nseconds
    attr.ok = in.ok();
    return attr;
}

bool parsePostAttr(XdrReader &in, NfsFattr *attr = nullptr)
{
    const bool present = in.boolean();
    if (present) {
        const NfsFattr parsed = parseFattr(in);
        if (attr) {
            *attr = parsed;
        }
    }
    return present;
}

struct LookupReply {
    bool transportOk = false;
    quint32 acceptStat = 0;
    quint32 status = 0;
    QByteArray handle;
    bool objectAttrs = false;
    NfsFattr object;
    bool dirAttrs = false;
};

LookupReply doLookup(RpcClient &client, const QByteArray &dirHandle,
                     const QByteArray &name, quint32 xid)
{
    XdrWriter args;
    args.opaque(dirHandle);
    args.opaque(name);
    const RpcReply reply =
        client.call(xid, kNfsProgram, kVersion3, 3, args.data());
    LookupReply result;
    result.transportOk = reply.ok;
    result.acceptStat = reply.acceptStat;
    if (!reply.ok || reply.acceptStat != kOk) {
        return result;
    }
    XdrReader in(reply.payload);
    result.status = in.u32();
    if (result.status == kOk) {
        result.handle = in.opaque();
        result.objectAttrs = parsePostAttr(in, &result.object);
        result.dirAttrs = parsePostAttr(in);
    }
    return result;
}

struct GetattrReply {
    bool transportOk = false;
    quint32 acceptStat = 0;
    quint32 status = 0;
    bool attrsPresent = false;
    NfsFattr attrs;
};

GetattrReply doGetattr(RpcClient &client, const QByteArray &handle, quint32 xid)
{
    XdrWriter args;
    args.opaque(handle);
    const RpcReply reply =
        client.call(xid, kNfsProgram, kVersion3, 1, args.data());
    GetattrReply result;
    result.transportOk = reply.ok;
    result.acceptStat = reply.acceptStat;
    if (!reply.ok || reply.acceptStat != kOk) {
        return result;
    }
    XdrReader in(reply.payload);
    result.status = in.u32();
    if (result.status == kOk) {
        result.attrs = parseFattr(in);
        result.attrsPresent = result.attrs.ok;
    }
    return result;
}

struct AccessReply {
    bool transportOk = false;
    quint32 acceptStat = 0;
    quint32 status = 0;
    bool attrsPresent = false;
    quint32 allowed = 0;
};

AccessReply doAccess(RpcClient &client, const QByteArray &handle,
                     quint32 requested, quint32 xid)
{
    XdrWriter args;
    args.opaque(handle);
    args.u32(requested);
    const RpcReply reply =
        client.call(xid, kNfsProgram, kVersion3, 4, args.data());
    AccessReply result;
    result.transportOk = reply.ok;
    result.acceptStat = reply.acceptStat;
    if (!reply.ok || reply.acceptStat != kOk) {
        return result;
    }
    XdrReader in(reply.payload);
    result.status = in.u32();
    result.attrsPresent = parsePostAttr(in);
    if (result.status == kOk) {
        result.allowed = in.u32();
    }
    return result;
}

struct ReadReply {
    bool transportOk = false;
    quint32 acceptStat = 0;
    quint32 status = 0;
    bool attrsPresent = false;
    quint32 count = 0;
    bool eof = false;
    QByteArray data;
};

ReadReply doRead(RpcClient &client, const QByteArray &handle, quint64 offset,
                 quint32 count, quint32 xid)
{
    XdrWriter args;
    args.opaque(handle);
    args.u64(offset);
    args.u32(count);
    const RpcReply reply =
        client.call(xid, kNfsProgram, kVersion3, 6, args.data());
    ReadReply result;
    result.transportOk = reply.ok;
    result.acceptStat = reply.acceptStat;
    if (!reply.ok || reply.acceptStat != kOk) {
        return result;
    }
    XdrReader in(reply.payload);
    result.status = in.u32();
    result.attrsPresent = parsePostAttr(in);
    if (result.status == kOk) {
        result.count = in.u32();
        result.eof = in.boolean();
        result.data = in.opaque();
    }
    return result;
}

struct DirEntry {
    quint64 fileid = 0;
    QByteArray name;
    quint64 cookie = 0;
    bool attrsPresent = false;
    NfsFattr attrs;
    bool handlePresent = false;
    QByteArray handle;
};

struct ReaddirReply {
    bool transportOk = false;
    quint32 acceptStat = 0;
    quint32 status = 0;
    bool attrsPresent = false;
    QByteArray cookieVerf;
    QList<DirEntry> entries;
    bool eof = false;
    bool parsed = false;

    quint64 lastCookie() const
    {
        return entries.isEmpty() ? 0 : entries.last().cookie;
    }
};

ReaddirReply doReaddir(RpcClient &client, const QByteArray &handle,
                       quint64 cookie, const QByteArray &verf,
                       quint32 dircount, quint32 maxcount, bool plus,
                       quint32 xid)
{
    XdrWriter args;
    args.opaque(handle);
    args.u64(cookie);
    args.fixedOpaque(verf);
    args.u32(dircount);
    args.u32(maxcount);
    const RpcReply reply =
        client.call(xid, kNfsProgram, kVersion3, plus ? 17 : 14, args.data());
    ReaddirReply result;
    result.transportOk = reply.ok;
    result.acceptStat = reply.acceptStat;
    if (!reply.ok || reply.acceptStat != kOk) {
        return result;
    }
    XdrReader in(reply.payload);
    result.status = in.u32();
    result.attrsPresent = parsePostAttr(in);
    if (result.status != kOk) {
        return result;
    }
    result.cookieVerf = in.fixedOpaque(8);
    for (;;) {
        const bool follow = in.boolean();
        if (!follow) {
            break;
        }
        DirEntry entry;
        entry.fileid = in.u64();
        entry.name = in.opaque();
        entry.cookie = in.u64();
        if (plus) {
            entry.attrsPresent = parsePostAttr(in, &entry.attrs);
            entry.handlePresent = in.boolean();
            if (entry.handlePresent) {
                entry.handle = in.opaque();
            }
        }
        if (!in.ok()) {
            return result;
        }
        result.entries.append(entry);
    }
    result.eof = in.boolean();
    result.parsed = in.ok();
    return result;
}

// ---------------------------------------------------------------------------
// 服务夹具
// ---------------------------------------------------------------------------
class NfsFixture
{
public:
    QTemporaryDir dir;
    NfsServer server;
    quint16 port = 0;

    bool start(const QJsonObject &extra = {})
    {
        if (!dir.isValid()) {
            return false;
        }
        port = freeTcpPort();
        if (port == 0) {
            return false;
        }
        QJsonObject config{{QStringLiteral("root"), dir.path()},
                           {QStringLiteral("port"), int(port)}};
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            config.insert(it.key(), it.value());
        }
        return server.start(config);
    }

    ~NfsFixture() { server.stop(); }
};

} // namespace

class TestNfs : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void mountAndExport();
    void getattrAndLookup();
    void lookupErrors();
    void accessAndRead();
    void readdirListsAndPaginates();
    void readdirPlusAndErrors();
    void statusProcedures();
    void rpcFramingAndProtocolErrors();
    void startFailures();
};

void TestNfs::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("NTerminalTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_nfs"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings().clear();
}

void TestNfs::cleanupTestCase()
{
    QSettings().clear();
}

void TestNfs::mountAndExport()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    writeFile(fixture.dir.filePath(QStringLiteral("hello.txt")), "hello");
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));

    // MOUNT NULL
    RpcReply reply = client.call(2001, kMountProgram, 3, 0);
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.xid, quint32(2001));
    QCOMPARE(reply.messageType, quint32(1));   // REPLY
    QCOMPARE(reply.replyStat, quint32(0));     // MSG_ACCEPTED
    QCOMPARE(reply.acceptStat, kOk);
    QVERIFY(reply.payload.isEmpty());

    // MNT "/"：只导出根目录，句柄固定为 1，AUTH_UNIX
    XdrWriter mountArgs;
    mountArgs.opaque("/");
    reply = client.call(2002, kMountProgram, 3, 1, mountArgs.data());
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, kOk);
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), kOk);
        const QByteArray handle = in.opaque();
        QCOMPARE(handle.size(), 8);
        QCOMPARE(decodeHandle(handle), quint64(1));
        QCOMPARE(in.u32(), quint32(1));   // auth_flavors 数量
        QCOMPARE(in.u32(), quint32(1));   // AUTH_UNIX
        QVERIFY(in.ok());
    }

    // 空路径同样视为根目录
    XdrWriter emptyMount;
    emptyMount.opaque("");
    reply = client.call(2003, kMountProgram, 3, 1, emptyMount.data());
    QVERIFY(reply.ok);
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), kOk);
        QCOMPARE(decodeHandle(in.opaque()), quint64(1));
    }

    // 未导出的路径
    XdrWriter badMount;
    badMount.opaque("/not-exported");
    reply = client.call(2004, kMountProgram, 3, 1, badMount.data());
    QVERIFY(reply.ok);
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), quint32(2));   // MNT3ERR_NOENT
    }

    // DUMP：没有其他导出项
    reply = client.call(2005, kMountProgram, 3, 2);
    QVERIFY(reply.ok);
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.boolean(), false);
    }

    // UMNT / UMNTALL 只回成功
    XdrWriter umnt;
    umnt.opaque("/");
    reply = client.call(2006, kMountProgram, 3, 3, umnt.data());
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, kOk);
    QVERIFY(reply.payload.isEmpty());
    reply = client.call(2007, kMountProgram, 3, 4);
    QVERIFY(reply.ok);
    QVERIFY(reply.payload.isEmpty());

    // EXPORT：一个导出项 "/"，无组列表、无后续项
    reply = client.call(2008, kMountProgram, 3, 5);
    QVERIFY(reply.ok);
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.boolean(), true);
        QCOMPARE(in.opaque(), QByteArray("/"));
        QCOMPARE(in.boolean(), false);   // groups
        QCOMPARE(in.boolean(), false);   // 下一个导出项
        QVERIFY(in.ok());
    }

    // 未实现的 MOUNT 过程与版本不符
    reply = client.call(2009, kMountProgram, 3, 9);
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, quint32(3));   // PROC_UNAVAIL

    reply = client.call(2010, kMountProgram, 2, 0);
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, quint32(2));   // PROG_MISMATCH
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), quint32(3));
        QCOMPARE(in.u32(), quint32(3));
    }
}

void TestNfs::getattrAndLookup()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    const QByteArray fileData = QByteArrayLiteral("0123456789abcdefghij");
    writeFile(fixture.dir.filePath(QStringLiteral("hello.txt")), fileData);
    QVERIFY(QDir().mkpath(fixture.dir.filePath(QStringLiteral("sub"))));
    writeFile(fixture.dir.filePath(QStringLiteral("sub/inner.txt")), "inner");
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));

    // NFS NULL
    RpcReply nullReply = client.call(2101, kNfsProgram, 3, 0);
    QVERIFY(nullReply.ok);
    QCOMPARE(nullReply.acceptStat, kOk);
    QVERIFY(nullReply.payload.isEmpty());

    // 根目录 GETATTR
    const QByteArray rootHandle = encodeHandle(1);
    GetattrReply rootAttr = doGetattr(client, rootHandle, 2102);
    QVERIFY(rootAttr.transportOk);
    QCOMPARE(rootAttr.acceptStat, kOk);
    QCOMPARE(rootAttr.status, kOk);
    QVERIFY(rootAttr.attrsPresent);
    QCOMPARE(rootAttr.attrs.type, quint32(2));      // NF3DIR
    QCOMPARE(rootAttr.attrs.mode, quint32(0755));
    QCOMPARE(rootAttr.attrs.nlink, quint32(2));
    QCOMPARE(rootAttr.attrs.fsid, quint64(0x4E54));
    QCOMPARE(rootAttr.attrs.fileid, quint64(1));
    QCOMPARE(rootAttr.attrs.uid, quint32(0));
    QCOMPARE(rootAttr.attrs.gid, quint32(0));

    // LOOKUP 文件
    const LookupReply fileLookup =
        doLookup(client, rootHandle, QByteArrayLiteral("hello.txt"), 2103);
    QVERIFY(fileLookup.transportOk);
    QCOMPARE(fileLookup.acceptStat, kOk);
    QCOMPARE(fileLookup.status, kOk);
    QCOMPARE(fileLookup.handle.size(), 8);
    QVERIFY(fileLookup.objectAttrs);
    QVERIFY(fileLookup.dirAttrs);
    QCOMPARE(fileLookup.object.type, quint32(1));   // NF3REG
    QCOMPARE(fileLookup.object.mode, quint32(0644));
    QCOMPARE(fileLookup.object.nlink, quint32(1));
    QCOMPARE(fileLookup.object.size, quint64(fileData.size()));
    QCOMPARE(fileLookup.object.used, quint64(fileData.size()));
    QCOMPARE(fileLookup.object.fileid, decodeHandle(fileLookup.handle));

    GetattrReply fileAttr = doGetattr(client, fileLookup.handle, 2104);
    QVERIFY(fileAttr.transportOk);
    QCOMPARE(fileAttr.status, kOk);
    QVERIFY(fileAttr.attrsPresent);
    QCOMPARE(fileAttr.attrs.type, quint32(1));
    QCOMPARE(fileAttr.attrs.size, quint64(fileData.size()));
    QCOMPARE(fileAttr.attrs.fileid, decodeHandle(fileLookup.handle));

    // 同一个文件多次 LOOKUP 得到同一个句柄
    const LookupReply again =
        doLookup(client, rootHandle, QByteArrayLiteral("hello.txt"), 2105);
    QCOMPARE(again.status, kOk);
    QCOMPARE(again.handle, fileLookup.handle);

    // LOOKUP "." 返回当前目录本身，".." 在根目录仍返回根
    const LookupReply dot = doLookup(client, rootHandle, QByteArrayLiteral("."), 2106);
    QVERIFY(dot.transportOk);
    QCOMPARE(dot.status, kOk);
    QCOMPARE(dot.handle, rootHandle);
    QVERIFY(dot.objectAttrs);
    QCOMPARE(dot.object.fileid, quint64(1));
    const LookupReply dotdot =
        doLookup(client, rootHandle, QByteArrayLiteral(".."), 2107);
    QCOMPARE(dotdot.status, kOk);
    QCOMPARE(dotdot.handle, rootHandle);

    // 子目录中 ".." 回到根目录
    const LookupReply sub =
        doLookup(client, rootHandle, QByteArrayLiteral("sub"), 2108);
    QVERIFY(sub.transportOk);
    QCOMPARE(sub.status, kOk);
    QVERIFY(sub.objectAttrs);
    QCOMPARE(sub.object.type, quint32(2));
    const LookupReply subDotDot =
        doLookup(client, sub.handle, QByteArrayLiteral(".."), 2109);
    QCOMPARE(subDotDot.status, kOk);
    QCOMPARE(subDotDot.handle, rootHandle);

    // 文件删除后其句柄变为 stale（不是 bad handle）
    QFile::remove(fixture.dir.filePath(QStringLiteral("hello.txt")));
    const GetattrReply gone = doGetattr(client, fileLookup.handle, 2110);
    QVERIFY(gone.transportOk);
    QCOMPARE(gone.status, kErrStale);

    // 句柄表在连接之间共享：连接 B 可以使用连接 A 取得的句柄
    RpcClient other;
    QVERIFY(other.connectTo(fixture.port));
    const GetattrReply shared = doGetattr(other, sub.handle, 2111);
    QVERIFY(shared.transportOk);
    QCOMPARE(shared.status, kOk);
    QVERIFY(shared.attrsPresent);
    QCOMPARE(shared.attrs.type, quint32(2));
}

void TestNfs::lookupErrors()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    writeFile(fixture.dir.filePath(QStringLiteral("hello.txt")), "x");
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));
    const QByteArray rootHandle = encodeHandle(1);

    // 不存在的名字：带目录属性
    LookupReply reply = doLookup(client, rootHandle, QByteArrayLiteral("missing"), 2201);
    QVERIFY(reply.transportOk);
    QCOMPARE(reply.acceptStat, kOk);
    QCOMPARE(reply.status, kErrNoEnt);

    // 名字里带路径分隔符、盘符、父目录引用一律视为不存在
    for (const QByteArray &name : {QByteArray("a/b"), QByteArray("a\\b"),
                                   QByteArray("..\\x"), QByteArray("C:evil")}) {
        reply = doLookup(client, rootHandle, name, 2202);
        QVERIFY(reply.transportOk);
        QCOMPARE(reply.status, kErrNoEnt);
    }

    // 超过 255 字节的名字
    reply = doLookup(client, rootHandle, QByteArray(256, 'a'), 2203);
    QVERIFY(reply.transportOk);
    QCOMPARE(reply.status, kErrNameTooLong);

    // 长度不为 8 的句柄：bad handle
    reply = doLookup(client, QByteArray("abcd"), QByteArrayLiteral("hello.txt"), 2204);
    QVERIFY(reply.transportOk);
    QCOMPARE(reply.status, kErrBadHandle);

    // 未知的 8 字节句柄：stale
    reply = doLookup(client, encodeHandle(424242), QByteArrayLiteral("hello.txt"), 2205);
    QVERIFY(reply.transportOk);
    QCOMPARE(reply.status, kErrStale);

    // 用文件句柄当目录查找不存在的子项
    const LookupReply file =
        doLookup(client, rootHandle, QByteArrayLiteral("hello.txt"), 2206);
    QCOMPARE(file.status, kOk);
    reply = doLookup(client, file.handle, QByteArrayLiteral("child"), 2207);
    QVERIFY(reply.transportOk);
    QCOMPARE(reply.acceptStat, kOk);
    QCOMPARE(reply.status, kErrNoEnt);

    // 句柄参数被截断
    XdrWriter truncated;
    truncated.u32(8);
    truncated.raw("abcd");
    RpcReply raw = client.call(2208, kNfsProgram, 3, 1, truncated.data());
    QVERIFY(raw.ok);
    {
        XdrReader in(raw.payload);
        QCOMPARE(in.u32(), kErrBadHandle);
    }

    // GETATTR 的坏句柄 / stale 句柄
    GetattrReply attr = doGetattr(client, QByteArray("short"), 2209);
    QCOMPARE(attr.status, kErrBadHandle);
    attr = doGetattr(client, encodeHandle(987654), 2210);
    QCOMPARE(attr.status, kErrStale);
}

void TestNfs::accessAndRead()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    QByteArray content;
    for (int i = 0; i < 200; ++i) {
        content.append(char('A' + (i % 26)));
    }
    writeFile(fixture.dir.filePath(QStringLiteral("data.bin")), content);
    QByteArray large(70000, 'x');
    for (int i = 0; i < large.size(); ++i) {
        large[i] = char(i % 251);
    }
    writeFile(fixture.dir.filePath(QStringLiteral("large.bin")), large);
    QVERIFY(QDir().mkpath(fixture.dir.filePath(QStringLiteral("sub"))));
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));
    const QByteArray rootHandle = encodeHandle(1);
    const LookupReply file =
        doLookup(client, rootHandle, QByteArrayLiteral("data.bin"), 2301);
    QCOMPARE(file.status, kOk);
    const QByteArray fileHandle = file.handle;
    const LookupReply sub =
        doLookup(client, rootHandle, QByteArrayLiteral("sub"), 2302);
    QCOMPARE(sub.status, kOk);

    // ACCESS：目录可读/可查找，文件可读/可执行
    AccessReply access = doAccess(client, rootHandle, 0x3F, 2303);
    QVERIFY(access.transportOk);
    QCOMPARE(access.status, kOk);
    QVERIFY(access.attrsPresent);
    QCOMPARE(access.allowed, kAccessRead | kAccessLookup);
    access = doAccess(client, fileHandle, 0x3F, 2304);
    QCOMPARE(access.status, kOk);
    QCOMPARE(access.allowed, kAccessRead | kAccessExecute);
    access = doAccess(client, fileHandle, kAccessModify, 2305);
    QCOMPARE(access.status, kOk);
    QCOMPARE(access.allowed, quint32(0));
    access = doAccess(client, encodeHandle(777777), 0x3F, 2306);
    QCOMPARE(access.status, kErrStale);
    QVERIFY(!access.attrsPresent);

    // READ：整文件
    ReadReply read = doRead(client, fileHandle, 0, 65536, 2307);
    QVERIFY(read.transportOk);
    QCOMPARE(read.acceptStat, kOk);
    QCOMPARE(read.status, kOk);
    QVERIFY(read.attrsPresent);
    QCOMPARE(read.count, quint32(content.size()));
    QCOMPARE(read.data, content);
    QCOMPARE(read.eof, true);

    // READ：中间片段，未到 EOF
    read = doRead(client, fileHandle, 50, 20, 2308);
    QCOMPARE(read.status, kOk);
    QCOMPARE(read.data, content.mid(50, 20));
    QCOMPARE(read.eof, false);

    // READ：正好从末尾开始
    read = doRead(client, fileHandle, 200, 10, 2309);
    QCOMPARE(read.status, kOk);
    QCOMPARE(read.data, QByteArray());
    QCOMPARE(read.eof, true);

    // READ：越过末尾
    read = doRead(client, fileHandle, 1000, 10, 2310);
    QCOMPARE(read.status, kOk);
    QCOMPARE(read.data, QByteArray());
    QCOMPARE(read.eof, true);

    // READ：count=0 不返回数据，也未到 EOF
    read = doRead(client, fileHandle, 0, 0, 2311);
    QCOMPARE(read.status, kOk);
    QCOMPARE(read.data, QByteArray());
    QCOMPARE(read.eof, false);

    // READ：count 被限制在 64KB
    const LookupReply largeFile =
        doLookup(client, rootHandle, QByteArrayLiteral("large.bin"), 2312);
    QCOMPARE(largeFile.status, kOk);
    read = doRead(client, largeFile.handle, 0, 100000, 2313);
    QCOMPARE(read.status, kOk);
    QCOMPARE(read.count, quint32(65536));
    QCOMPARE(read.data, large.left(65536));
    QCOMPARE(read.eof, false);
    read = doRead(client, largeFile.handle, 65536, 100000, 2314);
    QCOMPARE(read.status, kOk);
    QCOMPARE(read.data, large.mid(65536));
    QCOMPARE(read.eof, true);

    // READ 目录：ISDIR，仍带属性
    read = doRead(client, sub.handle, 0, 10, 2315);
    QCOMPARE(read.status, kErrIsDir);
    QVERIFY(read.attrsPresent);

    // READ stale / bad handle
    read = doRead(client, encodeHandle(888888), 0, 10, 2316);
    QCOMPARE(read.status, kErrStale);
    QVERIFY(!read.attrsPresent);
    read = doRead(client, QByteArray("bad"), 0, 10, 2317);
    QCOMPARE(read.status, kErrBadHandle);
    QVERIFY(!read.attrsPresent);
}

void TestNfs::readdirListsAndPaginates()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    QVERIFY(QDir().mkpath(fixture.dir.filePath(QStringLiteral("dirA"))));
    writeFile(fixture.dir.filePath(QStringLiteral("a.txt")), "a");
    writeFile(fixture.dir.filePath(QStringLiteral("b.txt")), "b");
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));
    const QByteArray rootHandle = encodeHandle(1);
    const QByteArray zeroVerf(8, '\0');

    // 一次取完：目录项以 "."、".." 开头，目录在前，其余按名称忽略大小写
    ReaddirReply page = doReaddir(client, rootHandle, 0, zeroVerf,
                                  0, 65536, false, 2401);
    QVERIFY2(page.transportOk, qPrintable(QStringLiteral("transport")));
    QCOMPARE(page.acceptStat, kOk);
    QCOMPARE(page.status, kOk);
    QVERIFY(page.attrsPresent);
    QVERIFY(page.parsed);
    QCOMPARE(page.cookieVerf, zeroVerf);
    QCOMPARE(page.eof, true);
    QStringList names;
    for (const DirEntry &entry : page.entries) {
        names.append(QString::fromUtf8(entry.name));
        QVERIFY(!entry.attrsPresent);   // 非 PLUS 不带属性
        QVERIFY(!entry.handlePresent);
    }
    const QStringList expected{QStringLiteral("."), QStringLiteral(".."),
                               QStringLiteral("dirA"), QStringLiteral("a.txt"),
                               QStringLiteral("b.txt")};
    QCOMPARE(names, expected);
    // cookie 从 1 开始递增，"."/".." 的 fileid 分别为根与根
    QCOMPARE(page.entries.at(0).cookie, quint64(1));
    QCOMPARE(page.entries.at(0).fileid, quint64(1));
    QCOMPARE(page.entries.at(1).cookie, quint64(2));
    QCOMPARE(page.entries.at(1).fileid, quint64(1));
    for (int i = 1; i < page.entries.size(); ++i) {
        QCOMPARE(page.entries.at(i).cookie, quint64(page.entries.at(i - 1).cookie + 1));
    }

    // 小 maxcount 分页：每次至少一项，按 cookie 续读，最终与一次取完一致
    QStringList collected;
    quint64 cookie = 0;
    int rounds = 0;
    for (;;) {
        QVERIFY(++rounds < 20);
        page = doReaddir(client, rootHandle, cookie, zeroVerf, 0, 120, false,
                         2402 + rounds);
        QVERIFY(page.transportOk);
        QCOMPARE(page.acceptStat, kOk);
        QCOMPARE(page.status, kOk);
        QVERIFY(page.parsed);
        QVERIFY(!page.entries.isEmpty());
        for (const DirEntry &entry : page.entries) {
            collected.append(QString::fromUtf8(entry.name));
        }
        cookie = page.lastCookie();
        if (page.eof) {
            break;
        }
    }
    QCOMPARE(collected, expected);
    QVERIFY(rounds > 1);   // 确实发生了分页

    // cookie 非零时 cookieverf 必须是 8 字节零值
    page = doReaddir(client, rootHandle, 1, zeroVerf, 0, 65536, false, 2420);
    QVERIFY(page.transportOk);
    QCOMPARE(page.status, kOk);
    QVERIFY(!page.entries.isEmpty());
    QCOMPARE(QString::fromUtf8(page.entries.first().name), QStringLiteral(".."));

    // 错误的 cookieverf
    page = doReaddir(client, rootHandle, 1, QByteArray(8, 'X'), 0, 65536,
                     false, 2421);
    QVERIFY(page.transportOk);
    QCOMPARE(page.status, kErrBadCookie);
}

void TestNfs::readdirPlusAndErrors()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    QVERIFY(QDir().mkpath(fixture.dir.filePath(QStringLiteral("dirA"))));
    writeFile(fixture.dir.filePath(QStringLiteral("a.txt")), "alpha");
    writeFile(fixture.dir.filePath(QStringLiteral("b.txt")), "beta");
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));
    const QByteArray rootHandle = encodeHandle(1);

    // READDIRPLUS：每项都带属性与句柄
    ReaddirReply page = doReaddir(client, rootHandle, 0, QByteArray(8, '\0'),
                                  0, 65536, true, 2501);
    QVERIFY(page.transportOk);
    QCOMPARE(page.acceptStat, kOk);
    QCOMPARE(page.status, kOk);
    QVERIFY(page.parsed);
    QVERIFY(page.eof);
    QCOMPARE(page.entries.size(), 5);
    for (const DirEntry &entry : page.entries) {
        QVERIFY(entry.attrsPresent);
        QVERIFY(entry.handlePresent);
        QCOMPARE(entry.handle.size(), 8);
        QCOMPARE(decodeHandle(entry.handle), entry.fileid);
        QCOMPARE(entry.attrs.fileid, entry.fileid);
        if (QString::fromUtf8(entry.name) == QLatin1String("a.txt")) {
            QCOMPARE(entry.attrs.type, quint32(1));
            QCOMPARE(entry.attrs.size, quint64(5));
        }
        if (QString::fromUtf8(entry.name) == QLatin1String("dirA")) {
            QCOMPARE(entry.attrs.type, quint32(2));
        }
    }

    // 文件句柄上的 READDIR：NOTDIR，带属性
    const LookupReply file =
        doLookup(client, rootHandle, QByteArrayLiteral("a.txt"), 2502);
    QCOMPARE(file.status, kOk);
    page = doReaddir(client, file.handle, 0, QByteArray(8, '\0'), 0, 65536,
                     false, 2503);
    QVERIFY(page.transportOk);
    QCOMPARE(page.status, kErrNotDir);
    QVERIFY(page.attrsPresent);

    // stale 句柄
    page = doReaddir(client, encodeHandle(555555), 0, QByteArray(8, '\0'),
                     0, 65536, false, 2504);
    QCOMPARE(page.status, kErrStale);
    QVERIFY(!page.attrsPresent);

    // 长度错误的句柄
    page = doReaddir(client, QByteArray("hand"), 0, QByteArray(8, '\0'),
                     0, 65536, false, 2505);
    QCOMPARE(page.status, kErrBadHandle);
}

void TestNfs::statusProcedures()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    writeFile(fixture.dir.filePath(QStringLiteral("file.txt")), "0123456789");
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));
    const QByteArray rootHandle = encodeHandle(1);
    const LookupReply file =
        doLookup(client, rootHandle, QByteArrayLiteral("file.txt"), 2601);
    QCOMPARE(file.status, kOk);

    // FSSTAT
    XdrWriter args;
    args.opaque(rootHandle);
    RpcReply reply = client.call(2602, kNfsProgram, 3, 18, args.data());
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, kOk);
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), kOk);
        QVERIFY(parsePostAttr(in));
        const quint64 total = in.u64();
        const quint64 freeBytes = in.u64();
        const quint64 availBytes = in.u64();
        QCOMPARE(availBytes, freeBytes);
        QVERIFY(freeBytes <= total);
        QCOMPARE(in.u64(), quint64(0));   // tfiles
        QCOMPARE(in.u64(), quint64(0));   // ffiles
        QCOMPARE(in.u64(), quint64(0));   // afiles
        QCOMPARE(in.u32(), quint32(0));   // invarsec
        QVERIFY(in.ok());
        QVERIFY(total > 0);
    }

    // FSINFO
    reply = client.call(2603, kNfsProgram, 3, 19, args.data());
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, kOk);
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), kOk);
        QVERIFY(parsePostAttr(in));
        QCOMPARE(in.u32(), quint32(65536));   // rtmax
        QCOMPARE(in.u32(), quint32(65536));   // rtpref
        QCOMPARE(in.u32(), quint32(4096));    // rtmult
        QCOMPARE(in.u32(), quint32(65536));   // wtmax
        QCOMPARE(in.u32(), quint32(65536));   // wtpref
        QCOMPARE(in.u32(), quint32(4096));    // wtmult
        QCOMPARE(in.u32(), quint32(4096));    // dtpref
        QCOMPARE(in.u64(), quint64(0x7FFFFFFFFFFFFFFFull));   // maxfilesize
        QCOMPARE(in.u32(), quint32(0));       // time_delta.seconds
        QCOMPARE(in.u32(), quint32(1));       // time_delta.nseconds
        QCOMPARE(in.u32(), quint32(0x0008));  // FSF3_HOMOGENEOUS
        QVERIFY(in.ok());
    }

    // PATHCONF：目录与文件都可查询
    for (const QByteArray &handle : {rootHandle, file.handle}) {
        XdrWriter pathArgs;
        pathArgs.opaque(handle);
        reply = client.call(2604, kNfsProgram, 3, 20, pathArgs.data());
        QVERIFY(reply.ok);
        QCOMPARE(reply.acceptStat, kOk);
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), kOk);
        QVERIFY(parsePostAttr(in));
        QCOMPARE(in.u32(), quint32(1));     // linkmax
        QCOMPARE(in.u32(), quint32(255));   // name_max
        QCOMPARE(in.boolean(), true);       // no_trunc
        QCOMPARE(in.boolean(), true);       // chown_restricted
        QCOMPARE(in.boolean(), true);       // case_insensitive
        QCOMPARE(in.boolean(), true);       // case_preserving
        QVERIFY(in.ok());
    }

    // 写类过程一律 PROC_UNAVAIL（SETATTR/MKDIR/CREATE/…）
    for (quint32 proc : {quint32(2), quint32(5), quint32(8), quint32(9),
                         quint32(21)}) {
        reply = client.call(2605 + proc, kNfsProgram, 3, proc, args.data());
        QVERIFY(reply.ok);
        QCOMPARE(reply.acceptStat, quint32(3));
    }

    // 状态类过程遇到 stale 句柄
    XdrWriter staleArgs;
    staleArgs.opaque(encodeHandle(666666));
    for (quint32 proc : {quint32(18), quint32(19), quint32(20)}) {
        reply = client.call(2700 + proc, kNfsProgram, 3, proc, staleArgs.data());
        QVERIFY(reply.ok);
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), kErrStale);
        QCOMPARE(in.boolean(), false);
    }
}

void TestNfs::rpcFramingAndProtocolErrors()
{
    NfsFixture fixture;
    QVERIFY(fixture.dir.isValid());
    QVERIFY2(fixture.start(), qPrintable(fixture.server.errorString()));

    RpcClient client;
    QVERIFY(client.connectTo(fixture.port));

    // 一次写入两条调用：应答按顺序、xid 对应
    QByteArray twoCalls = frameRecord(makeCall(3001, kNfsProgram, 3, 0));
    twoCalls += frameRecord(makeCall(3002, kNfsProgram, 3, 0));
    QVERIFY(client.sendRaw(twoCalls));
    RpcReply first = client.readReply();
    RpcReply second = client.readReply();
    QVERIFY2(first.ok, qPrintable(first.error));
    QVERIFY2(second.ok, qPrintable(second.error));
    QCOMPARE(first.xid, quint32(3001));
    QCOMPARE(second.xid, quint32(3002));

    // 记录分片：前半段（last=false）不触发处理，补后半段后才应答
    const QByteArray fullCall = makeCall(3003, kNfsProgram, 3, 0);
    const int split = 12;
    QVERIFY(client.sendRaw(frameRecord(fullCall.left(split), false)));
    QTest::qWait(100);
    QCOMPARE(client.bufferedBytes(), 0);
    QCOMPARE(client.socket().bytesAvailable(), qint64(0));
    QVERIFY(client.sendRaw(frameRecord(fullCall.mid(split), true)));
    RpcReply reply = client.readReply();
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.xid, quint32(3003));
    QCOMPARE(reply.acceptStat, kOk);

    // 分片可以一次写入
    const QByteArray otherCall = makeCall(3004, kNfsProgram, 3, 0);
    const QByteArray fragments = frameRecord(otherCall.left(8), false)
                                 + frameRecord(otherCall.mid(8), true);
    QVERIFY(client.sendRaw(fragments));
    reply = client.readReply();
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.xid, quint32(3004));

    // RPC 版本不是 2：MSG_DENIED + RPC_MISMATCH（低/高版本均为 2）
    QByteArray badRpc = makeCall(3005, kNfsProgram, 3, 0, {}, 1, 0);
    QVERIFY(client.sendRaw(frameRecord(badRpc)));
    reply = client.readReply();
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.xid, quint32(3005));
    QCOMPARE(reply.messageType, quint32(1));   // MSG_DENIED
    QCOMPARE(reply.replyStat, quint32(1));
    QCOMPARE(reply.rejectStat, quint32(0));    // RPC_MISMATCH
    QCOMPARE(reply.low, quint32(2));
    QCOMPARE(reply.high, quint32(2));

    // 未知程序：PROG_UNAVAIL
    reply = client.call(3006, 999999, 3, 0);
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, quint32(1));
    QVERIFY(reply.payload.isEmpty());

    // 程序版本不符：PROG_MISMATCH，(3,3)
    reply = client.call(3007, kNfsProgram, 2, 0);
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, quint32(2));
    {
        XdrReader in(reply.payload);
        QCOMPARE(in.u32(), quint32(3));
        QCOMPARE(in.u32(), quint32(3));
    }

    // 未实现的 NFS 过程：PROC_UNAVAIL
    reply = client.call(3008, kNfsProgram, 3, 7);
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, quint32(3));

    // 不是 CALL 的消息被忽略，随后正常的调用仍能收到应答
    QByteArray notCall = frameRecord(makeCall(3009, kNfsProgram, 3, 0, {}, 2, 1));
    QVERIFY(client.sendRaw(notCall));
    QVERIFY(client.sendCall(3010, kNfsProgram, 3, 0));
    reply = client.readReply();
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.xid, quint32(3010));

    // 截断的调用头被忽略，连接保持可用
    XdrWriter truncated;
    truncated.u32(3011);
    truncated.u32(0);
    truncated.u32(2);   // 缺少 program/version/proc
    QVERIFY(client.sendRaw(frameRecord(truncated.data())));
    QVERIFY(client.sendCall(3012, kNfsProgram, 3, 0));
    reply = client.readReply();
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.xid, quint32(3012));

    // 超过 16MB 的记录长度：服务端直接断开连接
    RpcClient oversized;
    QVERIFY(oversized.connectTo(fixture.port));
    QByteArray header(4, '\0');
    qToBigEndian<quint32>(0x80000000u | (16u * 1024u * 1024u + 1u), header.data());
    QVERIFY(oversized.sendRaw(header));
    QVERIFY(waitUntil([&oversized] {
        return oversized.socket().state() == QAbstractSocket::UnconnectedState;
    }));

    // 原连接不受影响
    reply = client.call(3013, kNfsProgram, 3, 0);
    QVERIFY(reply.ok);
    QCOMPARE(reply.acceptStat, kOk);
}

void TestNfs::startFailures()
{
    const quint16 port = freeTcpPort();
    QVERIFY(port != 0);

    {
        NfsServer server;
        QSignalSpy errorSpy(&server, &ServerService::errorOccurred);
        QVERIFY(!server.start(QJsonObject{{QStringLiteral("port"), int(port)}}));
        QVERIFY(server.errorString().contains(QStringLiteral("根目录")));
        QCOMPARE(errorSpy.count(), 1);
    }
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        NfsServer server;
        QVERIFY(!server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                          {QStringLiteral("port"), 0}}));
        QVERIFY(server.errorString().contains(QStringLiteral("端口")));
    }
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::Any, port));
        NfsServer server;
        QVERIFY(!server.start(QJsonObject{{QStringLiteral("root"), dir.path()},
                                          {QStringLiteral("port"), int(port)}}));
        QVERIFY2(server.errorString().contains(QStringLiteral("无法监听端口")),
                 qPrintable(server.errorString()));
    }
}

QTEST_GUILESS_MAIN(TestNfs)
#include "tst_nfs.moc"
