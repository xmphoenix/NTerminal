#include "services/nfsserver.h"
#include "services/serviceutil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QStorageInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// XDR 编解码（RFC 4506）：NFS/MOUNT 协议都使用大端 4 字节对齐的编码
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
        const int pad = (4 - (data.size() % 4)) % 4;
        m_data.append(QByteArray(pad, '\0'));
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

    bool boolean() { return u32() != 0; }

    QByteArray fixedOpaque(int size)
    {
        if (size < 0 || m_pos + size > m_data.size()) {
            m_ok = false;
            m_pos = m_data.size();
            return {};
        }
        const QByteArray out = m_data.mid(m_pos, size);
        m_pos += size;
        const int pad = (4 - (size % 4)) % 4;
        m_pos = qMin(m_pos + pad, m_data.size());
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

    void skipOpaque()
    {
        const quint32 size = u32();
        fixedOpaque(int(size));
    }

private:
    QByteArray m_data;
    int m_pos = 0;
    bool m_ok = true;
};

// ---- NFSv3 常量 ----
constexpr quint32 kProgramNfs = 100003;
constexpr quint32 kProgramMount = 100005;
constexpr quint32 kVersion3 = 3;

constexpr quint32 kOk = 0;
constexpr quint32 kErrNoEnt = 2;
constexpr quint32 kErrIo = 5;
constexpr quint32 kErrAccess = 13;
constexpr quint32 kErrNotDir = 20;
constexpr quint32 kErrIsDir = 21;
constexpr quint32 kErrNameTooLong = 63;
constexpr quint32 kErrRofs = 30;
constexpr quint32 kErrStale = 70;
constexpr quint32 kErrBadHandle = 10001;
constexpr quint32 kErrBadCookie = 10003;

constexpr quint32 kAccessRead = 0x0001;
constexpr quint32 kAccessLookup = 0x0002;
constexpr quint32 kAccessModify = 0x0004;
constexpr quint32 kAccessExtend = 0x0008;
constexpr quint32 kAccessDelete = 0x0010;
constexpr quint32 kAccessExecute = 0x0020;

quint64 decodeHandle(const QByteArray &handle)
{
    if (handle.size() != 8) {
        return 0;
    }
    return qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(handle.constData()));
}

QByteArray encodeHandle(quint64 id)
{
    QByteArray handle(8, '\0');
    qToBigEndian<quint64>(id, handle.data());
    return handle;
}

void writeTime(XdrWriter &out, const QDateTime &time)
{
    out.u32(quint32(time.isValid() ? time.toSecsSinceEpoch() : 0));
    out.u32(0);
}

// ==== RPC 应答外壳 ====
QByteArray acceptedReply(quint32 xid, quint32 acceptStat)
{
    XdrWriter writer;
    writer.u32(xid);
    writer.u32(1);   // REPLY
    writer.u32(0);   // MSG_ACCEPTED
    writer.u32(0);   // verf flavor = AUTH_NULL
    writer.u32(0);   // verf length
    writer.u32(acceptStat);
    return writer.data();
}

} // namespace

// ---------------------------------------------------------------------------
// NFS/MOUNT 协议处理（含文件句柄表）
// ---------------------------------------------------------------------------
class NfsProtocol
{
public:
    explicit NfsProtocol(QString root)
        : m_root(std::move(root))
    {
        // 根目录的句柄固定为 1
        handleForPath(m_root);
    }

    QByteArray handle(const QByteArray &message);

private:
    void getattr(XdrReader &in, XdrWriter &out);
    void lookup(XdrReader &in, XdrWriter &out);
    void access(XdrReader &in, XdrWriter &out);
    void read(XdrReader &in, XdrWriter &out);
    void readdir(XdrReader &in, XdrWriter &out, bool plus);
    void fsstat(XdrReader &in, XdrWriter &out);
    void fsinfo(XdrReader &in, XdrWriter &out);
    void pathconf(XdrReader &in, XdrWriter &out);

