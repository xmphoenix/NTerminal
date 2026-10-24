#include "services/ftpserver.h"
#include "services/serviceutil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <functional>
#include <utility>

namespace {

// Unix 风格的 LIST 行，curl/资源管理器都能识别
QByteArray listLine(const QFileInfo &info)
{
    const bool isDir = info.isDir();
    const QString mode = isDir ? QStringLiteral("drwxr-xr-x") : QStringLiteral("-rw-r--r--");
    const QString owner = isDir ? QStringLiteral("ftp") : QStringLiteral("ftp");
    const QDateTime modified = info.lastModified().isValid() ? info.lastModified()
                                                             : QDateTime::currentDateTime();
    return QStringLiteral("%1 %2 %3 %4 %5 %6 %7\r\n")
        .arg(mode)
        .arg(1, 8)
        .arg(owner, -8)
        .arg(owner, -8)
        .arg(isDir ? 4096 : info.size(), 12)
        .arg(modified.toString(QStringLiteral("MMM dd HH:mm")), -12)
        .arg(info.fileName())
        .toUtf8();
}

} // namespace

// ---------------------------------------------------------------------------
// 单条控制连接
// ---------------------------------------------------------------------------
class FtpSession : public QObject
{
    Q_OBJECT

public:
    FtpSession(QString root, const QJsonObject &config, QTcpSocket *control,
               std::function<void(const QString &)> logger, QObject *parent)
        : QObject(parent)
        , m_root(std::move(root))
        , m_allowAnonymous(config.value(QStringLiteral("allowAnonymous")).toBool())
        , m_user(config.value(QStringLiteral("user")).toString())
        , m_password(config.value(QStringLiteral("password")).toString())
        , m_allowWrite(config.value(QStringLiteral("allowWrite")).toBool())
        , m_control(control)
        , m_log(std::move(logger))
    {
        m_control->setParent(this);
        connect(m_control, &QTcpSocket::readyRead, this, &FtpSession::onReadyRead);
        connect(m_control, &QTcpSocket::disconnected, this, &FtpSession::deleteLater);
        reply(QStringLiteral("220 NTerminal FTP 服务就绪"));
    }

private:
    enum Transfer { NoTransfer, ListFiles, ListNames, Download, Upload };

    void reply(const QString &line)
    {
        m_control->write(line.toUtf8() + "\r\n");
    }

    QString peerText() const
    {
        return QStringLiteral("%1:%2").arg(m_control->peerAddress().toString())
            .arg(m_control->peerPort());
    }

    // 客户端路径 -> 根目录内的绝对路径（allowMissing 用于上传/新建目录）
    QString resolve(const QString &argument, bool allowMissing = false) const
    {
        QString rel = argument;
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (rel.isEmpty()) {
            rel = m_cwd;
        } else if (rel.startsWith(QLatin1Char('/'))) {
            rel = rel.mid(1);
        } else if (!m_cwd.isEmpty()) {
            rel = m_cwd + QLatin1Char('/') + rel;
        }
        rel = QDir::cleanPath(rel);
        if (rel == QLatin1String(".")) {
            rel.clear();
        }
        if (rel == QLatin1String("..") || rel.startsWith(QLatin1String("../"))) {
            return QString();
        }
        return ServiceUtil::safeJoin(m_root, rel, allowMissing);
    }

    QString relativeOf(const QString &absolute) const
    {
        if (absolute == m_root) {
            return QString();
        }
        return QDir(m_root).relativeFilePath(absolute);
    }

    void onReadyRead()
    {
        m_buffer += m_control->readAll();
        if (m_buffer.size() > 8192 && !m_buffer.contains('\n')) {
            reply(QStringLiteral("500 命令行过长"));
            m_buffer.clear();
            return;
        }
        int newline = m_buffer.indexOf('\n');
        while (newline >= 0) {
            QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            handleCommand(QString::fromUtf8(line));
            if (!m_control) {
                return;
            }
            newline = m_buffer.indexOf('\n');
        }
    }

