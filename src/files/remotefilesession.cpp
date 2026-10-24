#include "files/remotefilesession.h"

#include "files/ftpfileclient.h"
#include "files/sshfileclient.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaType>

namespace {

QString joinRemotePath(const QString &directory, const QString &name)
{
    if (directory.isEmpty() || directory == QLatin1String("/")) {
        return QLatin1Char('/') + name;
    }
    if (directory.endsWith(QLatin1Char('/'))) {
        return directory + name;
    }
    return directory + QLatin1Char('/') + name;
}

// 大文件按 64 KB 回调一次进度；如果全部转发给 GUI，1 GB 文件会产生
// 一万多条跨线程事件，反而让进度更新卡顿。这里限制为约 10 次/秒，
// 同时保证首条和最后一条进度一定送达。
class ProgressThrottle
{
public:
    ProgressThrottle() { m_timer.start(); }

    bool shouldEmit(qint64 done, qint64 total)
    {
        const qint64 now = m_timer.elapsed();
        const bool complete = total > 0 && done >= total;
        if (m_first || complete || now - m_lastEmit >= 100) {
            m_first = false;
            m_lastEmit = now;
            return true;
        }
        return false;
    }

private:
    QElapsedTimer m_timer;
    qint64 m_lastEmit = 0;
    bool m_first = true;
};

} // namespace

RemoteFileSession::RemoteFileSession(const Common::ConnectionSettings &settings,
                                     QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<RemoteFileEntry>("RemoteFileEntry");
    qRegisterMetaType<QVector<RemoteFileEntry>>("QVector<RemoteFileEntry>");
    qRegisterMetaType<RemoteTransferItem>("RemoteTransferItem");
    qRegisterMetaType<QVector<RemoteTransferItem>>("QVector<RemoteTransferItem>");

    m_thread = new QThread(this);
    m_worker = new RemoteFileWorker(settings);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &RemoteFileWorker::connected, this,
            [this](const QString &home, bool directories) {
                m_connected = true;
                m_directorySupport = directories;
                emit connected(home, directories);
            });
    connect(m_worker, &RemoteFileWorker::connectionFailed, this,
            &RemoteFileSession::connectionFailed);
    connect(m_worker, &RemoteFileWorker::directoryListed, this,
            &RemoteFileSession::directoryListed);
    connect(m_worker, &RemoteFileWorker::listFailed, this,
            &RemoteFileSession::listFailed);
    connect(m_worker, &RemoteFileWorker::transferStarted, this,
            &RemoteFileSession::transferStarted);
    connect(m_worker, &RemoteFileWorker::transferProgress, this,
            &RemoteFileSession::transferProgress);
    connect(m_worker, &RemoteFileWorker::transferFinished, this,
            &RemoteFileSession::transferFinished);
    connect(m_worker, &RemoteFileWorker::operationFinished, this,
            &RemoteFileSession::operationFinished);
}

RemoteFileSession::~RemoteFileSession()
{
    stop();
}

void RemoteFileSession::start()
{
    if (!m_thread || !m_worker) {
        return;
    }
    if (!m_thread->isRunning()) {
        m_thread->start();
    }
    QMetaObject::invokeMethod(m_worker, &RemoteFileWorker::start,
                              Qt::QueuedConnection);
}

void RemoteFileSession::stop()
{
    if (!m_thread || !m_worker) {
        return;
    }
    if (!m_thread->isRunning()) {
        // 线程尚未启动：finished 不会触发 deleteLater，直接回收 worker
        delete m_worker;
        m_worker = nullptr;
        return;
    }

    m_worker->requestCancel();
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, thread = m_thread] {
            worker->shutdown();
            thread->quit();
        },
        Qt::QueuedConnection);
    if (!m_thread->wait(5000)) {
        m_thread->terminate();
        m_thread->wait(1000);
    }
    const bool wasConnected = m_connected;
    m_connected = false;
    m_worker = nullptr;
    if (wasConnected) {
        emit disconnected();
    }
}

void RemoteFileSession::listDirectory(const QString &path)
{
    if (!m_thread || !m_worker || !m_thread->isRunning()) {
        emit listFailed(path, tr("未连接"));
        return;
    }
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, path] { worker->list(path); },
        Qt::QueuedConnection);
}