    bool nfs3(quint32 proc, XdrReader &in, XdrWriter &out);
    bool mount3(quint32 proc, XdrReader &in, XdrWriter &out);

    quint32 resolveHandle(XdrReader &in, QString *path, quint64 *id);
    quint64 handleForPath(const QString &path);
    QString parentOf(const QString &path) const;
    void writeFattr(XdrWriter &out, const QFileInfo &info, quint64 id) const;
    void writePostAttr(XdrWriter &out, const QFileInfo &info, quint64 id,
                       bool include = true) const;

    QString m_root;
    QHash<quint64, QString> m_paths;      // 句柄 -> 路径
    QHash<QString, quint64> m_handles;    // 路径 -> 句柄
    quint64 m_nextHandle = 1;
};

quint64 NfsProtocol::handleForPath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    const auto existing = m_handles.constFind(clean);
    if (existing != m_handles.constEnd()) {
        return existing.value();
    }
    const quint64 id = m_nextHandle++;
    m_handles.insert(clean, id);
    m_paths.insert(id, clean);
    return id;
}

QString NfsProtocol::parentOf(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    if (clean.compare(m_root, Qt::CaseInsensitive) == 0) {
        return m_root;
    }
    const QString parent = QDir::cleanPath(QFileInfo(clean).path());
    return ServiceUtil::pathInside(m_root, parent) ? parent : m_root;
}

void NfsProtocol::writeFattr(XdrWriter &out, const QFileInfo &info, quint64 id) const
{
    const bool isDir = info.isDir();
    const quint64 size = isDir ? 0 : quint64(info.size());
    out.u32(isDir ? 2 : 1);                          // NF3DIR / NF3REG
    out.u32(isDir ? 0755u : 0644u);                  // mode
    out.u32(isDir ? 2u : 1u);                        // nlink
    out.u32(0);                                      // uid
    out.u32(0);                                      // gid
    out.u64(size);                                   // size
    out.u64(size);                                   // used
    out.u32(0);                                      // rdev.major
    out.u32(0);                                      // rdev.minor
    out.u64(0x4E54);                                 // fsid
    out.u64(id);                                     // fileid
    const QDateTime modified = info.lastModified();
    writeTime(out, modified);
    writeTime(out, modified);
    writeTime(out, modified);
}

void NfsProtocol::writePostAttr(XdrWriter &out, const QFileInfo &info, quint64 id,
                                bool include) const
{
    if (!include || !info.exists()) {
        out.boolean(false);
        return;
    }
    out.boolean(true);
    writeFattr(out, info, id);
}

quint32 NfsProtocol::resolveHandle(XdrReader &in, QString *path, quint64 *id)
{
    const QByteArray handle = in.opaque();
    if (!in.ok() || handle.size() != 8) {
        return kErrBadHandle;
    }
    const quint64 value = decodeHandle(handle);
    const auto it = m_paths.constFind(value);
    if (it == m_paths.constEnd()) {
        return kErrStale;
    }
    *id = value;
    *path = it.value();
    return kOk;
}