    void handleCommand(const QString &line)
    {
        if (line.isEmpty()) {
            return;
        }
        const int space = line.indexOf(QLatin1Char(' '));
        const QString command = (space < 0 ? line : line.left(space)).toUpper();
        const QString argument = space < 0 ? QString() : line.mid(space + 1);

        if (command == QLatin1String("USER")) {
            m_pendingUser = argument;
            reply(QStringLiteral("331 需要密码"));
            return;
        }
        if (command == QLatin1String("PASS")) {
            const bool anonymous = m_allowAnonymous
                                   && (m_pendingUser.compare(QLatin1String("anonymous"),
                                                             Qt::CaseInsensitive) == 0
                                       || m_pendingUser.compare(QLatin1String("ftp"),
                                                                Qt::CaseInsensitive) == 0);
            // 即使允许匿名，也接受配置的账号密码（匿名只是额外放开一个入口）
            const bool account = !m_user.isEmpty() && m_pendingUser == m_user
                                 && argument == m_password;
            if (anonymous || account) {
                m_loggedIn = true;
                m_log(tr("FTP 登录成功：%1（%2）").arg(m_pendingUser, peerText()));
                reply(QStringLiteral("230 登录成功"));
            } else {
                m_log(tr("FTP 登录被拒绝：%1（%2）").arg(m_pendingUser, peerText()));
                reply(QStringLiteral("530 用户名或密码错误"));
            }
            return;
        }
        if (command == QLatin1String("QUIT")) {
            reply(QStringLiteral("221 再见"));
            m_control->disconnectFromHost();
            return;
        }
        if (command == QLatin1String("FEAT")) {
            reply(QStringLiteral("211-Features:"));
            reply(QStringLiteral(" SIZE"));
            reply(QStringLiteral(" MDTM"));
            reply(QStringLiteral(" UTF8"));
            reply(QStringLiteral(" PASV"));
            reply(QStringLiteral(" EPSV"));
            reply(QStringLiteral("211 End"));
            return;
        }
        if (command == QLatin1String("OPTS")) {
            reply(argument.compare(QLatin1String("UTF8 ON"), Qt::CaseInsensitive) == 0
                      ? QStringLiteral("200 UTF8 已启用")
                      : QStringLiteral("501 不支持的选项"));
            return;
        }
        if (!m_loggedIn) {
            reply(QStringLiteral("530 请先登录"));
            return;
        }

        if (command == QLatin1String("SYST")) {
            reply(QStringLiteral("215 UNIX Type: L8"));
        } else if (command == QLatin1String("TYPE")) {
            reply(QStringLiteral("200 传输类型已设置"));
        } else if (command == QLatin1String("MODE") || command == QLatin1String("STRU")) {
            reply(QStringLiteral("200 已接受"));
        } else if (command == QLatin1String("NOOP")) {
            reply(QStringLiteral("200 OK"));
        } else if (command == QLatin1String("PWD") || command == QLatin1String("XPWD")) {
            reply(QStringLiteral("257 \"/%1\"").arg(m_cwd));
        } else if (command == QLatin1String("CWD") || command == QLatin1String("XCWD")) {
            const QString path = resolve(argument);
            if (!path.isEmpty() && QFileInfo(path).isDir()) {
                m_cwd = relativeOf(path);
                reply(QStringLiteral("250 目录已切换"));
            } else {
                reply(QStringLiteral("550 目录不存在"));
            }
        } else if (command == QLatin1String("CDUP") || command == QLatin1String("XCUP")) {
            if (!m_cwd.isEmpty()) {
                const int slash = m_cwd.lastIndexOf(QLatin1Char('/'));
                m_cwd = slash < 0 ? QString() : m_cwd.left(slash);
            }
            reply(QStringLiteral("250 目录已切换"));
        } else if (command == QLatin1String("PASV")) {
            enterPassive();
        } else if (command == QLatin1String("EPSV")) {
            enterExtendedPassive();
        } else if (command == QLatin1String("PORT")) {
            enterActive(argument);
        } else if (command == QLatin1String("LIST") || command == QLatin1String("NLST")) {
            startListing(command == QLatin1String("NLST"), argument);
        } else if (command == QLatin1String("RETR")) {
            startDownload(argument);
        } else if (command == QLatin1String("STOR")) {
            startUpload(argument, false);
        } else if (command == QLatin1String("APPE")) {
            startUpload(argument, true);
        } else if (command == QLatin1String("SIZE")) {
            const QString path = resolve(argument);
            if (!path.isEmpty() && QFileInfo(path).isFile()) {
                reply(QStringLiteral("213 %1").arg(QFileInfo(path).size()));
            } else {
                reply(QStringLiteral("550 文件不存在"));
            }
        } else if (command == QLatin1String("MDTM")) {
            const QString path = resolve(argument);
            if (!path.isEmpty() && QFileInfo(path).exists()) {
                reply(QStringLiteral("213 %1").arg(
                    QFileInfo(path).lastModified().toUTC().toString(QStringLiteral("yyyyMMddHHmmss"))));
            } else {
                reply(QStringLiteral("550 文件不存在"));
            }
        } else if (command == QLatin1String("DELE")) {
            if (!m_allowWrite) {
                reply(QStringLiteral("550 服务为只读"));
                return;
            }
            const QString path = resolve(argument);
            if (!path.isEmpty() && QFileInfo(path).isFile() && QFile::remove(path)) {
                m_log(tr("FTP 删除文件 %1（%2）").arg(argument, peerText()));
                reply(QStringLiteral("250 已删除"));
            } else {
                reply(QStringLiteral("550 无法删除"));
            }
        } else if (command == QLatin1String("MKD") || command == QLatin1String("XMKD")) {
            if (!m_allowWrite) {
                reply(QStringLiteral("550 服务为只读"));
                return;
            }
            const QString path = resolve(argument, true);
            if (!path.isEmpty() && QDir().mkdir(path)) {
                m_log(tr("FTP 新建目录 %1（%2）").arg(argument, peerText()));
                reply(QStringLiteral("257 \"%1\" 已创建").arg(argument));
            } else {
                reply(QStringLiteral("550 无法创建目录"));
            }
        } else if (command == QLatin1String("RMD") || command == QLatin1String("XRMD")) {
            if (!m_allowWrite) {
                reply(QStringLiteral("550 服务为只读"));
                return;
            }
            const QString path = resolve(argument);
            if (!path.isEmpty() && QDir(path).removeRecursively()) {
                m_log(tr("FTP 删除目录 %1（%2）").arg(argument, peerText()));
                reply(QStringLiteral("250 已删除"));
            } else {
                reply(QStringLiteral("550 无法删除"));
            }
        } else if (command == QLatin1String("RNFR")) {
            m_renameFrom = resolve(argument);
            reply(m_renameFrom.isEmpty() ? QStringLiteral("550 文件不存在")
                                         : QStringLiteral("350 请提供新名称"));
        } else if (command == QLatin1String("RNTO")) {
            if (!m_allowWrite || m_renameFrom.isEmpty()) {
                reply(QStringLiteral("550 无法重命名"));
                return;
            }
            const QString to = resolve(argument, true);
            if (!to.isEmpty() && QFile::rename(m_renameFrom, to)) {
                reply(QStringLiteral("250 已重命名"));
            } else {
                reply(QStringLiteral("550 无法重命名"));
            }
            m_renameFrom.clear();
        } else if (command == QLatin1String("ABOR")) {
            reply(QStringLiteral("226 已中止"));
        } else {
            reply(QStringLiteral("502 未实现的命令"));
        }
    }

