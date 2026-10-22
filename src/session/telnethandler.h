#ifndef TELNETHANDLER_H
#define TELNETHANDLER_H

#include "connectionhandler.h"
#include <QTcpSocket>

class TelnetHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit TelnetHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~TelnetHandler();
    
    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    void setTerminalSize(int columns, int rows) override;
    
private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    
private:
    QByteArray processTelnetProtocol(const QByteArray &data);
    void handleSubnegotiation(const QByteArray &data);
    void sendTelnetCommand(quint8 command, quint8 option);
    void sendNaws();
    
    QTcpSocket *m_socket;
    Common::ConnectionSettings m_settings;
    bool m_negotiating;
    
    enum TelnetLoginState {
        LoginNone,
        LoginWaitPrompt,
        LoginSentUser,
        LoginSentPassword,
        LoginDone
    };
    TelnetLoginState m_loginState;
    QString m_receiveBuffer;
    
    int m_columns;
    int m_rows;
};

#endif // TELNETHANDLER_H
