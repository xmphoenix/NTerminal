#ifndef SESSIONPROCESS_H
#define SESSIONPROCESS_H

#include <QObject>
#include <QJsonObject>
#include "common/connectionsettings.h"
#include "common/messagetypes.h"
#include "ipc/ipcserver.h"

class ConnectionHandler;
class SessionLogger;

class SessionProcess : public QObject
{
    Q_OBJECT

public:
    explicit SessionProcess(const QString &sessionId, QObject *parent = nullptr);
    ~SessionProcess();
    
    bool initialize();
    void run();
    
    QString sessionId() const { return m_sessionId; }

private slots:
    void onClientConnected();
    void onClientDisconnected();
    void onMessageReceived(Ipc::MessageType type, const QByteArray &payload);
    
    void onConnectionDataReceived(const QByteArray &data);
    void onConnectionConnected();
    void onConnectionDisconnected();
    void onConnectionError(const QString &error);

private:
    void handleSessionConfig(const QByteArray &payload);
    void handleDataToSend(const QByteArray &payload);
    void handleTerminalSize(const QByteArray &payload);
    void handleShutdown();
    
    void sendConnectionStatus(Ipc::ConnectionStatus status, const QString &message = QString());
    void sendDataReceived(const QByteArray &data);
    
    QString m_sessionId;
    Ipc::IpcServer *m_ipcServer;
    ConnectionHandler *m_connectionHandler;
    SessionLogger *m_logger;
    
    Common::ConnectionSettings m_settings;
    bool m_initialized;
    bool m_running;
    
    int m_columns;
    int m_rows;
};

#endif // SESSIONPROCESS_H
