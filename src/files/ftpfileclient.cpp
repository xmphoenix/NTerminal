#include "files/ftpfileclient.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QLocale>
#include <QRegularExpression>
#include <QTcpSocket>

#include <algorithm>

namespace {

constexpr int kControlTimeoutMs = 20000;
constexpr int kDataTimeoutMs = 60000;
constexpr int kBlockSize = 64 * 1024;

bool isSuccessCode(int code)
{
    return code >= 200 && code < 300;
}

QString ftpPathJoin(const QString &base, const QString &name)
{
    if (base.isEmpty() || base == QLatin1String("/")) {
        return QLatin1Char('/') + name;
    }
    if (base.endsWith(QLatin1Char('/'))) {
        return base + name;
    }
    return base + QLatin1Char('/') + name;
}

} // namespace

FtpFileClient::FtpFileClient(const Common::ConnectionSettings &settings)
    : m_settings(settings)
{
}

FtpFileClient::~FtpFileClient()
{
    disconnect();
}

bool FtpFileClient::readResponse(int *code, QString *response, QString *error)
{
    if (!m_control) {
        *error = QStringLiteral("FTP 控制连接未建立");
        return false;
    }

    QStringList lines;
    while (true) {
        if (!m_control->canReadLine()
            && !m_control->waitForReadyRead(kControlTimeoutMs)) {
            *error = QStringLiteral("等待 FTP 响应超时");
            return false;
        }

        while (m_control->canReadLine()) {
            QString line = QString::fromUtf8(m_control->readLine()).trimmed();
            if (line.isEmpty()) {
                continue;
            }
            if (line.size() < 3) {
                continue;
            }
            bool ok = false;
            const int responseCode = line.left(3).toInt(&ok);
            if (!ok) {
                continue;
            }
            lines.append(line);

            if (line.size() > 3 && line.at(3) == QLatin1Char('-')) {
                // 多行响应，读到 "NNN " 为止
                while (true) {
                    if (!m_control->canReadLine()
                        && !m_control->waitForReadyRead(kControlTimeoutMs)) {
                        *error = QStringLiteral("等待 FTP 多行响应超时");
                        return false;
                    }
                    QString part =
                        QString::fromUtf8(m_control->readLine()).trimmed();
                    if (part.isEmpty()) {
                        continue;
                    }
                    lines.append(part);
                    if (part.startsWith(QString::number(responseCode)
                                        + QLatin1Char(' '))) {
                        break;
                    }
                }
            }

            *code = responseCode;
            *response = lines.join(QLatin1Char('\n'));
            m_lastResponse = *response;
            return true;
        }
    }
}

bool FtpFileClient::command(const QString &text, const QStringList &expectedCodes,
                            QString *response, QString *error)
{
    if (!m_control
        || m_control->state() != QAbstractSocket::ConnectedState) {
        *error = QStringLiteral("FTP 未连接");
        return false;
    }

    QByteArray payload = text.toUtf8();
    payload.append("\r\n");
    if (m_control->write(payload) != payload.size()
        || !m_control->waitForBytesWritten(kControlTimeoutMs)) {
        *error = QStringLiteral("发送 FTP 命令失败");
        return false;
    }

    int code = 0;
    QString reply;
    if (!readResponse(&code, &reply, error)) {
        return false;
    }
    if (response) {
        *response = reply;
    }
    if (!expectedCodes.isEmpty()
        && !expectedCodes.contains(QString::number(code))) {
        *error = QStringLiteral("FTP 命令失败：%1").arg(reply);
        return false;
    }
    return true;
}

bool FtpFileClient::login(QString *error)
{
    const QString user = m_settings.username.isEmpty()
                             ? QStringLiteral("anonymous")
                             : m_settings.username;
    const QString password = m_settings.password.isEmpty()
                                 ? QStringLiteral("anonymous@")
                                 : m_settings.password;

    QString reply;
    if (!command(QStringLiteral("USER ") + user,
                 {QStringLiteral("230"), QStringLiteral("331")}, &reply, error)) {
        return false;
    }
    if (reply.startsWith(QLatin1String("331"))) {
        if (!command(QStringLiteral("PASS ") + password,
                     {QStringLiteral("230"), QStringLiteral("202")}, &reply,
                     error)) {
            return false;
        }
    }
    return true;
}

