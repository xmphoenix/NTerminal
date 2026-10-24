#include "services/httpserver.h"
#include "services/serviceutil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMimeDatabase>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <utility>

namespace {

QString contentTypeFor(const QString &path)
{
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    QString name = mime.name();
    if (name.isEmpty() || name == QLatin1String("application/octet-stream")) {
        return QStringLiteral("application/octet-stream");
    }
    if (name.startsWith(QLatin1String("text/")) || name == QLatin1String("application/javascript")
        || name == QLatin1String("application/json")) {
        name += QStringLiteral("; charset=utf-8");
    }
    return name;
}

QByteArray httpResponse(int status, const QString &reason, const QString &contentType,
                        const QByteArray &body, bool includeBody)
{
    QByteArray header;
    header += QStringLiteral("HTTP/1.1 %1 %2\r\n").arg(status).arg(reason).toUtf8();
    header += "Server: NTerminal\r\n";
    header += QStringLiteral("Content-Type: %1\r\n").arg(contentType).toUtf8();
    header += QStringLiteral("Content-Length: %1\r\n").arg(body.size()).toUtf8();
    header += "Accept-Ranges: none\r\n";
    header += "Connection: close\r\n\r\n";
    if (includeBody) {
        header += body;
    }
    return header;
}

QByteArray errorPage(int status, const QString &reason, const QString &detail)
{
    const QString html = QStringLiteral(
                             "<!doctype html><html><head><meta charset=\"utf-8\">"
                             "<title>%1 %2</title></head><body style=\"font-family:system-ui;"
                             "padding:32px\"><h1>%1 %2</h1><p>%3</p></body></html>")
                             .arg(status)
                             .arg(reason, detail.toHtmlEscaped());
    return html.toUtf8();
}

QString humanSize(qint64 bytes)
{
    constexpr qint64 kb = 1024;
    constexpr qint64 mb = kb * 1024;
    constexpr qint64 gb = mb * 1024;
    if (bytes >= gb) {
        return QStringLiteral("%1 GB").arg(double(bytes) / gb, 0, 'f', 1);
    }
    if (bytes >= mb) {
        return QStringLiteral("%1 MB").arg(double(bytes) / mb, 0, 'f', 1);
    }
    if (bytes >= kb) {
        return QStringLiteral("%1 KB").arg(double(bytes) / kb, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QByteArray directoryListing(const QDir &dir, const QString &urlPath)
{
    QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QString html = QStringLiteral(
                       "<!doctype html><html><head><meta charset=\"utf-8\">"
                       "<title>目录 %1</title><style>"
                       "body{font-family:system-ui,Segoe UI,sans-serif;padding:28px;color:#1f2933}"
                       "h1{font-size:18px}table{border-collapse:collapse;width:100%;max-width:760px}"
                       "td,th{text-align:left;padding:6px 10px;border-bottom:1px solid #e4e9ec;"
                       "font-size:13px}th{color:#6b7780;font-weight:600}"
                       "a{color:#0f766e;text-decoration:none}a:hover{text-decoration:underline}"
                       "</style></head><body><h1>目录 %1</h1><table>"
                       "<tr><th>名称</th><th>大小</th><th>修改时间</th></tr>")
                       .arg(urlPath.toHtmlEscaped());

    if (urlPath != QLatin1String("/")) {
        html += QStringLiteral("<tr><td><a href=\"../\">../</a></td><td></td><td></td></tr>");
    }
    for (const QFileInfo &info : std::as_const(entries)) {
        QString name = info.fileName();
        const bool isDir = info.isDir();
        if (isDir) {
            name += QLatin1Char('/');
        }
        const QString href = QString::fromLatin1(QUrl::toPercentEncoding(info.fileName()))
                             + (isDir ? QStringLiteral("/") : QString());
        html += QStringLiteral("<tr><td><a href=\"%1\">%2</a></td><td>%3</td><td>%4</td></tr>")
                    .arg(href, name.toHtmlEscaped(),
                         isDir ? QStringLiteral("—") : humanSize(info.size()),
                         info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    html += QStringLiteral("</table></body></html>");
    return html.toUtf8();
}

} // namespace

HttpServer::HttpServer(QObject *parent)
    : ServerService(parent)
{
}

QString HttpServer::displayName() const
{
    return tr("HTTP 服务器");
}

QString HttpServer::description() const
{
    return tr("用浏览器浏览/下载根目录下的文件，可用于快速共享文件");
}

QJsonObject HttpServer::defaultConfig() const
{
    QJsonObject config;
    config.insert(QStringLiteral("port"), defaultPort());
    config.insert(QStringLiteral("root"), QString());
    config.insert(QStringLiteral("listDirectory"), true);
    return config;
}

bool HttpServer::startServer()
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
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
    if (!m_server->listen(QHostAddress::Any, quint16(listenPort))) {
        setError(tr("无法监听端口 %1：%2").arg(listenPort).arg(m_server->errorString()));
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    log(tr("HTTP 服务已启动：http://0.0.0.0:%1/ 根目录 %2").arg(listenPort).arg(root));
    return true;
}

void HttpServer::stopServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    const QList<QTcpSocket *> sockets = m_buffers.keys();
    for (QTcpSocket *socket : sockets) {
        socket->disconnectFromHost();
    }
    m_buffers.clear();
}

void HttpServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
        // 请求头最多 64KB，超时/超限连接由客户端关闭
    }
}

void HttpServer::onReadyRead(QTcpSocket *socket)
{
    if (!m_buffers.contains(socket)) {
        return;
    }
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > 64 * 1024) {
        socket->write(httpResponse(400, QStringLiteral("Bad Request"),
                                   QStringLiteral("text/plain; charset=utf-8"),
                                   "Request header too large", true));
        buffer.clear();
        socket->disconnectFromHost();
        return;
    }
    const int end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return;   // 头部未收全
    }
    const QByteArray request = buffer.left(end);
    buffer.clear();
    handleRequest(socket, request);
}

