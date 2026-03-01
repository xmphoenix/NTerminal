#ifndef TFTPHANDLER_H
#define TFTPHANDLER_H

#include "connectionhandler.h"
#include <QUdpSocket>

class TftpHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit TftpHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~TftpHandler();

    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    void setTerminalSize(int columns, int rows) override;

private slots:
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);

private:
    QUdpSocket *m_socket;
    Common::ConnectionSettings m_settings;
    bool m_connected;
    QString m_commandBuffer;  // Buffer for user input from terminal
};

#endif // TFTPHANDLER_H
