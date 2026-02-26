#ifndef IPCPROTOCOL_H
#define IPCPROTOCOL_H

#include <QObject>
#include <QLocalSocket>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDataStream>
#include "common/messagetypes.h"

namespace Ipc {

class IpcProtocol : public QObject
{
    Q_OBJECT

public:
    explicit IpcProtocol(QObject *parent = nullptr);
    
    void setSocket(QLocalSocket *socket);
    void processData(const QByteArray &data);
    
    void sendMessage(MessageType type, const QByteArray &payload);
    void sendMessage(MessageType type, const QJsonObject &json);
    void sendMessage(MessageType type, const QString &text);
    
    static QByteArray encodeMessage(MessageType type, const QByteArray &payload);
    static QByteArray encodeMessage(MessageType type, const QJsonObject &json);
    static QByteArray encodeMessage(MessageType type, const QString &text);
    
    bool hasCompleteMessage() const;
    QByteArray takeNextMessage();
    MessageType takeNextMessageType();
    
signals:
    void messageReceived(MessageType type, const QByteArray &payload);
    void dataReceived(const QByteArray &data);
    
private:
    QLocalSocket *m_socket;
    QByteArray m_buffer;
    quint32 m_expectedLength;
    MessageType m_currentType;
    
    void tryParseMessages();
};

}

#endif // IPCPROTOCOL_H