void HttpServer::handleRequest(QTcpSocket *socket, const QByteArray &request)
{
    const QList<QByteArray> lines = request.split('\n');
    const QList<QByteArray> parts = lines.value(0).trimmed().split(' ');
    const QString peer = QStringLiteral("%1:%2")
                             .arg(socket->peerAddress().toString())
                             .arg(socket->peerPort());
    if (parts.size() < 2) {
        socket->write(httpResponse(400, QStringLiteral("Bad Request"),
                                   QStringLiteral("text/plain; charset=utf-8"),
                                   "Malformed request line", true));
        socket->disconnectFromHost();
        return;
    }

    const QString method = QString::fromLatin1(parts.at(0));
    if (method != QLatin1String("GET") && method != QLatin1String("HEAD")) {
        socket->write(httpResponse(405, QStringLiteral("Method Not Allowed"),
                                   QStringLiteral("text/plain; charset=utf-8"),
                                   "Only GET and HEAD are supported", true));
        socket->disconnectFromHost();
        log(tr("HTTP %1 %2 405").arg(method, peer));
        return;
    }
    const bool includeBody = method == QLatin1String("GET");

    QString target = QString::fromLatin1(parts.at(1));
    const int queryStart = target.indexOf(QLatin1Char('?'));
    if (queryStart >= 0) {
        target.truncate(queryStart);
    }
    target = QUrl::fromPercentEncoding(target.toUtf8());
    if (!target.startsWith(QLatin1Char('/'))) {
        target.prepend(QLatin1Char('/'));
    }

    const QString root = ServiceUtil::canonicalRoot(configString(QStringLiteral("root")));
    QString path = ServiceUtil::safeJoin(root, target.mid(1), true);
    if (path.isEmpty()) {
        const QByteArray body = errorPage(403, QStringLiteral("Forbidden"),
                                          tr("路径超出服务根目录"));
        socket->write(httpResponse(403, QStringLiteral("Forbidden"),
                                   QStringLiteral("text/html; charset=utf-8"), body, includeBody));
        socket->disconnectFromHost();
        log(tr("HTTP %1 %2 403").arg(target, peer));
        return;
    }

    QFileInfo info(path);
    if (info.isDir()) {
        // 目录：优先 index.html/index.htm，否则按配置生成列表
        const QStringList indexes = {QStringLiteral("index.html"), QStringLiteral("index.htm")};
        QString index;
        for (const QString &candidate : indexes) {
            const QString candidatePath =
                ServiceUtil::safeJoin(root, (target.mid(1).isEmpty()
                                                 ? QString()
                                                 : target.mid(1) + QLatin1Char('/'))
                                                + candidate);
            if (!candidatePath.isEmpty() && QFileInfo(candidatePath).isFile()) {
                index = candidatePath;
                break;
            }
        }
        if (!index.isEmpty()) {
            info = QFileInfo(index);
        } else if (configBool(QStringLiteral("listDirectory"))) {
            QString urlPath = target;
            if (!urlPath.endsWith(QLatin1Char('/'))) {
                urlPath += QLatin1Char('/');
            }
            const QByteArray body = directoryListing(QDir(path), urlPath);
            socket->write(httpResponse(200, QStringLiteral("OK"),
                                       QStringLiteral("text/html; charset=utf-8"), body,
                                       includeBody));
            socket->disconnectFromHost();
            log(tr("HTTP %1 %2 200 目录").arg(target, peer));
            return;
        } else {
            const QByteArray body = errorPage(403, QStringLiteral("Forbidden"),
                                              tr("目录浏览已关闭"));
            socket->write(httpResponse(403, QStringLiteral("Forbidden"),
                                       QStringLiteral("text/html; charset=utf-8"), body,
                                       includeBody));
            socket->disconnectFromHost();
            log(tr("HTTP %1 %2 403").arg(target, peer));
            return;
        }
    }

    if (!info.isFile() || !info.isReadable()) {
        const QByteArray body = errorPage(404, QStringLiteral("Not Found"),
                                          tr("文件不存在：%1").arg(target));
        socket->write(httpResponse(404, QStringLiteral("Not Found"),
                                   QStringLiteral("text/html; charset=utf-8"), body, includeBody));
        socket->disconnectFromHost();
        log(tr("HTTP %1 %2 404").arg(target, peer));
        return;
    }

    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        const QByteArray body = errorPage(500, QStringLiteral("Internal Server Error"),
                                          file.errorString());
        socket->write(httpResponse(500, QStringLiteral("Internal Server Error"),
                                   QStringLiteral("text/html; charset=utf-8"), body, includeBody));
        socket->disconnectFromHost();
        log(tr("HTTP %1 %2 500 %3").arg(target, peer, file.errorString()));
        return;
    }
    const QByteArray body = file.readAll();
    file.close();
    socket->write(httpResponse(200, QStringLiteral("OK"), contentTypeFor(info.filePath()),
                               body, includeBody));
    socket->disconnectFromHost();
    log(tr("HTTP %1 %2 200 %3").arg(target, peer, humanSize(body.size())));
}
