#include "transfer/filetransfer.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

FileTransfer::FileTransfer(Kind kind, Direction direction, const QStringList &paths,
                           QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_kind(kind)
    , m_direction(direction)
{
}

FileTransfer::~FileTransfer() = default;

QString FileTransfer::protocolName() const
{
    switch (m_kind) {
    case Kind::Zmodem:
        return QStringLiteral("ZMODEM");
    case Kind::Ymodem:
        return QStringLiteral("YMODEM");
    case Kind::Xmodem:
        return QStringLiteral("XMODEM");
    case Kind::Kermit:
        return QStringLiteral("Kermit");
    case Kind::Ascii:
        return QStringLiteral("ASCII");
    }
    return QString();
}

QString FileTransfer::directionName() const
{
    return m_direction == Direction::Send ? tr("发送") : tr("接收");
}

QString FileTransfer::fileSummary() const
{
    QStringList names;
    for (const QString &path : m_paths) {
        names.append(QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName());
    }
    return names.join(QStringLiteral(", "));
}

void FileTransfer::setWriter(std::function<void(const QByteArray &)> writer)
{
    m_writer = std::move(writer);
}

void FileTransfer::write(const QByteArray &data)
{
    if (m_writer && !data.isEmpty()) {
        m_writer(data);
    }
}

void FileTransfer::setStatus(const QString &status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged(status);
}

void FileTransfer::setProgress(qint64 transferred, qint64 total)
{
    m_transferred = transferred;
    if (total >= 0) {
        m_total = total;
    }
    emit progress(m_transferred, m_total);
}

void FileTransfer::log(const QString &line)
{
    emit logMessage(line);
}

void FileTransfer::finish(bool success, const QString &message)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (!success) {
        m_error = message;
    }
    emit finished(success, message);
}

QString FileTransfer::receiveDirectory() const
{
    if (m_paths.isEmpty()) {
        return QDir::currentPath();
    }
    const QFileInfo info(m_paths.first());
    if (info.isDir()) {
        return info.absoluteFilePath();
    }
    return QFileInfo(m_paths.first()).absolutePath();
}

QString FileTransfer::receiveFile() const
{
    return m_paths.value(0);
}

QString FileTransfer::sanitizeName(const QString &name)
{
    QString base = name;
    base.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int slash = base.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        base = base.mid(slash + 1);
    }
    base = base.trimmed();
    base.remove(QRegularExpression(QStringLiteral("[\x00-\x1F<>:\"|?*]")));
    if (base == QLatin1String(".") || base == QLatin1String("..")) {
        base.clear();
    }
    return base;
}
