#ifndef NFSSERVER_H
#define NFSSERVER_H

#include "services/serverservice.h"

#include <QHash>

class QTcpServer;
class QTcpSocket;
class NfsProtocol;

// NFS v3 只读服务（默认 2049，TCP）
//
// 为了让 Linux 客户端免装 rpcbind，本服务把 MOUNT 协议（100005 v3）和
// NFS 协议（100003 v3）放在同一个端口上：客户端用
//   mount -t nfs -o vers=3,proto=tcp,port=2049,mountport=2049 ...
// 即可挂载。服务只实现只读过程（GETATTR/LOOKUP/READ/READDIR/…），
// 写操作一律返回 ROFS。
class NfsServer : public ServerService
{
    Q_OBJECT

public:
    explicit NfsServer(QObject *parent = nullptr);
    ~NfsServer() override;

    QString id() const override { return QStringLiteral("nfs"); }
    QString displayName() const override;
    QString description() const override;
    int defaultPort() const override { return 2049; }

protected:
    QJsonObject defaultConfig() const override;
    bool startServer() override;
    void stopServer() override;

private:
    struct Connection {
        QByteArray buffer;    // 尚未解析的 RPC 记录
        QByteArray message;   // 已收齐的分片（最后一片到达后处理）
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void dropConnection(QTcpSocket *socket);

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Connection> m_connections;
    NfsProtocol *m_protocol = nullptr;
};

#endif // NFSSERVER_H
