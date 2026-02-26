#ifndef CONNECTIONHANDLER_H
#define CONNECTIONHANDLER_H

#include <QObject>
#include <QByteArray>
#include "common/connectionsettings.h"

class ConnectionHandler : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionHandler(QObject *parent = nullptr);
    virtual ~ConnectionHandler();
    
    static ConnectionHandler* create(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool isConnected() const = 0;
    virtual void sendData(const QByteArray &data) = 0;
    virtual QString connectionInfo() const = 0;
    
signals:
    void dataReceived(const QByteArray &data);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
};

#endif // CONNECTIONHANDLER_H