bool FtpFileClient::connect(QString *home, QString *error)
{
    disconnect();

    m_control = new QTcpSocket();
    m_control->connectToHost(m_settings.host, m_settings.port);
    if (!m_control->waitForConnected(kControlTimeoutMs)) {
        *error = QStringLiteral("连接 FTP 服务器失败：%1").arg(m_control->errorString());
        disconnect();
        return false;
    }

    int code = 0;
    QString reply;
    if (!readResponse(&code, &reply, error) || code != 220) {
        if (error->isEmpty()) {
            *error = QStringLiteral("FTP 服务器未返回欢迎信息：%1").arg(reply);
        }
        disconnect();
        return false;
    }

    if (!login(error)) {
        disconnect();
        return false;
    }

    if (!command(QStringLiteral("TYPE I"), {QStringLiteral("200")}, &reply, error)) {
        disconnect();
        return false;
    }

    if (!command(QStringLiteral("PWD"), {QStringLiteral("257")}, &reply, error)) {
        disconnect();
        return false;
    }
    *home = parsePwd(reply);
    if (home->isEmpty()) {
        *home = QStringLiteral("/");
    }
    return true;
}

void FtpFileClient::disconnect()
{
    closeDataConnection();
    if (m_control) {
        if (m_control->state() == QAbstractSocket::ConnectedState) {
            m_control->write("QUIT\r\n");
            m_control->waitForBytesWritten(500);
            m_control->disconnectFromHost();
            if (m_control->state() != QAbstractSocket::UnconnectedState) {
                m_control->waitForDisconnected(1000);
            }
        }
        delete m_control;
        m_control = nullptr;
    }
}

