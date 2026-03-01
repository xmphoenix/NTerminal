#include "rloginhandler.h"
#include <QDebug>

RloginHandler::RloginHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_socket(new QTcpSocket(this))
    , m_settings(settings)
    , m_handshakeSent(false)
    , m_columns(80)
    , m_rows(24)
{
    connect(m_socket, &QTcpSocket::connected, this, &RloginHandler::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RloginHandler::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &RloginHandler::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &RloginHandler::onError);
}

RloginHandler::~RloginHandler()
{
    close();
}

bool RloginHandler::open()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        return true;
    }

    qDebug() << "[RloginHandler] Connecting to" << m_settings.host << ":" << m_settings.port;
    m_socket->connectToHost(m_settings.host, m_settings.port);
    return true;
}

void RloginHandler::close()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool RloginHandler::isOpen() const
{
    return m_socket->state() != QAbstractSocket::UnconnectedState;
}

bool RloginHandler::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void RloginHandler::sendData(const QByteArray &data)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(data);
    }
}

QString RloginHandler::connectionInfo() const
{
    return QString("Rlogin - %1:%2").arg(m_settings.host).arg(m_settings.port);
}

void RloginHandler::setTerminalSize(int columns, int rows)
{
    m_columns = columns;
    m_rows = rows;
}

void RloginHandler::onConnected()
{
    qDebug() << "[RloginHandler] Connected";
    sendRloginHandshake();
    emit connected();
}

void RloginHandler::onDisconnected()
{
    qDebug() << "[RloginHandler] Disconnected";
    m_handshakeSent = false;
    emit disconnected();
}

void RloginHandler::onReadyRead()
{
    QByteArray data = m_socket->readAll();
    emit dataReceived(data);
}

void RloginHandler::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errorMsg = m_socket->errorString();
    qWarning() << "[RloginHandler] Error:" << errorMsg;
    emit errorOccurred(errorMsg);
}

void RloginHandler::sendRloginHandshake()
{
    if (m_handshakeSent) {
        return;
    }

    // Rlogin protocol: null + client-user-name + null + server-user-name + null + terminal-type/speed + null
    QByteArray handshake;
    handshake.append('\0');

    // Client username (local user)
    QString localUser = m_settings.username.isEmpty() ? "user" : m_settings.username;
    handshake.append(localUser.toUtf8());
    handshake.append('\0');

    // Server username (remote user)
    QString remoteUser = m_settings.username.isEmpty() ? "user" : m_settings.username;
    handshake.append(remoteUser.toUtf8());
    handshake.append('\0');

    // Terminal type and speed (e.g., "xterm/38400")
    QString termType = qgetenv("TERM");
    if (termType.isEmpty()) {
        termType = "xterm";
    }
    handshake.append(QString("%1/38400").arg(termType).toUtf8());
    handshake.append('\0');

    m_socket->write(handshake);
    m_handshakeSent = true;
    qDebug() << "[RloginHandler] Handshake sent:" << localUser << remoteUser << termType;
}

