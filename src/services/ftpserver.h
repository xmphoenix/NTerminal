#ifndef FTPSERVER_H
#define FTPSERVER_H

#include "services/serverservice.h"

class QTcpServer;
class FtpSession;

// FTP 服务（默认 21）
// 配置：root 根目录 / port 端口 / allowAnonymous 匿名访问 /
//       user+password 账号（关闭匿名时使用）/ allowWrite 是否允许写入
class FtpServer : public ServerService
{
    Q_OBJECT

public:
    explicit FtpServer(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("ftp"); }
    QString displayName() const override;
    QString description() const override;
    int defaultPort() const override { return 21; }

protected:
    QJsonObject defaultConfig() const override;
    bool startServer() override;
    void stopServer() override;

private:
    void onNewConnection();

    QTcpServer *m_server = nullptr;
    QList<FtpSession *> m_sessions;
};

#endif // FTPSERVER_H
