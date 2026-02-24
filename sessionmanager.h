#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QDateTime>
#include "connectiondialog.h"

class SessionManager : public QObject
{
    Q_OBJECT

public:
    struct Session {
        QString name;
        ConnectionDialog::ConnectionSettings settings;
        QDateTime lastUsed;
        
        Session() {}
        Session(const QString &name, const ConnectionDialog::ConnectionSettings &settings)
            : name(name), settings(settings), lastUsed(QDateTime::currentDateTime()) {}
    };

    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager();

    bool loadSessions();
    bool saveSessions();
    
    QList<Session> getSessions() const;
    ConnectionDialog::ConnectionSettings getSession(const QString &name) const;
    
    void addSession(const QString &name, const ConnectionDialog::ConnectionSettings &settings);
    void updateSession(const QString &name, const ConnectionDialog::ConnectionSettings &settings);
    void removeSession(const QString &name);
    
    QString getConfigFilePath() const;
    
signals:
    void sessionsLoaded();
    void sessionAdded(const QString &name);
    void sessionUpdated(const QString &name);
    void sessionRemoved(const QString &name);
    void errorOccurred(const QString &error);

private:
    QList<Session> m_sessions;
    QString m_configFilePath;
    
    ConnectionDialog::ConnectionType stringToConnectionType(const QString &str) const;
    QString connectionTypeToString(ConnectionDialog::ConnectionType type) const;
    
    QSerialPort::DataBits stringToDataBits(const QString &str) const;
    QString dataBitsToString(QSerialPort::DataBits dataBits) const;
    
    QSerialPort::Parity stringToParity(const QString &str) const;
    QString parityToString(QSerialPort::Parity parity) const;
    
    QSerialPort::StopBits stringToStopBits(const QString &str) const;
    QString stopBitsToString(QSerialPort::StopBits stopBits) const;
    
    QSerialPort::FlowControl stringToFlowControl(const QString &str) const;
    QString flowControlToString(QSerialPort::FlowControl flowControl) const;
};

#endif // SESSIONMANAGER_H