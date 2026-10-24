#ifndef FTP_FILE_CLIENT_H
#define FTP_FILE_CLIENT_H

#include "files/remotefileclient.h"
#include "common/connectionsettings.h"

#include <QStringList>

class QTcpSocket;

// 同步 FTP 客户端（在 RemoteFileWorker 的工作线程中调用）
// 使用被动模式（PASV），支持 LIST/RETR/STOR/MKD/RMD/DELE/RNFR/RNTO。
class FtpFileClient : public RemoteFileClient
{
public:
    explicit FtpFileClient(const Common::ConnectionSettings &settings);
    ~FtpFileClient() override;

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

private:
    bool readResponse(int *code, QString *response, QString *error);
    bool command(const QString &text, const QStringList &expectedCodes,
                 QString *response, QString *error);
    bool login(QString *error);
    bool enterPassive(QString *host, quint16 *port, QString *error);
    bool openDataConnection(const QString &host, quint16 port, QString *error);
    void closeDataConnection();
    bool readDataToFile(const QString &remotePath, const QString &localPath,
                        const Progress &progress, QString *error);
    bool writeFileToData(const QString &localPath, const QString &remotePath,
                         const Progress &progress, QString *error);
    QString parsePwd(const QString &response) const;
    QVector<RemoteFileEntry> parseList(const QByteArray &data,
                                       const QString &basePath) const;

    Common::ConnectionSettings m_settings;
    QTcpSocket *m_control = nullptr;
    QTcpSocket *m_data = nullptr;
    QString m_lastResponse;
};

#endif // FTP_FILE_CLIENT_H