// ---------------------------------------------------------------------------
// RPC 入口
// ---------------------------------------------------------------------------
QByteArray NfsProtocol::handle(const QByteArray &message)
{
    XdrReader in(message);
    const quint32 xid = in.u32();
    const quint32 messageType = in.u32();
    if (!in.ok() || messageType != 0) {
        return {};   // 不是 CALL（或消息不完整），忽略
    }
    const quint32 rpcVersion = in.u32();
    if (!in.ok()) {
        return {};
    }
    if (rpcVersion != 2) {
        // MSG_DENIED + RPC_MISMATCH
        XdrWriter out;
        out.u32(xid);
        out.u32(1);
        out.u32(1);
        out.u32(0);
        out.u32(2);
        out.u32(2);
        return out.data();
    }
    const quint32 program = in.u32();
    const quint32 version = in.u32();
    const quint32 proc = in.u32();
    in.u32();                 // 凭据 flavor
    const quint32 credLength = in.u32();
    in.fixedOpaque(int(credLength));
    in.u32();                 // 校验 flavor
    const quint32 verfLength = in.u32();
    in.fixedOpaque(int(verfLength));
    if (!in.ok()) {
        return {};
    }

    if (program != kProgramNfs && program != kProgramMount) {
        return acceptedReply(xid, 1);   // PROG_UNAVAIL
    }
    if (version != kVersion3) {
        XdrWriter out;
        out.u32(xid);
        out.u32(1);
        out.u32(0);
        out.u32(0);
        out.u32(0);
        out.u32(2);   // PROG_MISMATCH
        out.u32(kVersion3);
        out.u32(kVersion3);
        return out.data();
    }

    XdrWriter result;
    const bool supported = program == kProgramNfs ? nfs3(proc, in, result)
                                                  : mount3(proc, in, result);
    if (!supported) {
        return acceptedReply(xid, 3);   // PROC_UNAVAIL
    }
    QByteArray reply = acceptedReply(xid, 0);   // SUCCESS
    reply += result.data();
    return reply;
}

bool NfsProtocol::nfs3(quint32 proc, XdrReader &in, XdrWriter &out)
{
    switch (proc) {
    case 0:    // NULL
        return true;
    case 1:
        getattr(in, out);
        return true;
    case 3:
        lookup(in, out);
        return true;
    case 4:
        access(in, out);
        return true;
    case 6:
        read(in, out);
        return true;
    case 14:
        readdir(in, out, false);
        return true;
    case 17:
        readdir(in, out, true);
        return true;
    case 18:
        fsstat(in, out);
        return true;
    case 19:
        fsinfo(in, out);
        return true;
    case 20:
        pathconf(in, out);
        return true;
    default:
        return false;   // 写类过程：只读服务不提供
    }
}

