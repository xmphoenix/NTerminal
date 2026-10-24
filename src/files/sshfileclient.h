#ifndef SSH_FILE_CLIENT_H
#define SSH_FILE_CLIENT_H

#include "files/remotefileclient.h"
#include "common/connectionsettings.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

// SFTP / SCP 文件客户端（在同一 SSH 会话上工作）
//
// 浏览目录始终使用 SFTP；SCP 模式下文件上传/下载使用 SCP，
// 因此 SCP 模式也要求服务器开启 SFTP 子系统（大多数 OpenSSH 默认开启）。
class SshFileClient : public RemoteFileClient
{
public:
    explicit SshFileClient(const Common::ConnectionSettings &settings,
                           bool useScp);
    ~SshFileClient() override;

    bool connect(QString *home, QString *error) override;
    void disconnect() override;

    bool listDirectory(const QString &path,
                       QVector<RemoteFileEntry> *entries,
                       QString *error) override;
    bool downloadFile(const QString &remotePath, const QString &localPath,
                      const Progress &progress, QString *error) override;
    bool uploadFile(const QString &localPath, const QString &remotePath,
                    const Progress &progress, QString *error) override;

    bool makeDirectory(const QString &path, QString *error) override;
    bool removeEntry(const RemoteTransferItem &item, QString *error) override;
    bool renameEntry(const QString &from, const QString &to,
                     QString *error) override;

    bool supportsDirectoryTransfer() const override { return !m_useScp; }

private:
    bool authenticate(QString *error);
    bool setupSftp(QString *error);
    QString joinRemote(const QString &directory, const QString &name) const;
    bool downloadSftp(const QString &remotePath, const QString &localPath,
                      const Progress &progress, QString *error);
    bool uploadSftp(const QString &localPath, const QString &remotePath,
                    const Progress &progress, QString *error);
    bool downloadScp(const QString &remotePath, const QString &localPath,
                     const Progress &progress, QString *error);
    bool uploadScp(const QString &localPath, const QString &remotePath,
                   const Progress &progress, QString *error);
    QString lastError() const;

    Common::ConnectionSettings m_settings;
    bool m_useScp = false;
    ssh_session m_session = nullptr;
    sftp_session m_sftp = nullptr;
};

#endif // SSH_FILE_CLIENT_H
