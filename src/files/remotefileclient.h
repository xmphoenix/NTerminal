#ifndef REMOTEFILECLIENT_H
#define REMOTEFILECLIENT_H

#include <functional>
#include "files/remotefileentry.h"

// ============================================================================
// 远端文件客户端抽象
//
// 所有方法都是阻塞调用，由 RemoteFileWorker 在工作线程中执行。
// progress 回调返回 false 表示用户取消，客户端应尽快中止当前操作。
// ============================================================================
class RemoteFileClient
{
public:
    using Progress = std::function<bool(qint64 done, qint64 total)>;

    virtual ~RemoteFileClient() = default;

    virtual bool connect(QString *home, QString *error) = 0;
    virtual void disconnect() = 0;

    virtual bool listDirectory(const QString &path,
                               QVector<RemoteFileEntry> *entries,
                               QString *error) = 0;

    virtual bool downloadFile(const QString &remotePath,
                              const QString &localPath,
                              const Progress &progress,
                              QString *error) = 0;
    virtual bool uploadFile(const QString &localPath,
                            const QString &remotePath,
                            const Progress &progress,
                            QString *error) = 0;

    virtual bool makeDirectory(const QString &path, QString *error) = 0;
    virtual bool removeEntry(const RemoteTransferItem &item, QString *error) = 0;
    virtual bool renameEntry(const QString &from, const QString &to,
                             QString *error) = 0;

    // SCP 模式只传文件，目录由远端 shell/SFTP 处理，当前版本关闭目录传输
    virtual bool supportsDirectoryTransfer() const { return true; }
};

#endif // REMOTEFILECLIENT_H
