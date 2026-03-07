#include "sessionprocess.h"
#include "connectionhandler.h"
#include "sessionlogger.h"
#include <QDebug>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>

// Debug logging function
static void debugLog(const QString &message) {
    static QFile logFile;
    static bool initialized = false;

    if (!initialized) {
        QString logDir = QCoreApplication::applicationDirPath() + "/debug";
        QDir().mkpath(logDir);
        logFile.setFileName(logDir + "/sessionprocess_debug.log");
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        initialized = true;
    }

    if (logFile.isOpen()) {
        QTextStream stream(&logFile);
        stream << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
               << " [SessionProcess] " << message << "\n";
        stream.flush();
    }
}

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
    debugLog("========== SessionProcess Constructor ==========");
    debugLog(QString("Session ID: %1").arg(sessionId));
    debugLog("================================================");
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
    debugLog("========== onConnectionDataReceived CALLED ==========");
    debugLog(QString("Data size: %1 bytes").arg(data.size()));
    debugLog(QString("Data hex: %1").arg(QString(data.toHex(' '))));

    qDebug() << "========== SessionProcess Data Received ==========";
    qDebug() << "[SessionProcess] Data size:" << data.size() << "bytes";
    qDebug() << "[SessionProcess] Data hex:" << data.toHex(' ');
    qDebug() << "[SessionProcess] Data text:" << QString::fromUtf8(data);
    qDebug() << "==================================================";

    if (m_logger) {
        m_logger->logData(data, true);
    }

    sendDataReceived(data);
    debugLog("========== onConnectionDataReceived END ==========");
}

void SessionProcess::onConnectionConnected()
{
    debugLog("========== onConnectionConnected CALLED ==========");

    qDebug() << "========== SessionProcess Connected ==========";
    qDebug() << "[SessionProcess] Connection established";
    qDebug() << "==============================================";
    sendConnectionStatus(Ipc::ConnectionStatus::Connected, "Connection established");

    if (m_logger) {
        m_logger->logConnectionEvent("Connection established");
    }

    debugLog("========== onConnectionConnected END ==========");
}

void SessionProcess::onConnectionDisconnected()
{
    qDebug() << "========== SessionProcess Disconnected ==========";
    qDebug() << "[SessionProcess] Connection closed";
    qDebug() << "=================================================";
    sendConnectionStatus(Ipc::ConnectionStatus::Disconnected, "Connection closed");

    if (m_logger) {
        m_logger->logConnectionEvent("Connection closed");
    }
}

void SessionProcess::onConnectionError(const QString &error)
{
    qDebug() << "========== SessionProcess Error ==========";
    qWarning() << "[SessionProcess] Connection error:" << error;
    qDebug() << "==========================================";
    sendConnectionStatus(Ipc::ConnectionStatus::Error, error);

    if (m_logger) {
        m_logger->logConnectionEvent("Connection error", error);
    }
}

void SessionProcess::handleSessionConfig(const QByteArray &payload)
{
    debugLog("========== handleSessionConfig START ==========");
    qDebug() << "========== SessionProcess Config Received ==========";

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError) {
        debugLog(QString("ERROR: Failed to parse session config: %1").arg(error.errorString()));
        qWarning() << "[SessionProcess] Failed to parse session config:" << error.errorString();
        return;
    }

    m_settings = Common::ConnectionSettings::fromJson(doc.object());
    debugLog(QString("Session name: %1").arg(m_settings.sessionName));
    debugLog(QString("Connection type: %1").arg(static_cast<int>(m_settings.type)));
    debugLog(QString("Host: %1").arg(m_settings.host));
    debugLog(QString("Port: %1").arg(m_settings.port));
    debugLog(QString("Username: %1").arg(m_settings.username));

    qDebug() << "[SessionProcess] Session name:" << m_settings.sessionName;
    qDebug() << "[SessionProcess] Connection type:" << static_cast<int>(m_settings.type);
    qDebug() << "[SessionProcess] Host:" << m_settings.host;
    qDebug() << "[SessionProcess] Port:" << m_settings.port;
    qDebug() << "[SessionProcess] Username:" << m_settings.username;
    qDebug() << "====================================================";

    if (m_logger) {
        m_logger->startLogging(m_settings.sessionName);
    }

    debugLog("Creating connection handler...");
    m_connectionHandler = ConnectionHandler::create(m_settings, this);
    if (!m_connectionHandler) {
        debugLog("ERROR: Failed to create connection handler!");
        qWarning() << "[SessionProcess] Failed to create connection handler!";
        sendConnectionStatus(Ipc::ConnectionStatus::Error, "Failed to create connection handler");
        return;
    }

    debugLog(QString("Connection handler created: %1").arg(m_connectionHandler->metaObject()->className()));
    debugLog(QString("Connection handler pointer: %1").arg(reinterpret_cast<quintptr>(m_connectionHandler), 0, 16));
    qDebug() << "[SessionProcess] Connection handler created:" << m_connectionHandler->metaObject()->className();

    debugLog("Connecting signals...");
    connect(m_connectionHandler, &ConnectionHandler::dataReceived, this, &SessionProcess::onConnectionDataReceived);
    connect(m_connectionHandler, &ConnectionHandler::connected, this, &SessionProcess::onConnectionConnected);
    connect(m_connectionHandler, &ConnectionHandler::disconnected, this, &SessionProcess::onConnectionDisconnected);
    connect(m_connectionHandler, &ConnectionHandler::errorOccurred, this, &SessionProcess::onConnectionError);
    debugLog("Signals connected");

    sendConnectionStatus(Ipc::ConnectionStatus::Connecting, "Connecting...");

    debugLog("Calling open() on connection handler...");
    qDebug() << "[SessionProcess] Calling open() on connection handler...";
    bool openResult = m_connectionHandler->open();
    debugLog(QString("open() returned: %1").arg(openResult));
    qDebug() << "[SessionProcess] open() returned:" << openResult;

    if (!openResult) {
        debugLog("ERROR: Failed to open connection");
        qWarning() << "[SessionProcess] Failed to open connection";
        sendConnectionStatus(Ipc::ConnectionStatus::Error, "Failed to open connection");
    }

    debugLog("========== handleSessionConfig END ==========");
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
    debugLog(QString("========== sendDataReceived: Sending %1 bytes via IPC ==========").arg(data.size()));
    m_ipcServer->sendMessage(Ipc::MessageType::DataReceived, data);
    debugLog("========== sendDataReceived: IPC message sent ==========");
}