QString FtpFileClient::parsePwd(const QString &response) const
{
    const QRegularExpression regex(QStringLiteral("\"([^\"]*)\""));
    const QRegularExpressionMatch match = regex.match(response);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool FtpFileClient::enterPassive(QString *host, quint16 *port, QString *error)
{
    QString reply;
    if (command(QStringLiteral("PASV"), {QStringLiteral("227")}, &reply, error)) {
        const QRegularExpression regex(
            QStringLiteral("\\((\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)\\)"));
        const QRegularExpressionMatch match = regex.match(reply);
        if (match.hasMatch()) {
            *host = QStringLiteral("%1.%2.%3.%4")
                        .arg(match.captured(1), match.captured(2),
                             match.captured(3), match.captured(4));
            *port = quint16(match.captured(5).toInt() * 256
                            + match.captured(6).toInt());
            if (*host == QLatin1String("0.0.0.0") && m_control) {
                *host = m_control->peerAddress().toString();
            }
            return true;
        }
    }

    // PASV 不可用（例如 IPv6）时尝试 EPSV
    error->clear();
    if (command(QStringLiteral("EPSV"), {QStringLiteral("229")}, &reply, error)) {
        const QRegularExpression regex(QStringLiteral("\\(\\|\\|\\|(\\d+)\\|"));
        const QRegularExpressionMatch match = regex.match(reply);
        if (match.hasMatch()) {
            *host = m_control->peerAddress().toString();
            *port = quint16(match.captured(1).toInt());
            return true;
        }
    }

    *error = QStringLiteral("FTP 服务器不支持被动模式：%1").arg(reply);
    return false;
}

bool FtpFileClient::openDataConnection(const QString &host, quint16 port,
                                       QString *error)
{
    closeDataConnection();
    m_data = new QTcpSocket();
    m_data->connectToHost(host, port);
    if (!m_data->waitForConnected(kControlTimeoutMs)) {
        *error = QStringLiteral("连接 FTP 数据端口失败：%1").arg(m_data->errorString());
        closeDataConnection();
        return false;
    }
    return true;
}

void FtpFileClient::closeDataConnection()
{
    if (m_data) {
        m_data->abort();
        delete m_data;
        m_data = nullptr;
    }
}

bool FtpFileClient::listDirectory(const QString &path,
                                  QVector<RemoteFileEntry> *entries,
                                  QString *error)
{
    QString host;
    quint16 port = 0;
    if (!enterPassive(&host, &port, error)
        || !openDataConnection(host, port, error)) {
        return false;
    }

    QString reply;
    if (!command(QStringLiteral("LIST ") + path,
                 {QStringLiteral("125"), QStringLiteral("150"),
                  QStringLiteral("226"), QStringLiteral("250")},
                 &reply, error)) {
        closeDataConnection();
        return false;
    }

    QByteArray data;
    if (!reply.startsWith(QLatin1String("226"))
        && !reply.startsWith(QLatin1String("250"))) {
        while (m_data
               && m_data->state() != QAbstractSocket::UnconnectedState) {
            if (!m_data->waitForReadyRead(kDataTimeoutMs)) {
                if (m_data->state() == QAbstractSocket::UnconnectedState) {
                    break;
                }
                *error = QStringLiteral("读取 FTP 目录超时");
                closeDataConnection();
                return false;
            }
            data += m_data->readAll();
        }
        if (m_data) {
            data += m_data->readAll();
        }
        closeDataConnection();

        int finalCode = 0;
        QString finalReply;
        if (!readResponse(&finalCode, &finalReply, error)
            || !isSuccessCode(finalCode)) {
            if (error->isEmpty()) {
                *error = QStringLiteral("FTP 目录读取失败：%1").arg(finalReply);
            }
            return false;
        }
    } else {
        closeDataConnection();
    }

    *entries = parseList(data, path);
    return true;
}

QVector<RemoteFileEntry> FtpFileClient::parseList(const QByteArray &data,
                                                  const QString &basePath) const
{
    QVector<RemoteFileEntry> entries;
    static const QRegularExpression unixPattern(
        QStringLiteral("^([dl-])([rwxst-]{9})\\s+\\d+\\s+(\\S+)\\s+(\\S+)\\s+"
                       "(\\d+)\\s+([A-Z][a-z]{2}\\s+\\d{1,2}\\s+"
                       "(?:\\d{2}:\\d{2}|\\d{4}))\\s+(.+)$"));
    static const QRegularExpression dosPattern(
        QStringLiteral("^(\\d{2}-\\d{2}-\\d{2,4})\\s+(\\d{2}:\\d{2}(?:AM|PM))\\s+"
                       "(<DIR>|\\d+)\\s+(.+)$"));

    const QStringList lines =
        QString::fromUtf8(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("total "))) {
            continue;
        }

        RemoteFileEntry entry;
        QRegularExpressionMatch match = unixPattern.match(line);
        if (match.hasMatch()) {
            entry.isDir = match.captured(1) == QLatin1String("d");
            entry.permissions = match.captured(2);
            entry.owner = match.captured(3);
            entry.group = match.captured(4);
            entry.size = match.captured(5).toLongLong();
            const QString dateText = match.captured(6);
            const QLocale locale = QLocale::c();
            if (dateText.contains(QLatin1Char(':'))) {
                entry.modified = locale.toDateTime(
                    dateText + QLatin1Char(' ') + QString::number(QDate::currentDate().year()),
                    QStringLiteral("MMM d HH:mm yyyy"));
                if (entry.modified.isValid()
                    && entry.modified > QDateTime::currentDateTime().addDays(1)) {
                    entry.modified = entry.modified.addYears(-1);
                }
            } else {
                entry.modified = locale.toDateTime(dateText,
                                                   QStringLiteral("MMM d yyyy"));
            }
            entry.name = match.captured(7).trimmed();
        } else if ((match = dosPattern.match(line)).hasMatch()) {
            entry.isDir = match.captured(3) == QLatin1String("<DIR>");
            entry.size = entry.isDir ? 0 : match.captured(3).toLongLong();
            entry.modified = QLocale::c().toDateTime(
                match.captured(1) + QLatin1Char(' ') + match.captured(2),
                QStringLiteral("MM-dd-yy hh:mmAP"));
            entry.name = match.captured(4).trimmed();
        } else {
            // 无法解析的 LIST 行：至少保留文件名
            const QStringList parts =
                line.split(QRegularExpression(QStringLiteral("\\s+")),
                           Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                continue;
            }
            entry.name = parts.last();
        }

        if (entry.name.isEmpty() || entry.name == QLatin1String(".")
            || entry.name == QLatin1String("..")) {
            continue;
        }
        entry.path = ftpPathJoin(basePath, entry.name);
        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(),
              [](const RemoteFileEntry &a, const RemoteFileEntry &b) {
                  if (a.isDir != b.isDir) {
                      return a.isDir;
                  }
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    return entries;
}

bool FtpFileClient::readDataToFile(const QString &remotePath,
                                   const QString &localPath,
                                   const Progress &progress, QString *error)
{
    qint64 total = -1;
    QString sizeReply;
    // SIZE 失败不影响下载（部分服务器在 ASCII/受限目录下不支持）
    if (command(QStringLiteral("SIZE ") + remotePath,
                {QStringLiteral("213")}, &sizeReply, error)) {
        const QStringList parts = sizeReply.split(QLatin1Char(' '),
                                                  Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            total = parts.last().toLongLong();
        }
    }
    error->clear();

    QString host;
    quint16 port = 0;
    if (!enterPassive(&host, &port, error)
        || !openDataConnection(host, port, error)) {
        return false;
    }

    QString reply;
    if (!command(QStringLiteral("RETR ") + remotePath,
                 {QStringLiteral("125"), QStringLiteral("150"),
                  QStringLiteral("226"), QStringLiteral("250")},
                 &reply, error)) {
        closeDataConnection();
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        closeDataConnection();
        *error = QStringLiteral("无法创建本地文件 %1：%2")
                     .arg(localPath, local.errorString());
        return false;
    }

    if (!progress(0, total)) {
        closeDataConnection();
        local.close();
        local.remove();
        *error = QStringLiteral("已取消");
        return false;
    }

    const bool commandCompleted = reply.startsWith(QLatin1String("226"))
                                  || reply.startsWith(QLatin1String("250"));
    bool ok = true;
    qint64 done = 0;
    if (!commandCompleted) {
        while (m_data
               && m_data->state() != QAbstractSocket::UnconnectedState) {
            if (!m_data->waitForReadyRead(kDataTimeoutMs)) {
                if (m_data->state() == QAbstractSocket::UnconnectedState) {
                    break;
                }
                *error = QStringLiteral("读取 FTP 数据超时");
                ok = false;
                break;
            }
            const QByteArray chunk = m_data->readAll();
            if (!chunk.isEmpty()) {
                if (local.write(chunk) != chunk.size()) {
                    *error = QStringLiteral("写入本地文件失败：%1")
                                 .arg(local.errorString());
                    ok = false;
                    break;
                }
                done += chunk.size();
                if (!progress(done, total)) {
                    *error = QStringLiteral("已取消");
                    ok = false;
                    break;
                }
            }
        }
        if (ok && m_data) {
            const QByteArray tail = m_data->readAll();
            if (!tail.isEmpty()) {
                local.write(tail);
                done += tail.size();
            }
        }
    }
    closeDataConnection();
    local.close();

    if (ok && !commandCompleted) {
        int finalCode = 0;
        QString finalReply;
        if (!readResponse(&finalCode, &finalReply, error)
            || !isSuccessCode(finalCode)) {
            if (error->isEmpty()) {
                *error = QStringLiteral("FTP 下载失败：%1").arg(finalReply);
            }
            ok = false;
        }
    }
    if (!ok) {
        local.remove();
    }
    return ok;
}

bool FtpFileClient::writeFileToData(const QString &localPath,
                                    const QString &remotePath,
                                    const Progress &progress, QString *error)
{
    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法打开本地文件 %1：%2")
                     .arg(localPath, local.errorString());
        return false;
    }
    const qint64 total = local.size();

    QString host;
    quint16 port = 0;
    if (!enterPassive(&host, &port, error)
        || !openDataConnection(host, port, error)) {
        local.close();
        return false;
    }

    QString reply;
    if (!command(QStringLiteral("STOR ") + remotePath,
                 {QStringLiteral("125"), QStringLiteral("150"),
                  QStringLiteral("226"), QStringLiteral("250")},
                 &reply, error)) {
        closeDataConnection();
        local.close();
        return false;
    }

    if (!progress(0, total)) {
        closeDataConnection();
        local.close();
        *error = QStringLiteral("已取消");
        return false;
    }

    const bool commandCompleted = reply.startsWith(QLatin1String("226"))
                                  || reply.startsWith(QLatin1String("250"));
    bool ok = true;
    qint64 done = 0;
    if (!commandCompleted) {
        while (!local.atEnd()) {
            const QByteArray chunk = local.read(kBlockSize);
            if (m_data->write(chunk) != chunk.size()
                || !m_data->waitForBytesWritten(kDataTimeoutMs)) {
                *error = QStringLiteral("写入 FTP 数据连接失败：%1")
                             .arg(m_data->errorString());
                ok = false;
                break;
            }
            done += chunk.size();
            if (!progress(done, total)) {
                *error = QStringLiteral("已取消");
                ok = false;
                break;
            }
        }
    }
    local.close();

    if (m_data) {
        m_data->disconnectFromHost();
        if (m_data->state() != QAbstractSocket::UnconnectedState) {
            m_data->waitForDisconnected(kDataTimeoutMs);
        }
    }
    closeDataConnection();

    if (ok && !commandCompleted) {
        int finalCode = 0;
        QString finalReply;
        if (!readResponse(&finalCode, &finalReply, error)
            || !isSuccessCode(finalCode)) {
            if (error->isEmpty()) {
                *error = QStringLiteral("FTP 上传失败：%1").arg(finalReply);
            }
            ok = false;
        }
    }
    return ok;
}

