#ifndef IPCSERVER_H
#define IPCSERVER_H

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMap>
#include "ipcprotocol.h"

namespace Ipc {

class IpcServer : public QObject
{
    Q_OBJECT

public:
    explicit IpcServer(QObject *parent = nullptr);
    ~IpcServer();
    
    bool start(const QString &serverName);
    void stop();
    bool isListening() const;
    QString serverName() const;
    
    void sendMessage(MessageType type, const QByteArray &payload);
    void sendMessage(MessageType type, const QJsonObject &json);
    void sendMessage(MessageType type, const QString &text);
    
signals:
    void clientConnected();
    void clientDisconnected();
    void errorOccurred(const QString &error);
    void messageReceived(MessageType type, const QByteArray &payload);
    
private slots:
    void onNewConnection();
    void onSocketDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError error);
    void onProtocolMessageReceived(MessageType type, const QByteArray &payload);
    
private:
    QLocalServer *m_server;
    QLocalSocket *m_clientSocket;
    IpcProtocol *m_protocol;
    QString m_serverName;
};

}

#endif // IPCSERVER_H
