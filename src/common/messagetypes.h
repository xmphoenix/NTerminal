#ifndef MESSAGETYPES_H
#define MESSAGETYPES_H

#include <QtGlobal>
#include <QByteArray>
#include <QDataStream>

namespace Ipc {

enum class MessageType : quint8 {
    Invalid = 0x00,
    
    DataReceived = 0x01,
    DataToSend = 0x02,
    CursorUpdate = 0x03,
    ScreenUpdate = 0x04,
    ConnectionStatus = 0x05,
    TerminalSize = 0x06,
    KeyEvent = 0x07,
    SessionConfig = 0x08,
    LogEntry = 0x09,
    ScrollRegion = 0x0A,
    Attributes = 0x0B,
    ClearScreen = 0x0C,
    ClearLine = 0x0D,
    TitleChange = 0x0E,
    Bell = 0x0F,
    SystemStats = 0x10,
    Shutdown = 0xFF
};

enum class ConnectionStatus : quint8 {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Error = 3,
    Closed = 4
};

struct MessageHeader {
    quint32 length;
    MessageType type;
    
    MessageHeader() : length(0), type(MessageType::Invalid) {}
    MessageHeader(quint32 len, MessageType t) : length(len), type(t) {}
};

const int HEADER_SIZE = sizeof(quint32) + sizeof(quint8);

inline QByteArray encodeMessage(MessageType type, const QByteArray &payload) {
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(payload.size());
    stream << static_cast<quint8>(type);
    message.append(payload);
    return message;
}

inline bool decodeHeader(const QByteArray &data, MessageHeader &header, int offset = 0) {
    if (data.size() < offset + HEADER_SIZE) {
        return false;
    }
    
    QByteArray headerData = data.mid(offset, HEADER_SIZE);
    QDataStream stream(&headerData, QIODevice::ReadOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> header.length;
    quint8 typeVal;
    stream >> typeVal;
    header.type = static_cast<MessageType>(typeVal);
    
    return true;
}

}

Q_DECLARE_METATYPE(Ipc::MessageType)
Q_DECLARE_METATYPE(Ipc::ConnectionStatus)

#endif // MESSAGETYPES_H