void RemoteFileSession::upload(const QStringList &localPaths,
                               const QString &remoteDirectory)
{
    if (!m_thread || !m_worker || !m_thread->isRunning()) {
        emit operationFinished(QStringLiteral("upload"), false, tr("未连接"));
        return;
    }
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, localPaths, remoteDirectory] {
            worker->upload(localPaths, remoteDirectory);
        },
        Qt::QueuedConnection);
}

void RemoteFileSession::download(const QVector<RemoteTransferItem> &items,
                                 const QString &localDirectory)
{
    if (!m_thread || !m_worker || !m_thread->isRunning()) {
        emit operationFinished(QStringLiteral("download"), false, tr("未连接"));
        return;
    }
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, items, localDirectory] {
            worker->download(items, localDirectory);
        },
        Qt::QueuedConnection);
}

void RemoteFileSession::makeDirectory(const QString &path)
{
    if (!m_thread || !m_worker || !m_thread->isRunning()) {
        emit operationFinished(QStringLiteral("mkdir"), false, tr("未连接"));
        return;
    }
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, path] { worker->makeDirectory(path); },
        Qt::QueuedConnection);
}

void RemoteFileSession::remove(const QVector<RemoteTransferItem> &items)
{
    if (!m_thread || !m_worker || !m_thread->isRunning()) {
        emit operationFinished(QStringLiteral("remove"), false, tr("未连接"));
        return;
    }
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, items] { worker->remove(items); },
        Qt::QueuedConnection);
}

void RemoteFileSession::rename(const QString &from, const QString &to)
{
    if (!m_thread || !m_worker || !m_thread->isRunning()) {
        emit operationFinished(QStringLiteral("rename"), false, tr("未连接"));
        return;
    }
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, from, to] { worker->rename(from, to); },
        Qt::QueuedConnection);
}

void RemoteFileSession::cancel()
{
    if (m_worker) {
        m_worker->requestCancel();
    }
}

// ---------------------------------------------------------------------------
// Worker（工作线程）
// ---------------------------------------------------------------------------

RemoteFileWorker::RemoteFileWorker(const Common::ConnectionSettings &settings,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

RemoteFileWorker::~RemoteFileWorker()
{
    shutdown();
}

void RemoteFileWorker::start()
{
    m_cancel.store(false);
    if (m_client) {
        m_client->disconnect();
        m_client.reset();
    }

    switch (m_settings.type) {
    case Common::ConnectionType::FTP:
        m_client = std::make_unique<FtpFileClient>(m_settings);
        break;
    case Common::ConnectionType::SFTP:
        m_client = std::make_unique<SshFileClient>(m_settings, false);
        break;
    case Common::ConnectionType::SCP:
        m_client = std::make_unique<SshFileClient>(m_settings, true);
        break;
    default:
        emit connectionFailed(QStringLiteral("该连接类型不支持文件管理器"));
        return;
    }

    m_directorySupport = m_client->supportsDirectoryTransfer();
    QString home;
    QString error;
    if (!m_client->connect(&home, &error)) {
        emit connectionFailed(error);
        m_client.reset();
        return;
    }
    if (home.isEmpty()) {
        home = QStringLiteral("/");
    }
    emit connected(home, m_directorySupport);
}

void RemoteFileWorker::shutdown()
{
    m_cancel.store(true);
    if (m_client) {
        m_client->disconnect();
        m_client.reset();
    }
}

void RemoteFileWorker::list(const QString &path)
{
    if (!m_client) {
        emit listFailed(path, QStringLiteral("未连接"));
        return;
    }
    QVector<RemoteFileEntry> entries;
    QString error;
    if (!m_client->listDirectory(path, &entries, &error)) {
        emit listFailed(path, error);
        return;
    }
    emit directoryListed(path, entries);
}

QString RemoteFileWorker::joinRemote(const QString &directory,
                                     const QString &name) const
{
    return joinRemotePath(directory, name);
}

bool RemoteFileWorker::transferFile(const QString &localPath,
                                    const QString &remotePath,
                                    const QString &displayName)
{
    const qint64 total = QFileInfo(localPath).size();
    emit transferStarted(displayName, total);

    ProgressThrottle throttle;
    auto progress = [this, &throttle](qint64 done, qint64 total) {
        if (m_cancel.load()) {
            return false;
        }
        if (throttle.shouldEmit(done, total)) {
            emit transferProgress(done, total);
        }
        return true;
    };

    QString error;
    const bool ok = m_client->uploadFile(localPath, remotePath, progress, &error);
    emit transferFinished(displayName, ok, ok ? QStringLiteral("上传完成") : error);
    return ok;
}

bool RemoteFileWorker::uploadPath(const QString &localPath,
                                  const QString &remoteDirectory)
{
    const QFileInfo info(localPath);
    if (info.isDir()) {
        if (!m_directorySupport) {
            emit transferFinished(info.fileName(), false,
                                  QStringLiteral("SCP 模式暂不支持目录传输"));
            return false;
        }
        return uploadDirectory(localPath,
                               joinRemote(remoteDirectory, info.fileName()));
    }
    return transferFile(localPath,
                        joinRemote(remoteDirectory, info.fileName()),
                        info.fileName());
}

bool RemoteFileWorker::uploadDirectory(const QString &localDirectory,
                                       const QString &remoteDirectory)
{
    if (m_cancel.load()) {
        return false;
    }

    QString error;
    if (!m_client->makeDirectory(remoteDirectory, &error)) {
        emit transferFinished(QFileInfo(remoteDirectory).fileName(), false, error);
        return false;
    }

    QDir dir(localDirectory);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
        QDir::DirsFirst | QDir::Name);
    bool allOk = true;
    for (const QFileInfo &entry : entries) {
        if (m_cancel.load()) {
            return false;
        }
        if (entry.isSymLink()) {
            continue;   // 避免符号链接目录递归
        }
        if (entry.isDir()) {
            if (!uploadDirectory(entry.absoluteFilePath(),
                                 joinRemote(remoteDirectory, entry.fileName()))) {
                allOk = false;
            }
        } else if (!transferFile(entry.absoluteFilePath(),
                                 joinRemote(remoteDirectory, entry.fileName()),
                                 entry.fileName())) {
            allOk = false;
        }
    }
    return allOk;
}

