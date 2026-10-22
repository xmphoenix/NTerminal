#ifndef SFTPHANDLER_H
#define SFTPHANDLER_H

#include "connectionhandler.h"
#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <QSocketNotifier>
#include <QFile>
#include <QElapsedTimer>

class SftpHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit SftpHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~SftpHandler();

    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    void setTerminalSize(int columns, int rows) override;

private slots:
    void onSocketActivated(int socket);

private:
    bool authenticate();
    bool setupSftp();
    void cleanup();
    void processCommand(const QString &command);
    bool handleLocalCommand(const QString &cmd, const QStringList &args);
    void showHelp();
    QString formatSize(qint64 bytes) const;
    QString formatTransferSpeed(qint64 bytes, qint64 ms) const;
    QString formatPermissions(uint32_t mode) const;
    
    // SFTP operations
    void doListDirectory(const QString &path, bool longFormat = true);
    void doChangeDirectory(const QString &path);
    void doPrintWorkingDirectory();
    void doMakeDirectory(const QString &path);
    void doRemoveDirectory(const QString &path);
    void doRemoveFile(const QString &path);
    void doRename(const QString &oldPath, const QString &newPath);
    void doGetFile(const QString &remotePath, const QString &localPath);
    void doPutFile(const QString &localPath, const QString &remotePath);
    void doGetMultipleFiles(const QStringList &files);
    void doPutMultipleFiles(const QStringList &files);
    void doStat(const QString &path);
    QStringList expandLocalFiles(const QString &pattern);
    QString resolveRemotePath(const QString &path);
    
    ssh_session m_session;
    sftp_session m_sftp;
    Common::ConnectionSettings m_settings;
    QSocketNotifier *m_notifier;
    bool m_connected;
    QString m_commandBuffer;
    
    // Local state
    QString m_localDir;
    QString m_remoteDir;      // Current remote directory
    
    // Transfer state
    qint64 m_transferBlockSize;
    bool m_promptMode;
};

#endif // SFTPHANDLER_H