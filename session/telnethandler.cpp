#include "telnethandler.h"
#include <QDebug>

namespace {
    const quint8 TELNET_IAC = 255;
    const quint8 TELNET_WILL = 251;
    const quint8 TELNET_WONT = 252;
    const quint8 TELNET_DO = 253;
    const quint8 TELNET_DONT = 254;
    const quint8 TELNET_SB = 250;
    const quint8 TELNET_SE = 240;
    const quint8 TELNET_ECHO = 1;
    const quint8 TELNET_SGA = 3;
    const quint8 TELNET_TTYPE = 24;
    const quint8 TELNET_NAWS = 31;
    const quint8 TELNET_IS = 0;
}

TelnetHandler::TelnetHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_socket(new QTcpSocket(this))
    , m_settings(settings)
    , m_negotiating(false)
    , m_loginState(LoginNone)
    , m_columns(200)
    , m_rows(24)
{
    connect(m_socket, &QTcpSocket::connected, this, &TelnetHandler::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TelnetHandler::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TelnetHandler::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &TelnetHandler::onError);
}

TelnetHandler::~TelnetHandler()
{
    close();
}

bool TelnetHandler::open()
{
    qDebug() << "[TelnetHandler] Connecting to" << m_settings.host << ":" << m_settings.port;
    m_socket->connectToHost(m_settings.host, m_settings.port);
    return true;
}

void TelnetHandler::close()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool TelnetHandler::isOpen() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TelnetHandler::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void TelnetHandler::sendData(const QByteArray &data)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        QByteArray escapedData;
        for (char c : data) {
            if (static_cast<unsigned char>(c) == TELNET_IAC) {
                escapedData.append(static_cast<char>(TELNET_IAC));
                escapedData.append(static_cast<char>(TELNET_IAC));
            } else {
                escapedData.append(c);
            }
        }
        m_socket->write(escapedData);
        m_socket->flush();
        qDebug() << "[TelnetHandler] Sent" << escapedData.size() << "bytes";
    }
}

QString TelnetHandler::connectionInfo() const
{
    return QString("Telnet: %1:%2").arg(m_settings.host).arg(m_settings.port);
}

void TelnetHandler::onConnected()
{
    qDebug() << "[TelnetHandler] Connected";
    m_negotiating = true;
    m_loginState = LoginWaitPrompt;
    emit connected();
}

void TelnetHandler::onDisconnected()
{
    qDebug() << "[TelnetHandler] Disconnected";
    m_loginState = LoginNone;
    emit disconnected();
}

void TelnetHandler::onReadyRead()
{
    QByteArray data = m_socket->readAll();
    qDebug() << "[TelnetHandler] Received" << data.size() << "bytes";
    
    QByteArray processedData = processTelnetProtocol(data);
    
    if (!processedData.isEmpty()) {
        m_receiveBuffer.append(QString::fromUtf8(processedData));
        
        qDebug() << "[TelnetHandler] LoginState:" << m_loginState 
                 << "Buffer size:" << m_receiveBuffer.size();
        
        if (m_loginState == LoginWaitPrompt) {
            if (!m_settings.username.isEmpty()) {
                if (m_receiveBuffer.contains("login:") || m_receiveBuffer.contains("Login:") ||
                    m_receiveBuffer.contains("Username:") || m_receiveBuffer.contains("username:")) {
                    qDebug() << "[TelnetHandler] Sending username:" << m_settings.username;
                    m_socket->write((m_settings.username + "\r\n").toUtf8());
                    m_loginState = LoginSentUser;
                    m_receiveBuffer.clear();
                }
            } else {
                m_loginState = LoginDone;
            }
        } else if (m_loginState == LoginSentUser) {
            if (!m_settings.password.isEmpty()) {
                if (m_receiveBuffer.contains("Password:") || m_receiveBuffer.contains("password:")) {
                    qDebug() << "[TelnetHandler] Sending password";
                    m_socket->write((m_settings.password + "\r\n").toUtf8());
                    m_loginState = LoginSentPassword;
                    m_receiveBuffer.clear();
                }
            } else {
                m_loginState = LoginDone;
            }
        } else if (m_loginState == LoginSentPassword) {
            m_loginState = LoginDone;
            m_receiveBuffer.clear();
        }
        
        emit dataReceived(processedData);
    }
}

void TelnetHandler::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errorString = m_socket->errorString();
    qWarning() << "[TelnetHandler] Error:" << errorString;
    emit errorOccurred(errorString);
}

