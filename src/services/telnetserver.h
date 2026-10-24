#ifndef TELNETSERVER_H
#define TELNETSERVER_H

#include "services/serverservice.h"

class QTcpServer;
class TelnetSession;

// Telnet 服务（默认 23）：向 telnet 客户端提供命令 shell
// 配置：port 端口
// 注意：命令以启动本程序的 Windows 用户权限执行，仅建议在可信网络使用。
class TelnetServer : public ServerService
{
    Q_OBJECT

public:
    explicit TelnetServer(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("telnet"); }
    QString displayName() const override;
    QString description() const override;
    int defaultPort() const override { return 23; }

protected:
    QJsonObject defaultConfig() const override;
    bool startServer() override;
    void stopServer() override;

private:
    void onNewConnection();

    QTcpServer *m_server = nullptr;
    QList<TelnetSession *> m_sessions;
};

#endif // TELNETSERVER_H