bool NfsProtocol::mount3(quint32 proc, XdrReader &in, XdrWriter &out)
{
    switch (proc) {
    case 0:    // NULL
        return true;
    case 1: {  // MNT：只导出根目录
        const QString path = QString::fromUtf8(in.opaque());
        if (!in.ok() || !(path.isEmpty() || path == QLatin1String("/"))) {
            out.u32(2);   // MNT3ERR_NOENT
            return true;
        }
        out.u32(0);   // MNT3_OK
        out.opaque(encodeHandle(1));
        out.u32(1);   // auth_flavors 数量
        out.u32(1);   // AUTH_UNIX
        return true;
    }
    case 2:    // DUMP：没有其他导出项
        out.boolean(false);
        return true;
    case 3:    // UMNT
        in.skipOpaque();
        return true;
    case 4:    // UMNTALL
        return true;
    case 5:    // EXPORT
        out.boolean(true);
        out.opaque("/");
        out.boolean(false);   // groups
        out.boolean(false);   // 下一个导出项
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// NFSv3 过程
// ---------------------------------------------------------------------------
void NfsProtocol::getattr(XdrReader &in, XdrWriter &out)
{
    QString path;
    quint64 id = 0;
    const quint32 status = resolveHandle(in, &path, &id);
    if (status != kOk) {
        out.u32(status);
        return;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        out.u32(kErrStale);
        return;
    }
    out.u32(kOk);
    writeFattr(out, info, id);
}

void NfsProtocol::lookup(XdrReader &in, XdrWriter &out)
{
    QString dirPath;
    quint64 dirId = 0;
    const QByteArray handleData = in.opaque();
    const QByteArray nameBytes = in.opaque();
    if (!in.ok() || handleData.size() != 8) {
        out.u32(kErrBadHandle);
        out.boolean(false);
        return;
    }
    const quint64 dirHandle = decodeHandle(handleData);
    if (!m_paths.contains(dirHandle)) {
        out.u32(kErrStale);
        out.boolean(false);
        return;
    }
    dirId = dirHandle;
    dirPath = m_paths.value(dirHandle);
    const QFileInfo dirInfo(dirPath);

    const QString name = QString::fromUtf8(nameBytes);
    if (name.toUtf8().size() > 255) {
        out.u32(kErrNameTooLong);
        writePostAttr(out, dirInfo, dirId);
        return;
    }

    QString childPath;
    if (name == QLatin1String(".")) {
        childPath = dirPath;
    } else if (name == QLatin1String("..")) {
        childPath = parentOf(dirPath);
    } else if (!ServiceUtil::safeName(name)) {
        out.u32(kErrNoEnt);
        writePostAttr(out, dirInfo, dirId);
        return;
    } else {
        childPath = QDir::cleanPath(dirPath + QLatin1Char('/') + name);
    }
    const QFileInfo childInfo(childPath);
    if (!ServiceUtil::pathInside(m_root, childPath) || !childInfo.exists()) {
        out.u32(kErrNoEnt);
        writePostAttr(out, dirInfo, dirId);
        return;
    }

    const quint64 childId = handleForPath(childPath);
    out.u32(kOk);
    out.opaque(encodeHandle(childId));
    writePostAttr(out, childInfo, childId);
    writePostAttr(out, dirInfo, dirId);
}

void NfsProtocol::access(XdrReader &in, XdrWriter &out)
{
    QString path;
    quint64 id = 0;
    const quint32 status = resolveHandle(in, &path, &id);
    const quint32 requested = in.u32();
    const QFileInfo info(path);
    if (status != kOk || !info.exists()) {
        out.u32(status != kOk ? status : kErrStale);
        writePostAttr(out, QFileInfo(), 0, false);
        return;
    }
    quint32 allowed = 0;
    if (info.isDir()) {
        allowed = kAccessRead | kAccessLookup;
    } else {
        allowed = kAccessRead | kAccessExecute;
    }
    out.u32(kOk);
    writePostAttr(out, info, id);
    out.u32(requested & allowed);
}

void NfsProtocol::read(XdrReader &in, XdrWriter &out)
{
    QString path;
    quint64 id = 0;
    const quint32 status = resolveHandle(in, &path, &id);
    const quint64 offset = in.u64();
    const quint32 count = in.u32();
    const QFileInfo info(path);
    if (status != kOk || !info.exists()) {
        out.u32(status != kOk ? status : kErrStale);
        writePostAttr(out, QFileInfo(), 0, false);
        return;
    }
    if (info.isDir()) {
        out.u32(kErrIsDir);
        writePostAttr(out, info, id);
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.u32(kErrIo);
        writePostAttr(out, info, id);
        return;
    }
    if (!file.seek(qint64(offset))) {
        out.u32(kErrIo);
        writePostAttr(out, info, id);
        return;
    }
    const QByteArray data = file.read(qMin<quint32>(count, 65536));
    const bool eof = offset + quint64(data.size()) >= quint64(file.size());
    out.u32(kOk);
    writePostAttr(out, info, id);
    out.u32(quint32(data.size()));
    out.boolean(eof);
    out.opaque(data);
}

void NfsProtocol::readdir(XdrReader &in, XdrWriter &out, bool plus)
{
    QString dirPath;
    quint64 dirId = 0;
    const quint32 status = resolveHandle(in, &dirPath, &dirId);
    const quint64 cookie = in.u64();
    const QByteArray cookieVerf = in.fixedOpaque(8);
    in.u32();   // dircount
    const quint32 maxcount = in.u32();

    const QFileInfo dirInfo(dirPath);
    if (status != kOk || !dirInfo.exists()) {
        out.u32(status != kOk ? status : kErrStale);
        writePostAttr(out, QFileInfo(), 0, false);
        return;
    }
    if (!dirInfo.isDir()) {
        out.u32(kErrNotDir);
        writePostAttr(out, dirInfo, dirId);
        return;
    }
    if (cookie != 0 && !cookieVerf.isEmpty()
        && cookieVerf != QByteArray(8, '\0')) {
        out.u32(kErrBadCookie);
        writePostAttr(out, dirInfo, dirId);
        return;
    }

    struct Entry {
        QString name;
        QString path;
        quint64 id = 0;
    };
    QList<Entry> entries;
    entries.append({QStringLiteral("."), dirPath, dirId});
    const QString parentPath = parentOf(dirPath);
    entries.append({QStringLiteral(".."), parentPath, handleForPath(parentPath)});

    const QFileInfoList children = QDir(dirPath).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    entries.reserve(children.size() + 2);
    for (const QFileInfo &child : children) {
        entries.append({child.fileName(), child.filePath(), handleForPath(child.filePath())});
    }

    const int start = cookie == 0 ? 0 : int(qMin<quint64>(cookie, quint64(entries.size())));
    out.u32(kOk);
    writePostAttr(out, dirInfo, dirId);
    out.fixedOpaque(QByteArray(8, '\0'));   // cookieverf

    int nextIndex = start;
    int added = 0;
    for (int index = start; index < entries.size(); ++index) {
        const Entry &entry = entries.at(index);
        XdrWriter encoded;
        encoded.boolean(true);                 // entries_follow
        encoded.u64(entry.id);                 // fileid
        encoded.opaque(entry.name.toUtf8());
        encoded.u64(quint64(index) + 1);       // cookie
        if (plus) {
            const QFileInfo entryInfo(entry.path);
            writePostAttr(encoded, entryInfo, entry.id);
            encoded.boolean(true);             // name_handle
            encoded.opaque(encodeHandle(entry.id));
        }
        if (added > 0 && out.data().size() + encoded.data().size() + 8 > int(maxcount)) {
            break;
        }
        out.raw(encoded.data());
        ++added;
        nextIndex = index + 1;
    }
    out.boolean(false);                        // 列表结束
    out.boolean(nextIndex >= entries.size());  // eof
}

void NfsProtocol::fsstat(XdrReader &in, XdrWriter &out)
{
    QString path;
    quint64 id = 0;
    const quint32 status = resolveHandle(in, &path, &id);
    const QFileInfo info(path);
    if (status != kOk || !info.exists()) {
        out.u32(status != kOk ? status : kErrStale);
        writePostAttr(out, QFileInfo(), 0, false);
        return;
    }
    QStorageInfo storage(path);
    quint64 total = 0;
    quint64 free = 0;
    if (storage.isValid()) {
        total = quint64(storage.bytesTotal());
        free = quint64(storage.bytesAvailable());
    }
    out.u32(kOk);
    writePostAttr(out, info, id);
    out.u64(total);   // tbytes
    out.u64(free);    // fbytes
    out.u64(free);    // abytes
    out.u64(0);       // tfiles
    out.u64(0);       // ffiles
    out.u64(0);       // afiles
    out.u32(0);       // invarsec
}

void NfsProtocol::fsinfo(XdrReader &in, XdrWriter &out)
{
    QString path;
    quint64 id = 0;
    const quint32 status = resolveHandle(in, &path, &id);
    const QFileInfo info(path);
    if (status != kOk || !info.exists()) {
        out.u32(status != kOk ? status : kErrStale);
        writePostAttr(out, QFileInfo(), 0, false);
        return;
    }
    out.u32(kOk);
    writePostAttr(out, info, id);
    out.u32(65536);    // rtmax
    out.u32(65536);    // rtpref
    out.u32(4096);     // rtmult
    out.u32(65536);    // wtmax
    out.u32(65536);    // wtpref
    out.u32(4096);     // wtmult
    out.u32(4096);     // dtpref
    out.u64(0x7FFFFFFFFFFFFFFFull);   // maxfilesize
    out.u32(0);        // time_delta.seconds
    out.u32(1);        // time_delta.nseconds
    out.u32(0x0008);   // properties: FSF3_HOMOGENEOUS
}

void NfsProtocol::pathconf(XdrReader &in, XdrWriter &out)
{
    QString path;
    quint64 id = 0;
    const quint32 status = resolveHandle(in, &path, &id);
    const QFileInfo info(path);
    if (status != kOk || !info.exists()) {
        out.u32(status != kOk ? status : kErrStale);
        writePostAttr(out, QFileInfo(), 0, false);
        return;
    }
    out.u32(kOk);
    writePostAttr(out, info, id);
    out.u32(1);        // linkmax
    out.u32(255);      // name_max
    out.boolean(true);  // no_trunc
    out.boolean(true);  // chown_restricted
    out.boolean(true);  // case_insensitive
    out.boolean(true);  // case_preserving
}

// ---------------------------------------------------------------------------
// NfsServer
// ---------------------------------------------------------------------------
NfsServer::NfsServer(QObject *parent)
    : ServerService(parent)
{
}

NfsServer::~NfsServer()
{
    delete m_protocol;
    m_protocol = nullptr;
}

QString NfsServer::displayName() const
{
    return tr("NFS 服务器");
}

QString NfsServer::description() const
{
    return tr("以只读方式向 Linux 客户端导出根目录（NFSv3 / TCP）");
}

QJsonObject NfsServer::defaultConfig() const
{
    QJsonObject config;
    config.insert(QStringLiteral("port"), defaultPort());
    config.insert(QStringLiteral("root"), QString());
    return config;
}

bool NfsServer::startServer()
{
    const QString root = ServiceUtil::canonicalRoot(configString(QStringLiteral("root")));
    if (root.isEmpty()) {
        setError(tr("根目录不存在或不是文件夹"));
        return false;
    }
    const int listenPort = port();
    if (listenPort < 1 || listenPort > 65535) {
        setError(tr("端口无效"));
        return false;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &NfsServer::onNewConnection);
    if (!m_server->listen(QHostAddress::Any, quint16(listenPort))) {
        setError(tr("无法监听端口 %1：%2").arg(listenPort).arg(m_server->errorString()));
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    delete m_protocol;
    m_protocol = new NfsProtocol(root);
    log(tr("NFS 服务已启动（只读）：根目录 %1，端口 %2").arg(root).arg(listenPort));
    return true;
}

void NfsServer::stopServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    const QList<QTcpSocket *> sockets = m_connections.keys();
    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_connections.clear();
    delete m_protocol;
    m_protocol = nullptr;
}

void NfsServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_connections.insert(socket, Connection());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            dropConnection(socket);
        });
    }
}

