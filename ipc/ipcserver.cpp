#include "ipcserver.h"
#include <QDebug>

namespace Ipc {

IpcServer::IpcServer(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
    , m_clientSocket(nullptr)
    , m_protocol(new IpcProtocol(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
    connect(m_protocol, &IpcProtocol::messageReceived, this, &IpcServer::onProtocolMessageReceived);
}

IpcServer::~IpcServer()
{
    stop();
}

bool IpcServer::start(const QString &serverName)
{
    m_serverName = serverName;
    
    QLocalServer::removeServer(serverName);
    
    if (!m_server->listen(serverName)) {
        qWarning() << "[IpcServer] Failed to start server:" << serverName 
                   << "Error:" << m_server->errorString();
        emit errorOccurred(m_server->errorString());
        return false;
    }
    
    qDebug() << "[IpcServer] Server started:" << serverName;
    return true;
}

void IpcServer::stop()
{
    if (m_clientSocket) {
        m_clientSocket->disconnectFromServer();
        m_clientSocket = nullptr;
    }
    
    if (m_server->isListening()) {
        m_server->close();
    }
    
    qDebug() << "[IpcServer] Server stopped";
}

bool IpcServer::isListening() const
{
    return m_server->isListening();
}

QString IpcServer::serverName() const
{
    return m_serverName;
}

void IpcServer::sendMessage(MessageType type, const QByteArray &payload)
{
    if (m_protocol && m_clientSocket) {
        m_protocol->sendMessage(type, payload);
    }
}

void IpcServer::sendMessage(MessageType type, const QJsonObject &json)
{
    if (m_protocol && m_clientSocket) {
        m_protocol->sendMessage(type, json);
    }
}

void IpcServer::sendMessage(MessageType type, const QString &text)
{
    if (m_protocol && m_clientSocket) {
        m_protocol->sendMessage(type, text);
    }
}

void IpcServer::onNewConnection()
{
    QLocalSocket *socket = m_server->nextPendingConnection();
    if (!socket) {
        return;
    }
    
    if (m_clientSocket) {
        qWarning() << "[IpcServer] Rejecting additional connection, already have a client";
        socket->disconnectFromServer();
        socket->deleteLater();
        return;
    }
    
    m_clientSocket = socket;
    m_protocol->setSocket(m_clientSocket);
    
    connect(m_clientSocket, &QLocalSocket::disconnected, this, &IpcServer::onSocketDisconnected);
    connect(m_clientSocket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::errorOccurred),
            this, &IpcServer::onSocketError);
    
    qDebug() << "[IpcServer] Client connected";
    emit clientConnected();
}

void IpcServer::onSocketDisconnected()
{
    qDebug() << "[IpcServer] Client disconnected";
    m_clientSocket = nullptr;
    m_protocol->setSocket(nullptr);
    emit clientDisconnected();
}

void IpcServer::onSocketError(QLocalSocket::LocalSocketError error)
{
    if (m_clientSocket) {
        QString errorString = m_clientSocket->errorString();
        qWarning() << "[IpcServer] Socket error:" << error << errorString;
        emit errorOccurred(errorString);
    }
}

void IpcServer::onProtocolMessageReceived(MessageType type, const QByteArray &payload)
{
    emit messageReceived(type, payload);
}

}
