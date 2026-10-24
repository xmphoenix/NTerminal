#ifndef REMOTEFILESESSION_H
#define REMOTEFILESESSION_H

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>

#include "common/connectionsettings.h"
#include "files/remotefileclient.h"
#include "files/remotefileentry.h"

class RemoteFileWorker;

// ============================================================================
// 远端文件会话（GUI 线程侧）
//
// 持有一个工作线程；所有连接/列表/传输操作都在工作线程中阻塞执行，
// GUI 只通过信号接收结果，避免 libssh/QTcpSocket 阻塞界面。
// ============================================================================
class RemoteFileSession : public QObject
{
    Q_OBJECT

public:
    explicit RemoteFileSession(const Common::ConnectionSettings &settings,
                               QObject *parent = nullptr);
    ~RemoteFileSession() override;

    void start();
    void stop();
    bool isConnected() const { return m_connected; }
    bool supportsDirectoryTransfer() const { return m_directorySupport; }

    void listDirectory(const QString &path);
    void upload(const QStringList &localPaths, const QString &remoteDirectory);
    void download(const QVector<RemoteTransferItem> &items,
                  const QString &localDirectory);
    void makeDirectory(const QString &path);
    void remove(const QVector<RemoteTransferItem> &items);
    void rename(const QString &from, const QString &to);
    void cancel();

signals:
    void connected(const QString &home, bool supportsDirectoryTransfer);
    void connectionFailed(const QString &error);
    void directoryListed(const QString &path,
                         const QVector<RemoteFileEntry> &entries);
    void listFailed(const QString &path, const QString &error);
    void transferStarted(const QString &name, qint64 total);
    void transferProgress(qint64 done, qint64 total);
    void transferFinished(const QString &name, bool success,
                          const QString &message);
    void operationFinished(const QString &operation, bool success,
                           const QString &message);
    void disconnected();

private:
    QThread *m_thread = nullptr;
    RemoteFileWorker *m_worker = nullptr;
    bool m_connected = false;
    bool m_directorySupport = true;
};

// 工作线程内对象：创建客户端并执行阻塞操作
class RemoteFileWorker : public QObject
{
    Q_OBJECT

public:
    explicit RemoteFileWorker(const Common::ConnectionSettings &settings,
                              QObject *parent = nullptr);
    ~RemoteFileWorker() override;

    // 线程安全：GUI 线程直接调用
    void requestCancel() { m_cancel.store(true); }

public slots:
    void start();
    void shutdown();
    void list(const QString &path);
    void upload(const QStringList &localPaths, const QString &remoteDirectory);
    void download(const QVector<RemoteTransferItem> &items,
                  const QString &localDirectory);
    void makeDirectory(const QString &path);
    void remove(const QVector<RemoteTransferItem> &items);
    void rename(const QString &from, const QString &to);

signals:
    void connected(const QString &home, bool supportsDirectoryTransfer);
    void connectionFailed(const QString &error);
    void directoryListed(const QString &path,
                         const QVector<RemoteFileEntry> &entries);
    void listFailed(const QString &path, const QString &error);
    void transferStarted(const QString &name, qint64 total);
    void transferProgress(qint64 done, qint64 total);
    void transferFinished(const QString &name, bool success,
                          const QString &message);
    void operationFinished(const QString &operation, bool success,
                           const QString &message);

private:
    bool uploadPath(const QString &localPath, const QString &remoteDirectory);
    bool uploadDirectory(const QString &localDirectory,
                         const QString &remoteDirectory);
    bool downloadEntry(const RemoteTransferItem &item,
                       const QString &localDirectory);
    bool downloadDirectory(const RemoteTransferItem &item,
                           const QString &localDirectory, int depth);
    bool removeEntry(const RemoteTransferItem &item);
    QString joinRemote(const QString &directory, const QString &name) const;
    bool transferFile(const QString &localPath, const QString &remotePath,
                      const QString &displayName);

    Common::ConnectionSettings m_settings;
    std::unique_ptr<RemoteFileClient> m_client;
    std::atomic_bool m_cancel{false};
    bool m_directorySupport = true;
};

#endif // REMOTEFILESESSION_H
