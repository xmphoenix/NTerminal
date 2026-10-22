#include "ssshandler.h"
#include <QDebug>
#include <QTimer>

SshHandler::SshHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_session(nullptr)
    , m_channel(nullptr)
    , m_settings(settings)
    , m_notifier(nullptr)
    , m_connected(false)
    , m_columns(200)
    , m_rows(24)
{
}

SshHandler::~SshHandler()
{
    close();
}

bool SshHandler::open()
{
    m_session = ssh_new();
    if (!m_session) {
        emit errorOccurred("Failed to create SSH session");
        return false;
    }
    
    ssh_options_set(m_session, SSH_OPTIONS_HOST, m_settings.host.toUtf8().constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &m_settings.port);
    ssh_options_set(m_session, SSH_OPTIONS_USER, m_settings.username.toUtf8().constData());
    
    int timeout = 30;
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    
    qDebug() << "[SshHandler] Connecting to" << m_settings.host << ":" << m_settings.port;
    
    int rc = ssh_connect(m_session);
    if (rc != SSH_OK) {
        QString error = QString("SSH connection failed: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        cleanup();
        return false;
    }
    
    if (!authenticate()) {
        cleanup();
        return false;
    }
    
    if (!setupChannel()) {
        cleanup();
        return false;
    }
    
    int socketFd = ssh_get_fd(m_session);
    if (socketFd >= 0) {
        m_notifier = new QSocketNotifier(socketFd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &SshHandler::onSocketActivated);
    }
    
    m_connected = true;
    qDebug() << "[SshHandler] Connected";
    emit connected();
    return true;
}

void SshHandler::close()
{
    m_connected = false;
    
    if (m_notifier) {
        delete m_notifier;
        m_notifier = nullptr;
    }
    
    cleanup();
    emit disconnected();
}

bool SshHandler::isOpen() const
{
    return m_session != nullptr && m_channel != nullptr;
}

bool SshHandler::isConnected() const
{
    return m_connected && m_channel != nullptr;
}

void SshHandler::sendData(const QByteArray &data)
{
    if (!m_channel || !m_connected) {
        return;
    }
    
    ssh_channel_write(m_channel, data.constData(), data.size());
}

QString SshHandler::connectionInfo() const
{
    return QString("SSH: %1@%2:%3").arg(m_settings.username, m_settings.host).arg(m_settings.port);
}

void SshHandler::onSocketActivated(int socket)
{
    Q_UNUSED(socket);
    checkData();
}

bool SshHandler::authenticate()
{
    int rc = ssh_userauth_password(m_session, nullptr, m_settings.password.toUtf8().constData());
    
    if (rc == SSH_AUTH_SUCCESS) {
        qDebug() << "[SshHandler] Password authentication successful";
        return true;
    }
    
    rc = ssh_userauth_publickey_auto(m_session, nullptr, nullptr);
    if (rc == SSH_AUTH_SUCCESS) {
        qDebug() << "[SshHandler] Public key authentication successful";
        return true;
    }
    
    QString error = QString("Authentication failed: %1").arg(ssh_get_error(m_session));
    emit errorOccurred(error);
    return false;
}

bool SshHandler::setupChannel()
{
    m_channel = ssh_channel_new(m_session);
    if (!m_channel) {
        emit errorOccurred("Failed to create SSH channel");
        return false;
    }
    
    int rc = ssh_channel_open_session(m_channel);
    if (rc != SSH_OK) {
        QString error = QString("Failed to open SSH channel: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        return false;
    }
    
    rc = ssh_channel_request_pty_size(m_channel, "xterm-256color", m_columns, m_rows);
    if (rc != SSH_OK) {
        QString error = QString("Failed to request PTY: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        return false;
    }
    
    rc = ssh_channel_request_shell(m_channel);
    if (rc != SSH_OK) {
        QString error = QString("Failed to request shell: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        return false;
    }
    
    qDebug() << "[SshHandler] Channel setup complete";
    return true;
}

void SshHandler::checkData()
{
    if (!m_channel || !m_connected) {
        return;
    }
    
    char buffer[4096];
    int nbytes = ssh_channel_read_nonblocking(m_channel, buffer, sizeof(buffer), 0);
    
    if (nbytes > 0) {
        emit dataReceived(QByteArray(buffer, nbytes));
    }
    
    if (ssh_channel_is_eof(m_channel)) {
        qDebug() << "[SshHandler] Channel EOF";
        close();
    }
}

void SshHandler::cleanup()
{
    if (m_channel) {
        if (ssh_channel_is_open(m_channel)) {
            ssh_channel_close(m_channel);
        }
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }
    
    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
}

void SshHandler::setTerminalSize(int columns, int rows)
{
    m_columns = columns;
    m_rows = rows;
    
    if (m_channel && ssh_channel_is_open(m_channel)) {
        ssh_channel_change_pty_size(m_channel, columns, rows);
        qDebug() << "[SshHandler] Terminal size changed to:" << columns << "x" << rows;
    }
}
