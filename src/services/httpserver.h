#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "services/serverservice.h"

#include <QHash>

class QTcpServer;
class QTcpSocket;

// HTTP 静态文件服务（默认 8080）
// 配置：root 根目录 / port 端口 / listDirectory 是否允许目录浏览
class HttpServer : public ServerService
{
    Q_OBJECT

public:
    explicit HttpServer(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("http"); }
    QString displayName() const override;
    QString description() const override;
    int defaultPort() const override { return 8080; }

protected:
    QJsonObject defaultConfig() const override;
    bool startServer() override;
    void stopServer() override;

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void handleRequest(QTcpSocket *socket, const QByteArray &request);

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

#endif // HTTPSERVER_H