    // ---- 数据连接 ----
    void enterPassive()
    {
        closePassive();
        m_passive = new QTcpServer(this);
        connect(m_passive, &QTcpServer::newConnection, this, &FtpSession::onPassiveConnection);
        if (!m_passive->listen(QHostAddress::AnyIPv4, 0)) {
            reply(QStringLiteral("425 无法建立数据连接"));
            closePassive();
            return;
        }
        quint32 ip = m_control->localAddress().toIPv4Address();
        if (ip == 0) {
            ip = QHostAddress(QHostAddress::LocalHost).toIPv4Address();
        }
        const quint16 dataPort = m_passive->serverPort();
        reply(QStringLiteral("227 Entering Passive Mode (%1,%2,%3,%4,%5,%6)")
                  .arg((ip >> 24) & 0xFF)
                  .arg((ip >> 16) & 0xFF)
                  .arg((ip >> 8) & 0xFF)
                  .arg(ip & 0xFF)
                  .arg(dataPort / 256)
                  .arg(dataPort % 256));
    }

    void enterExtendedPassive()
    {
        closePassive();
        m_passive = new QTcpServer(this);
        connect(m_passive, &QTcpServer::newConnection, this, &FtpSession::onPassiveConnection);
        if (!m_passive->listen(QHostAddress::AnyIPv4, 0)) {
            reply(QStringLiteral("425 无法建立数据连接"));
            closePassive();
            return;
        }
        reply(QStringLiteral("229 Entering Extended Passive Mode (|||%1|)")
                  .arg(m_passive->serverPort()));
    }

    void enterActive(const QString &argument)
    {
        const QStringList parts = argument.split(QLatin1Char(','));
        if (parts.size() != 6) {
            reply(QStringLiteral("501 参数错误"));
            return;
        }
        const QString host = QStringLiteral("%1.%2.%3.%4")
                                 .arg(parts.at(0), parts.at(1), parts.at(2), parts.at(3));
        const quint16 dataPort = quint16(parts.at(4).toInt() * 256 + parts.at(5).toInt());
        closePassive();
        closeData();   // 丢弃上一轮的数据连接与未完成传输
        // 主动模式同样接上传输收尾逻辑，否则下载完不会回 226、上传也没人读数据
        auto *socket = new QTcpSocket(this);
        attachDataSocket(socket);
        connect(socket, &QTcpSocket::connected, this, &FtpSession::startPendingTransfer);
        socket->connectToHost(host, dataPort);
        reply(QStringLiteral("200 PORT 命令成功"));
    }