void NfsServer::dropConnection(QTcpSocket *socket)
{
    m_connections.remove(socket);
    socket->deleteLater();
}

void NfsServer::onReadyRead(QTcpSocket *socket)
{
    if (!m_protocol || !m_connections.contains(socket)) {
        return;
    }
    Connection &connection = m_connections[socket];
    connection.buffer += socket->readAll();

    // RPC over TCP：每条消息前是 4 字节记录标记（最高位=最后一片）
    while (connection.buffer.size() >= 4) {
        const quint32 header = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(connection.buffer.constData()));
        const quint32 length = header & 0x7FFFFFFFu;
        if (length > 16u * 1024u * 1024u) {
            socket->abort();
            return;
        }
        if (quint32(connection.buffer.size()) < 4 + length) {
            break;
        }
        connection.message.append(connection.buffer.mid(4, int(length)));
        connection.buffer.remove(0, int(4 + length));
        if ((header & 0x80000000u) == 0) {
            continue;   // 还有后续分片
        }
        const QByteArray reply = m_protocol->handle(connection.message);
        connection.message.clear();
        if (reply.isEmpty()) {
            continue;
        }
        QByteArray framed(4, '\0');
        qToBigEndian<quint32>(0x80000000u | quint32(reply.size()), framed.data());
        framed.append(reply);
        socket->write(framed);
    }
}
