#include "sessionprocess.h"
#include "connectionhandler.h"
#include "sessionlogger.h"
#include <QDebug>
#include <QJsonDocument>
#include <QCoreApplication>

SessionProcess::SessionProcess(const QString &sessionId, QObject *parent)
    : QObject(parent)
    , m_sessionId(sessionId)
    , m_ipcServer(new Ipc::IpcServer(this))
    , m_connectionHandler(nullptr)
    , m_logger(nullptr)
    , m_initialized(false)
    , m_running(false)
    , m_columns(200)
    , m_rows(24)
{
}

SessionProcess::~SessionProcess()
{
    if (m_connectionHandler) {
        m_connectionHandler->close();
        delete m_connectionHandler;
    }
    if (m_logger) {
        m_logger->stopLogging();
        delete m_logger;
    }
    m_ipcServer->stop();
}

bool SessionProcess::initialize()
{
    QString serverName = QString("nterminal_%1").arg(m_sessionId);
    
    if (!m_ipcServer->start(serverName)) {
        qWarning() << "[SessionProcess] Failed to start IPC server:" << serverName;
        return false;
    }
    
    connect(m_ipcServer, &Ipc::IpcServer::clientConnected, this, &SessionProcess::onClientConnected);
    connect(m_ipcServer, &Ipc::IpcServer::clientDisconnected, this, &SessionProcess::onClientDisconnected);
    connect(m_ipcServer, &Ipc::IpcServer::messageReceived, this, &SessionProcess::onMessageReceived);
    
    m_logger = new SessionLogger(m_sessionId, this);
    
    m_initialized = true;
    qDebug() << "[SessionProcess] Initialized, waiting for client connection:" << serverName;
    return true;
}

void SessionProcess::run()
{
    m_running = true;
    qDebug() << "[SessionProcess] Running session:" << m_sessionId;
}

void SessionProcess::onClientConnected()
{
    qDebug() << "[SessionProcess] Client connected";
    sendConnectionStatus(Ipc::ConnectionStatus::Connected, "Client connected");
}

void SessionProcess::onClientDisconnected()
{
    qDebug() << "[SessionProcess] Client disconnected";
    m_running = false;
    QCoreApplication::quit();
}

void SessionProcess::onMessageReceived(Ipc::MessageType type, const QByteArray &payload)
{
    switch (type) {
        case Ipc::MessageType::SessionConfig:
            handleSessionConfig(payload);
            break;
        case Ipc::MessageType::DataToSend:
            handleDataToSend(payload);
            break;
        case Ipc::MessageType::TerminalSize:
            handleTerminalSize(payload);
            break;
        case Ipc::MessageType::Shutdown:
            handleShutdown();
            break;
        default:
            break;
    }
}

void SessionProcess::onConnectionDataReceived(const QByteArray &data)
{
    if (m_logger) {
        m_logger->logData(data, true);
    }
    
    sendDataReceived(data);
}

void SessionProcess::onConnectionConnected()
{
    qDebug() << "[SessionProcess] Connection established";
    sendConnectionStatus(Ipc::ConnectionStatus::Connected, "Connection established");
    
    if (m_logger) {
        m_logger->logConnectionEvent("Connection established");
    }
}

void SessionProcess::onConnectionDisconnected()
{
    qDebug() << "[SessionProcess] Connection closed";
    sendConnectionStatus(Ipc::ConnectionStatus::Disconnected, "Connection closed");
    
    if (m_logger) {
        m_logger->logConnectionEvent("Connection closed");
    }
}

void SessionProcess::onConnectionError(const QString &error)
{
    qWarning() << "[SessionProcess] Connection error:" << error;
    sendConnectionStatus(Ipc::ConnectionStatus::Error, error);
    
    if (m_logger) {
        m_logger->logConnectionEvent("Connection error", error);
    }
}

void SessionProcess::handleSessionConfig(const QByteArray &payload)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "[SessionProcess] Failed to parse session config:" << error.errorString();
        return;
    }
    
    m_settings = Common::ConnectionSettings::fromJson(doc.object());
    qDebug() << "[SessionProcess] Session config received:" << m_settings.sessionName;
    
    if (m_logger) {
        m_logger->startLogging(m_settings.sessionName);
    }
    
    m_connectionHandler = ConnectionHandler::create(m_settings, this);
    if (!m_connectionHandler) {
        sendConnectionStatus(Ipc::ConnectionStatus::Error, "Failed to create connection handler");
        return;
    }
    
    connect(m_connectionHandler, &ConnectionHandler::dataReceived, this, &SessionProcess::onConnectionDataReceived);
    connect(m_connectionHandler, &ConnectionHandler::connected, this, &SessionProcess::onConnectionConnected);
    connect(m_connectionHandler, &ConnectionHandler::disconnected, this, &SessionProcess::onConnectionDisconnected);
    connect(m_connectionHandler, &ConnectionHandler::errorOccurred, this, &SessionProcess::onConnectionError);
    
    sendConnectionStatus(Ipc::ConnectionStatus::Connecting, "Connecting...");
    
    if (!m_connectionHandler->open()) {
        sendConnectionStatus(Ipc::ConnectionStatus::Error, "Failed to open connection");
    }
}

void SessionProcess::handleDataToSend(const QByteArray &payload)
{
    qDebug() << "[SessionProcess] handleDataToSend: payload size=" << payload.size();
    qDebug() << "[SessionProcess] handleDataToSend: payload hex=" << payload.toHex(' ');
    qDebug() << "[SessionProcess] handleDataToSend: payload string=" << QString::fromUtf8(payload);

    if (m_connectionHandler) {
        qDebug() << "[SessionProcess] handleDataToSend: connectionHandler exists";
        qDebug() << "[SessionProcess] handleDataToSend: isConnected=" << m_connectionHandler->isConnected();

        if (m_connectionHandler->isConnected()) {
            qDebug() << "[SessionProcess] handleDataToSend: Sending data to connection handler";
            m_connectionHandler->sendData(payload);
            if (m_logger) {
                m_logger->logData(payload, false);
            }
        } else {
            qDebug() << "[SessionProcess] handleDataToSend: Connection handler not connected, data not sent";
        }
    } else {
        qDebug() << "[SessionProcess] handleDataToSend: No connection handler";
    }
}

void SessionProcess::handleTerminalSize(const QByteArray &payload)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError) {
        return;
    }
    
    QJsonObject obj = doc.object();
    m_columns = obj["columns"].toInt(200);
    m_rows = obj["rows"].toInt(24);
    
    qDebug() << "[SessionProcess] Terminal size changed to:" << m_columns << "x" << m_rows;
    
    if (m_connectionHandler) {
        m_connectionHandler->setTerminalSize(m_columns, m_rows);
    }
}

void SessionProcess::handleShutdown()
{
    qDebug() << "[SessionProcess] Shutdown requested";
    m_running = false;
    
    if (m_connectionHandler) {
        m_connectionHandler->close();
    }
    
    if (m_logger) {
        m_logger->stopLogging();
    }
    
    QCoreApplication::quit();
}

void SessionProcess::sendConnectionStatus(Ipc::ConnectionStatus status, const QString &message)
{
    QJsonObject obj;
    obj["status"] = static_cast<int>(status);
    obj["message"] = message;
    m_ipcServer->sendMessage(Ipc::MessageType::ConnectionStatus, obj);
}

void SessionProcess::sendDataReceived(const QByteArray &data)
{
    m_ipcServer->sendMessage(Ipc::MessageType::DataReceived, data);
}