    void onPassiveConnection()
    {
        if (!m_passive) {
            return;
        }
        QTcpSocket *socket = m_passive->nextPendingConnection();
        if (!socket) {
            return;
        }
        closePassive();
        attachDataSocket(socket);
        if (m_transfer != NoTransfer) {
            startPendingTransfer();
        }
    }

    void attachDataSocket(QTcpSocket *socket)
    {
        if (m_data) {
            m_data->disconnect(this);
            m_data->abort();
            m_data->deleteLater();
        }
        m_data = socket;
        m_data->setParent(this);
        connect(m_data, &QTcpSocket::disconnected, this, &FtpSession::onDataClosed);
        connect(m_data, &QTcpSocket::readyRead, this, &FtpSession::onDataReadyRead);
        connect(m_data, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError error) {
                    // 传输结束时对端先关闭属于正常流程，交给 disconnected 处理
                    if (error == QAbstractSocket::RemoteHostClosedError) {
                        return;
                    }
                    reply(QStringLiteral("426 数据连接中断"));
                    closeData();
                });
    }

    void closePassive()
    {
        if (m_passive) {
            m_passive->close();
            m_passive->deleteLater();
            m_passive = nullptr;
        }
    }

    void closeData(bool closeSocket = true)
    {
        if (m_data) {
            if (closeSocket) {
                m_data->disconnect(this);
                m_data->abort();
                m_data->deleteLater();
            }
            m_data = nullptr;
        }
        m_transfer = NoTransfer;
        if (m_uploadFile) {
            m_uploadFile->close();
            delete m_uploadFile;
            m_uploadFile = nullptr;
        }
    }

    void startListing(bool namesOnly, const QString &argument)
    {
        QString target = argument.trimmed();
        if (target.startsWith(QLatin1Char('-'))) {
            target.clear();   // LIST -la
        }
        const QString path = resolve(target);
        if (path.isEmpty()) {
            reply(QStringLiteral("550 目录不存在"));
            return;
        }
        const QFileInfo info(path);
        QByteArray payload;
        if (info.isDir()) {
            const QFileInfoList entries = QDir(path).entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo &entry : entries) {
                if (namesOnly) {
                    payload += entry.fileName().toUtf8() + "\r\n";
                } else {
                    payload += listLine(entry);
                }
            }
        } else if (info.exists()) {
            payload = namesOnly ? QByteArray(info.fileName().toUtf8() + "\r\n")
                                : listLine(info);
        } else {
            reply(QStringLiteral("550 目录不存在"));
            return;
        }
        m_pendingPayload = payload;
        m_transfer = ListFiles;
        startPendingTransfer();
    }

    void startDownload(const QString &argument)
    {
        const QString path = resolve(argument);
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            reply(QStringLiteral("550 文件不存在"));
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            reply(QStringLiteral("550 无法读取文件"));
            return;
        }
        m_pendingPayload = file.readAll();
        file.close();
        m_pendingDescription = tr("下载 %1（%2 字节）").arg(argument).arg(m_pendingPayload.size());
        m_transfer = Download;
        startPendingTransfer();
    }

    void startUpload(const QString &argument, bool append)
    {
        if (!m_allowWrite) {
            reply(QStringLiteral("550 服务为只读"));
            return;
        }
        const QString path = resolve(argument, true);
        if (path.isEmpty()) {
            reply(QStringLiteral("550 路径不合法"));
            return;
        }
        m_uploadFile = new QFile(path);
        const QIODevice::OpenMode mode = append ? (QIODevice::WriteOnly | QIODevice::Append)
                                                : (QIODevice::WriteOnly | QIODevice::Truncate);
        if (!m_uploadFile->open(mode)) {
            delete m_uploadFile;
            m_uploadFile = nullptr;
            reply(QStringLiteral("550 无法写入文件"));
            return;
        }
        m_pendingDescription = append ? tr("追加 %1").arg(argument)
                                      : tr("上传 %1").arg(argument);
        m_transfer = Upload;
        startPendingTransfer();
    }

    void startPendingTransfer()
    {
        if (m_transfer == NoTransfer) {
            return;   // 数据连接先到，等命令
        }
        if (!m_data) {
            if (m_passive) {
                return;   // PASV 端口已就绪，等客户端连上来
            }
            reply(QStringLiteral("425 未建立数据连接"));
            m_transfer = NoTransfer;
            return;
        }
        if (m_data->state() == QAbstractSocket::ConnectingState) {
            return;   // 主动模式，连接建立后会再次进入
        }
        if (m_data->state() != QAbstractSocket::ConnectedState) {
            reply(QStringLiteral("425 未建立数据连接"));
            m_transfer = NoTransfer;
            return;
        }
        reply(QStringLiteral("150 正在打开数据连接"));
        if (m_transfer == Upload) {
            m_uploadBytes = 0;
            return;   // 等待 readyRead
        }
        m_data->write(m_pendingPayload);
        m_data->disconnectFromHost();
        if (m_transfer == Download) {
            m_log(tr("FTP %1（%2）").arg(m_pendingDescription, peerText()));
        } else {
            m_log(tr("FTP 列出目录（%1）").arg(peerText()));
        }
    }

    void onDataReadyRead()
    {
        if (m_transfer != Upload || !m_uploadFile || !m_data) {
            return;
        }
        const QByteArray chunk = m_data->readAll();
        if (m_uploadFile->write(chunk) != chunk.size()) {
            reply(QStringLiteral("452 磁盘写入失败"));
            closeData();
            return;
        }
        m_uploadBytes += chunk.size();
    }

    void onDataClosed()
    {
        if (m_transfer == Upload && m_uploadFile) {
            m_uploadFile->close();
            m_log(tr("FTP %1（%2，%3 字节）")
                      .arg(m_pendingDescription, peerText())
                      .arg(m_uploadBytes));
            delete m_uploadFile;
            m_uploadFile = nullptr;
            reply(QStringLiteral("226 传输完成"));
        } else if (m_transfer == Download || m_transfer == ListFiles) {
            reply(QStringLiteral("226 传输完成"));
        }
        m_transfer = NoTransfer;
        if (m_data) {
            m_data->deleteLater();
            m_data = nullptr;
        }
        closePassive();
    }

    QString m_root;
    bool m_allowAnonymous = true;
    QString m_user;
    QString m_password;
    bool m_allowWrite = false;

    QTcpSocket *m_control = nullptr;
    QByteArray m_buffer;
    bool m_loggedIn = false;
    QString m_pendingUser;
    QString m_cwd;

    QTcpServer *m_passive = nullptr;
    QTcpSocket *m_data = nullptr;

    Transfer m_transfer = NoTransfer;
    QByteArray m_pendingPayload;
    QString m_pendingDescription;
    QFile *m_uploadFile = nullptr;
    qint64 m_uploadBytes = 0;
    QString m_renameFrom;

    std::function<void(const QString &)> m_log;
};

