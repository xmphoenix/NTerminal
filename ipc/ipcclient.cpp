#include "ipcclient.h"
#include <QDebug>

namespace Ipc {

IpcClient::IpcClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_protocol(new IpcProtocol(this))
{
    connect(m_socket, &QLocalSocket::connected, this, &IpcClient::onSocketConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &IpcClient::onSocketDisconnected);
    connect(m_socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::errorOccurred),
            this, &IpcClient::onSocketError);
    
    connect(m_protocol, &IpcProtocol::messageReceived, this, &IpcClient::messageReceived);
    
    m_protocol->setSocket(m_socket);
}

IpcClient::~IpcClient()
{
    disconnectFromServer();
}

bool IpcClient::connectToServer(const QString &serverName, int timeoutMs)
{
    m_serverName = serverName;
    
    m_socket->connectToServer(serverName);
    
    if (!m_socket->waitForConnected(timeoutMs)) {
        qWarning() << "[IpcClient] Failed to connect to server:" << serverName 
                   << "Error:" << m_socket->errorString();
        return false;
    }
    
    qDebug() << "[IpcClient] Connected to server:" << serverName 
             << "Socket state:" << m_socket->state();
    
    return true;
}

void IpcClient::disconnectFromServer()
{
    if (m_socket && m_socket->state() != QLocalSocket::UnconnectedState) {
        m_socket->disconnectFromServer();
    }
}

bool IpcClient::isConnected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

void IpcClient::sendMessage(MessageType type, const QByteArray &payload)
{
    if (!m_socket) {
        qWarning() << "[IpcClient] Cannot send message: socket is null";
        return;
    }
    
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        qWarning() << "[IpcClient] Cannot send message: socket state =" << m_socket->state();
        return;
    }
    
    m_protocol->sendMessage(type, payload);
}

void IpcClient::sendMessage(MessageType type, const QJsonObject &json)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
        qWarning() << "[IpcClient] Cannot send message: not connected";
        return;
    }
    
    m_protocol->sendMessage(type, json);
}

void IpcClient::sendMessage(MessageType type, const QString &text)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
        qWarning() << "[IpcClient] Cannot send message: not connected";
        return;
    }
    
    m_protocol->sendMessage(type, text);
}

QString IpcClient::serverName() const
{
    return m_serverName;
}

void IpcClient::onSocketConnected()
{
    qDebug() << "[IpcClient] Socket connected signal, state:" << m_socket->state();
    emit connected();
}

void IpcClient::onSocketDisconnected()
{
    qDebug() << "[IpcClient] Socket disconnected";
    emit disconnected();
}

void IpcClient::onSocketError(QLocalSocket::LocalSocketError error)
{
    QString errorString = m_socket->errorString();
    qWarning() << "[IpcClient] Socket error:" << error << errorString;
    emit errorOccurred(errorString);
}

}