QByteArray TelnetHandler::processTelnetProtocol(const QByteArray &data)
{
    QByteArray result;
    int i = 0;
    
    while (i < data.size()) {
        unsigned char ch = static_cast<unsigned char>(data[i]);
        
        if (ch == TELNET_IAC) {
            if (i + 1 >= data.size()) {
                break;
            }
            
            unsigned char cmd = static_cast<unsigned char>(data[i + 1]);
            
            if (cmd == TELNET_WILL || cmd == TELNET_WONT ||
                cmd == TELNET_DO || cmd == TELNET_DONT) {
                if (i + 2 >= data.size()) {
                    break;
                }
                
                unsigned char option = static_cast<unsigned char>(data[i + 2]);
                
                if (cmd == TELNET_DO) {
                    if (option == TELNET_ECHO) {
                        sendTelnetCommand(TELNET_WONT, option);
                        qDebug() << "[TelnetHandler] DO ECHO -> WONT ECHO";
                    } else if (option == TELNET_SGA) {
                        sendTelnetCommand(TELNET_WILL, option);
                        qDebug() << "[TelnetHandler] DO SGA -> WILL SGA";
                    } else if (option == TELNET_TTYPE) {
                        sendTelnetCommand(TELNET_WILL, option);
                        qDebug() << "[TelnetHandler] DO TTYPE -> WILL TTYPE";
                    } else if (option == TELNET_NAWS) {
                        sendTelnetCommand(TELNET_WILL, option);
                        sendNaws();
                        qDebug() << "[TelnetHandler] DO NAWS -> WILL NAWS + send NAWS";
                    } else {
                        sendTelnetCommand(TELNET_WONT, option);
                        qDebug() << "[TelnetHandler] DO" << option << "-> WONT";
                    }
                } else if (cmd == TELNET_WILL) {
                    if (option == TELNET_ECHO || option == TELNET_SGA) {
                        sendTelnetCommand(TELNET_DO, option);
                        qDebug() << "[TelnetHandler] WILL" << option << "-> DO";
                    } else {
                        sendTelnetCommand(TELNET_DONT, option);
                        qDebug() << "[TelnetHandler] WILL" << option << "-> DONT";
                    }
                }
                
                i += 3;
                continue;
            } else if (cmd == TELNET_SB) {
                int end = data.indexOf(static_cast<char>(TELNET_SE), i + 2);
                if (end != -1) {
                    QByteArray subData = data.mid(i + 2, end - i - 2);
                    handleSubnegotiation(subData);
                    i = end + 1;
                    continue;
                }
                i++;
                continue;
            } else if (cmd == TELNET_IAC) {
                result.append(static_cast<char>(TELNET_IAC));
                i += 2;
                continue;
            } else {
                i += 2;
                continue;
            }
        } else {
            result.append(static_cast<char>(ch));
            i++;
        }
    }
    
    return result;
}

void TelnetHandler::handleSubnegotiation(const QByteArray &data)
{
    if (data.isEmpty()) return;
    
    unsigned char option = static_cast<unsigned char>(data[0]);
    
    if (option == TELNET_TTYPE && data.size() >= 2 && static_cast<unsigned char>(data[1]) == 1) {
        QByteArray response;
        response.append(static_cast<char>(TELNET_IAC));
        response.append(static_cast<char>(TELNET_SB));
        response.append(static_cast<char>(TELNET_TTYPE));
        response.append(static_cast<char>(TELNET_IS));
        response.append("xterm-256color");
        response.append(static_cast<char>(TELNET_IAC));
        response.append(static_cast<char>(TELNET_SE));
        m_socket->write(response);
        qDebug() << "[TelnetHandler] Sent TTYPE: xterm-256color";
    }
}

void TelnetHandler::sendTelnetCommand(quint8 command, quint8 option)
{
    QByteArray cmd;
    cmd.append(static_cast<char>(TELNET_IAC));
    cmd.append(static_cast<char>(command));
    cmd.append(static_cast<char>(option));
    m_socket->write(cmd);
}

void TelnetHandler::sendNaws()
{
    QByteArray naws;
    naws.append(static_cast<char>(TELNET_IAC));
    naws.append(static_cast<char>(TELNET_SB));
    naws.append(static_cast<char>(TELNET_NAWS));
    naws.append(static_cast<char>((m_columns >> 8) & 0xFF));
    naws.append(static_cast<char>(m_columns & 0xFF));
    naws.append(static_cast<char>((m_rows >> 8) & 0xFF));
    naws.append(static_cast<char>(m_rows & 0xFF));
    naws.append(static_cast<char>(TELNET_IAC));
    naws.append(static_cast<char>(TELNET_SE));
    m_socket->write(naws);
    qDebug() << "[TelnetHandler] Sent NAWS:" << m_columns << "x" << m_rows;
}
