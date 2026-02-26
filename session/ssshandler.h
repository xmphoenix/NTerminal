#ifndef SSHHANDLER_H
#define SSHHANDLER_H

#include "connectionhandler.h"
#include <libssh/libssh.h>
#include <QSocketNotifier>

class SshHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit SshHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~SshHandler();
    
    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    
private slots:
    void onSocketActivated(int socket);
    
private:
    bool authenticate();
    bool setupChannel();
    void checkData();
    void cleanup();
    
    ssh_session m_session;
    ssh_channel m_channel;
    Common::ConnectionSettings m_settings;
    QSocketNotifier *m_notifier;
    bool m_connected;
    int m_columns;
    int m_rows;
};

#endif // SSHHANDLER_H