// ---------------------------------------------------------------------------
// FtpServer
// ---------------------------------------------------------------------------
FtpServer::FtpServer(QObject *parent)
    : ServerService(parent)
{
}

QString FtpServer::displayName() const
{
    return tr("FTP 服务器");
}

QString FtpServer::description() const
{
    return tr("标准 FTP 服务，可用资源管理器、FileZilla 或 curl 访问根目录");
}

QJsonObject FtpServer::defaultConfig() const
{
    QJsonObject config;
    config.insert(QStringLiteral("port"), defaultPort());
    config.insert(QStringLiteral("root"), QString());
    config.insert(QStringLiteral("allowAnonymous"), true);
    config.insert(QStringLiteral("user"), QStringLiteral("ftp"));
    config.insert(QStringLiteral("password"), QStringLiteral("ftp"));
    config.insert(QStringLiteral("allowWrite"), true);
    return config;
}

bool FtpServer::startServer()
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
    connect(m_server, &QTcpServer::newConnection, this, &FtpServer::onNewConnection);
    if (!m_server->listen(QHostAddress::Any, quint16(listenPort))) {
        setError(tr("无法监听端口 %1：%2").arg(listenPort).arg(m_server->errorString()));
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    log(tr("FTP 服务已启动：ftp://0.0.0.0:%1/ 根目录 %2").arg(listenPort).arg(root));
    return true;
}

void FtpServer::stopServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    for (FtpSession *session : std::as_const(m_sessions)) {
        session->deleteLater();
    }
    m_sessions.clear();
}

void FtpServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        auto *session = new FtpSession(
            ServiceUtil::canonicalRoot(configString(QStringLiteral("root"))), config(), socket,
            [this](const QString &line) { log(line); }, this);
        m_sessions.append(session);
        connect(session, &QObject::destroyed, this, [this, session] {
            m_sessions.removeAll(session);
        });
    }
}

#include "ftpserver.moc"
