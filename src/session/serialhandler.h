#ifndef SERIALHANDLER_H
#define SERIALHANDLER_H

#include "connectionhandler.h"
#include <QSerialPort>

class SerialHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit SerialHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~SerialHandler();
    
    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    
private slots:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError error);
    
private:
    QSerialPort *m_serialPort;
    Common::ConnectionSettings m_settings;
};

#endif // SERIALHANDLER_H
