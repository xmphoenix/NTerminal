#include "ipcprotocol.h"
#include <QDebug>

namespace Ipc {

IpcProtocol::IpcProtocol(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_expectedLength(0)
    , m_currentType(MessageType::Invalid)
{
}

void IpcProtocol::setSocket(QLocalSocket *socket)
{
    m_socket = socket;
    if (m_socket) {
        connect(m_socket, &QLocalSocket::readyRead, [this]() {
            if (m_socket) {
                QByteArray data = m_socket->readAll();
                processData(data);
            }
        });
    }
}

void IpcProtocol::processData(const QByteArray &data)
{
    m_buffer.append(data);
    emit dataReceived(data);
    tryParseMessages();
}

void IpcProtocol::sendMessage(MessageType type, const QByteArray &payload)
{
    if (!m_socket) {
        qWarning() << "[IpcProtocol] Cannot send message: socket is null";
        return;
    }
    
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        qWarning() << "[IpcProtocol] Cannot send message: socket state =" << m_socket->state();
        return;
    }
    
    QByteArray message = encodeMessage(type, payload);
    qint64 written = m_socket->write(message);
    if (written != message.size()) {
        qWarning() << "[IpcProtocol] Partial write:" << written << "of" << message.size();
    }
    m_socket->flush();
}

void IpcProtocol::sendMessage(MessageType type, const QJsonObject &json)
{
    QJsonDocument doc(json);
    sendMessage(type, doc.toJson(QJsonDocument::Compact));
}

void IpcProtocol::sendMessage(MessageType type, const QString &text)
{
    sendMessage(type, text.toUtf8());
}

QByteArray IpcProtocol::encodeMessage(MessageType type, const QByteArray &payload)
{
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(payload.size());
    stream << static_cast<quint8>(type);
    message.append(payload);
    return message;
}

QByteArray IpcProtocol::encodeMessage(MessageType type, const QJsonObject &json)
{
    QJsonDocument doc(json);
    return encodeMessage(type, doc.toJson(QJsonDocument::Compact));
}

QByteArray IpcProtocol::encodeMessage(MessageType type, const QString &text)
{
    return encodeMessage(type, text.toUtf8());
}

bool IpcProtocol::hasCompleteMessage() const
{
    const int headerSize = sizeof(quint32) + sizeof(quint8);
    if (m_buffer.size() < headerSize) {
        return false;
    }

    QByteArray headerData = m_buffer.left(headerSize);
    QDataStream stream(&headerData, QIODevice::ReadOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 length;
    stream >> length;

    // 使用 qint64 比较，防止超大/恶意长度在 32 位截断后绕过完整性检查
    return static_cast<qint64>(m_buffer.size())
           >= static_cast<qint64>(headerSize) + static_cast<qint64>(length);
}

QByteArray IpcProtocol::takeNextMessage()
{
    const int headerSize = sizeof(quint32) + sizeof(quint8);
    if (m_buffer.size() < headerSize) {
        return QByteArray();
    }

    QByteArray headerData = m_buffer.left(headerSize);
    QDataStream stream(&headerData, QIODevice::ReadOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 length;
    stream >> length;
    quint8 typeVal;
    stream >> typeVal;

    // 完整性检查同样使用 64 位比较（与 hasCompleteMessage 一致）
    if (static_cast<qint64>(m_buffer.size())
        < static_cast<qint64>(headerSize) + static_cast<qint64>(length)) {
        return QByteArray();
    }

    m_currentType = static_cast<MessageType>(typeVal);

    QByteArray payload = m_buffer.mid(headerSize, static_cast<int>(length));
    m_buffer.remove(0, headerSize + static_cast<int>(length));

    return payload;
}

MessageType IpcProtocol::takeNextMessageType()
{
    return m_currentType;
}

void IpcProtocol::tryParseMessages()
{
    while (hasCompleteMessage()) {
        QByteArray payload = takeNextMessage();
        MessageType type = takeNextMessageType();
        emit messageReceived(type, payload);
    }
}

}