void RemoteFileWorker::upload(const QStringList &localPaths,
                              const QString &remoteDirectory)
{
    m_cancel.store(false);
    if (!m_client) {
        emit operationFinished(QStringLiteral("upload"), false,
                               QStringLiteral("未连接"));
        return;
    }

    bool allOk = true;
    QStringList failures;
    for (const QString &path : localPaths) {
        if (m_cancel.load()) {
            allOk = false;
            failures.append(QStringLiteral("已取消"));
            break;
        }
        if (!uploadPath(path, remoteDirectory)) {
            allOk = false;
            failures.append(QFileInfo(path).fileName());
        }
    }

    emit operationFinished(QStringLiteral("upload"), allOk,
                           allOk ? QStringLiteral("上传完成")
                                 : QStringLiteral("未完成：%1")
                                       .arg(failures.join(QStringLiteral(", "))));
}

bool RemoteFileWorker::downloadEntry(const RemoteTransferItem &item,
                                     const QString &localDirectory)
{
    if (m_cancel.load()) {
        return false;
    }

    if (item.isDir) {
        if (!m_directorySupport) {
            emit transferFinished(item.name, false,
                                  QStringLiteral("SCP 模式暂不支持目录下载"));
            return false;
        }
        return downloadDirectory(item, localDirectory, 0);
    }

    const QString localPath = QDir(localDirectory).filePath(item.name);
    emit transferStarted(item.name, item.size);
    ProgressThrottle throttle;
    auto progress = [this, &throttle](qint64 done, qint64 total) {
        if (m_cancel.load()) {
            return false;
        }
        if (throttle.shouldEmit(done, total)) {
            emit transferProgress(done, total);
        }
        return true;
    };

    QString error;
    const bool ok =
        m_client->downloadFile(item.path, localPath, progress, &error);
    emit transferFinished(item.name, ok, ok ? QStringLiteral("下载完成") : error);
    return ok;
}

