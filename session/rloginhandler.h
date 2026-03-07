#ifndef RLOGINHANDLER_H
#define RLOGINHANDLER_H

#include "connectionhandler.h"
#include <QTcpSocket>

class RloginHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit RloginHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~RloginHandler();

    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    void setTerminalSize(int columns, int rows) override;

private slots:
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);

private:
    void sendRloginHandshake();
    void sendWindowSize();
    void processDataWithEscapeSequences();

    QTcpSocket *m_socket;
    Common::ConnectionSettings m_settings;
    bool m_handshakeSent;
    bool m_handshakeComplete;
    int m_columns;
    int m_rows;
    QByteArray m_pendingData;  // Buffer for incomplete escape sequences
};

#endif // RLOGINHANDLER_H