bool FtpFileClient::downloadFile(const QString &remotePath,
                                 const QString &localPath,
                                 const Progress &progress, QString *error)
{
    return readDataToFile(remotePath, localPath, progress, error);
}

bool FtpFileClient::uploadFile(const QString &localPath,
                               const QString &remotePath,
                               const Progress &progress, QString *error)
{
    return writeFileToData(localPath, remotePath, progress, error);
}

bool FtpFileClient::makeDirectory(const QString &path, QString *error)
{
    QString reply;
    if (command(QStringLiteral("MKD ") + path,
                {QStringLiteral("257"), QStringLiteral("250")}, &reply,
                error)) {
        return true;
    }

    // 目录已存在时 MKD 返回 550；用 CWD/CDUP 判定它确实是目录
    QString cwdReply;
    if (command(QStringLiteral("CWD ") + path, {QStringLiteral("250")},
                &cwdReply, error)
        && command(QStringLiteral("CDUP"), {QStringLiteral("250")},
                   &cwdReply, error)) {
        return true;
    }
    *error = QStringLiteral("创建远端目录失败 %1").arg(path);
    return false;
}

bool FtpFileClient::removeEntry(const RemoteTransferItem &item, QString *error)
{
    QString reply;
    return command((item.isDir ? QStringLiteral("RMD ") : QStringLiteral("DELE "))
                       + item.path,
                   {QStringLiteral("250"), QStringLiteral("200")}, &reply,
                   error);
}

bool FtpFileClient::renameEntry(const QString &from, const QString &to,
                                QString *error)
{
    QString reply;
    if (!command(QStringLiteral("RNFR ") + from, {QStringLiteral("350")},
                 &reply, error)) {
        return false;
    }
    return command(QStringLiteral("RNTO ") + to, {QStringLiteral("250")},
                   &reply, error);
}