bool RemoteFileWorker::downloadDirectory(const RemoteTransferItem &item,
                                         const QString &localDirectory,
                                         int depth)
{
    if (depth > 32 || m_cancel.load()) {
        return false;
    }

    const QString targetDirectory =
        QDir(localDirectory).filePath(item.name.isEmpty() ? QStringLiteral(".")
                                                          : item.name);
    if (!QDir().mkpath(targetDirectory)) {
        emit transferFinished(item.name, false,
                              QStringLiteral("无法创建本地目录 %1").arg(targetDirectory));
        return false;
    }

    QVector<RemoteFileEntry> entries;
    QString error;
    if (!m_client->listDirectory(item.path, &entries, &error)) {
        emit transferFinished(item.name, false, error);
        return false;
    }

    bool allOk = true;
    for (const RemoteFileEntry &entry : entries) {
        if (m_cancel.load()) {
            return false;
        }
        if (entry.isSymlink && entry.isDir) {
            continue;   // 避免远端符号链接目录递归
        }
        RemoteTransferItem child;
        child.name = entry.name;
        child.path = entry.path;
        child.isDir = entry.isDir;
        child.size = entry.size;
        child.mode = entry.mode;
        if (!downloadEntry(child, targetDirectory)) {
            allOk = false;
        }
    }
    return allOk;
}

void RemoteFileWorker::download(const QVector<RemoteTransferItem> &items,
                                const QString &localDirectory)
{
    m_cancel.store(false);
    if (!m_client) {
        emit operationFinished(QStringLiteral("download"), false,
                               QStringLiteral("未连接"));
        return;
    }

    QDir().mkpath(localDirectory);
    bool allOk = true;
    QStringList failures;
    for (const RemoteTransferItem &item : items) {
        if (m_cancel.load()) {
            allOk = false;
            failures.append(QStringLiteral("已取消"));
            break;
        }
        if (!downloadEntry(item, localDirectory)) {
            allOk = false;
            failures.append(item.name);
        }
    }

    emit operationFinished(QStringLiteral("download"), allOk,
                           allOk ? QStringLiteral("下载完成")
                                 : QStringLiteral("未完成：%1")
                                       .arg(failures.join(QStringLiteral(", "))));
}

bool RemoteFileWorker::removeEntry(const RemoteTransferItem &item)
{
    if (!m_client) {
        return false;
    }

    if (item.isDir) {
        QVector<RemoteFileEntry> entries;
        QString error;
        if (m_client->listDirectory(item.path, &entries, &error)) {
            for (const RemoteFileEntry &entry : entries) {
                if (m_cancel.load()) {
                    return false;
                }
                if (entry.isSymlink && entry.isDir) {
                    continue;
                }
                RemoteTransferItem child;
                child.name = entry.name;
                child.path = entry.path;
                child.isDir = entry.isDir;
                child.size = entry.size;
                if (!removeEntry(child)) {
                    return false;
                }
            }
        }
    }

    QString error;
    return m_client->removeEntry(item, &error);
}

void RemoteFileWorker::remove(const QVector<RemoteTransferItem> &items)
{
    m_cancel.store(false);
    if (!m_client) {
        emit operationFinished(QStringLiteral("remove"), false,
                               QStringLiteral("未连接"));
        return;
    }

    bool allOk = true;
    QStringList failures;
    for (const RemoteTransferItem &item : items) {
        if (m_cancel.load()) {
            allOk = false;
            break;
        }
        if (!removeEntry(item)) {
            allOk = false;
            failures.append(item.name);
        }
    }
    emit operationFinished(QStringLiteral("remove"), allOk,
                           allOk ? QStringLiteral("删除完成")
                                 : QStringLiteral("删除失败：%1")
                                       .arg(failures.join(QStringLiteral(", "))));
}

void RemoteFileWorker::makeDirectory(const QString &path)
{
    if (!m_client) {
        emit operationFinished(QStringLiteral("mkdir"), false,
                               QStringLiteral("未连接"));
        return;
    }
    QString error;
    const bool ok = m_client->makeDirectory(path, &error);
    emit operationFinished(QStringLiteral("mkdir"), ok,
                           ok ? QStringLiteral("目录已创建") : error);
}

void RemoteFileWorker::rename(const QString &from, const QString &to)
{
    if (!m_client) {
        emit operationFinished(QStringLiteral("rename"), false,
                               QStringLiteral("未连接"));
        return;
    }
    QString error;
    const bool ok = m_client->renameEntry(from, to, &error);
    emit operationFinished(QStringLiteral("rename"), ok,
                           ok ? QStringLiteral("重命名完成") : error);
}
