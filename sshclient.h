#ifndef SSHCLIENT_H
#define SSHCLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QSocketNotifier>
#include <libssh/libssh.h>
#include <libssh/callbacks.h>

class SshClient : public QObject
{
    Q_OBJECT

public:
    enum ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Authenticating,
        Authenticated,
        Ready,
        Error
    };

    enum AuthMethod {
        AuthNone = 0,
        AuthPassword = 1,
        AuthPublicKey = 2,
        AuthKeyboardInteractive = 4
    };

    struct SshSettings {
        QString host;
        int port = 22;
        QString username;
        QString password;
        QString privateKeyPath;
        QString privateKeyPassphrase;
        bool usePrivateKey = false;
        int timeout = 30;
        bool verifyHostKey = false;
    };

    explicit SshClient(QObject *parent = nullptr);
    ~SshClient();

    bool connect(const SshSettings &settings);
    void disconnect();
    bool authenticate();
    bool openChannel();
    bool requestPty(const QString &termType = "xterm", int cols = 80, int rows = 24);
    bool requestShell();
    
    qint64 write(const QByteArray &data);
    QByteArray readAll();
    bool hasDataAvailable() const;
    
    ConnectionState state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    AuthMethod supportedAuthMethods() const { return m_supportedAuthMethods; }
    
    void setTerminalSize(int cols, int rows);

signals:
    void connected();
    void disconnected();
    void authenticated();
    void authenticationFailed(const QString &error);
    void ready();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);
    void stateChanged(ConnectionState state);

private slots:
    void onSocketActivated(int socket);

private:
    bool setupSession();
    bool verifyHostKey();
    bool authenticateWithPassword();
    bool authenticateWithPublicKey();
    bool authenticateWithKeyboardInteractive();
    void updateSupportedAuthMethods();
    void setError(const QString &error);
    void setState(ConnectionState state);
    void setupSocketNotifier();
    void cleanupSocketNotifier();
    
    static int sshCallback(const char *message, ssh_session session, 
                          int priority, void *userdata);
    static void sshLogCallback(ssh_session session, int priority, 
                               const char *message, void *userdata);

    ssh_session m_session;
    ssh_channel m_channel;
    ConnectionState m_state;
    SshSettings m_settings;
    QString m_lastError;
    AuthMethod m_supportedAuthMethods;
    QSocketNotifier *m_socketNotifier;
    ssh_callbacks m_callbacks;
    bool m_ptyRequested;
};

#endif // SSHCLIENT_H
