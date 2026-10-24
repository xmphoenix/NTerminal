#ifndef TFTPSERVER_H
#define TFTPSERVER_H

#include "services/serverservice.h"

class QUdpSocket;
class TftpSession;

// TFTP 服务（UDP，默认 69）
// 配置：root 根目录 / port 端口 / allowWrite 是否允许上传（WRQ）
class TftpServer : public ServerService
{
    Q_OBJECT

public:
    explicit TftpServer(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("tftp"); }
    QString displayName() const override;
    QString description() const override;
    int defaultPort() const override { return 69; }

protected:
    QJsonObject defaultConfig() const override;
    bool startServer() override;
    void stopServer() override;

private:
    void onReadyRead();
    void removeSession(TftpSession *session);

    QUdpSocket *m_socket = nullptr;
    QList<TftpSession *> m_sessions;
};

#endif // TFTPSERVER_H
