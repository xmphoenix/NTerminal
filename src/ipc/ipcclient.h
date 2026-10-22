#ifndef IPCCLIENT_H
#define IPCCLIENT_H

#include <QObject>
#include <QLocalSocket>
#include "ipcprotocol.h"

namespace Ipc {

class IpcClient : public QObject
{
    Q_OBJECT

public:
    explicit IpcClient(QObject *parent = nullptr);
    ~IpcClient();
    
    bool connectToServer(const QString &serverName, int timeoutMs = 5000);
    void disconnectFromServer();
    bool isConnected() const;
    
    void sendMessage(MessageType type, const QByteArray &payload);
    void sendMessage(MessageType type, const QJsonObject &json);
    void sendMessage(MessageType type, const QString &text);
    
    QString serverName() const;
    
signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
    void messageReceived(MessageType type, const QByteArray &payload);
    
private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError error);
    
private:
    QLocalSocket *m_socket;
    IpcProtocol *m_protocol;
    QString m_serverName;
};

}

#endif // IPCCLIENT_H
