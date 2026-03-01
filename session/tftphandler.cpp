#include "tftphandler.h"
#include <QDebug>

TftpHandler::TftpHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_socket(new QUdpSocket(this))
    , m_settings(settings)
    , m_connected(false)
{
    connect(m_socket, &QUdpSocket::readyRead, this, &TftpHandler::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &TftpHandler::onError);
}

TftpHandler::~TftpHandler()
{
    close();
}

bool TftpHandler::open()
{
    if (m_connected) {
        return true;
    }

    if (m_socket->bind()) {
        m_connected = true;
        qDebug() << "[TftpHandler] Socket bound, ready to communicate with" << m_settings.host << ":" << m_settings.port;
        emit connected();
        emit dataReceived("TFTP session ready. Type 'help' for available commands.\r\n");
        emit dataReceived("tftp> ");
        return true;
    }

    emit errorOccurred("Failed to bind UDP socket");
    return false;
}

void TftpHandler::close()
{
    if (m_socket->isOpen()) {
        m_socket->close();
    }
    m_connected = false;
    emit disconnected();
}

bool TftpHandler::isOpen() const
{
    return m_socket->isOpen();
}

bool TftpHandler::isConnected() const
{
    return m_connected;
}

void TftpHandler::sendData(const QByteArray &data)
{
    if (!m_connected) {
        return;
    }

    // Buffer the incoming data - terminal sends characters one by one
    QString input = QString::fromUtf8(data);

    for (const QChar &ch : input) {
        if (ch == '\r' || ch == '\n') {
            // Enter key pressed - process the complete command
            QString command = m_commandBuffer.trimmed();
            m_commandBuffer.clear();

            // Echo newline to terminal
            emit dataReceived("\r\n");

            if (command.isEmpty()) {
                emit dataReceived("tftp> ");
                continue;
            }

            QStringList parts = command.split(' ', Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                emit dataReceived("tftp> ");
                continue;
            }

            QString cmd = parts[0].toLower();
            QString result;

            if (cmd == "help") {
                result = "Available commands:\r\n";
                result += "  get <file>   - Download a file from server\r\n";
                result += "  put <file>   - Upload a file to server\r\n";
                result += "  help         - Show this help\r\n";
                result += "  quit/exit    - Close connection\r\n";
                result += "\r\nNote: TFTP is a simple protocol. Actual file transfer\r\n";
                result += "      requires implementing the full TFTP protocol.\r\n";
            } else if (cmd == "quit" || cmd == "exit" || cmd == "bye") {
                emit dataReceived("Closing TFTP session...\r\n");
                emit dataReceived("Connection closed.\r\n");
                close();
                return;  // Don't show prompt after quit
            } else if (cmd == "get" || cmd == "put") {
                result = QString("TFTP %1 command not yet fully implemented.\r\n").arg(cmd);
                result += "This requires implementing the TFTP protocol (RRQ/WRQ/DATA/ACK).\r\n";
            } else {
                // Send raw data as TFTP packet
                m_socket->writeDatagram(command.toUtf8(), QHostAddress(m_settings.host), m_settings.port);
                result = QString("Sent raw TFTP packet (%1 bytes)\r\n").arg(command.size());
            }

            emit dataReceived(result.toUtf8());
            emit dataReceived("tftp> ");
        } else if (ch == '\b' || ch == '\x7f') {
            // Backspace - remove last character from buffer
            if (!m_commandBuffer.isEmpty()) {
                m_commandBuffer.chop(1);
                // Echo backspace to terminal (move back, clear, move back)
                emit dataReceived("\b \b");
            }
        } else if (ch.isPrint() || ch == ' ') {
            // Regular character - add to buffer and echo
            m_commandBuffer.append(ch);
            emit dataReceived(QString(ch).toUtf8());
        }
        // Ignore other control characters
    }
}

QString TftpHandler::connectionInfo() const
{
    return QString("TFTP - %1:%2").arg(m_settings.host).arg(m_settings.port);
}

void TftpHandler::setTerminalSize(int columns, int rows)
{
    Q_UNUSED(columns);
    Q_UNUSED(rows);
}

void TftpHandler::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;

        m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        emit dataReceived(datagram);
    }
}

void TftpHandler::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errorMsg = m_socket->errorString();
    qWarning() << "[TftpHandler] Error:" << errorMsg;
    emit errorOccurred(errorMsg);
}
