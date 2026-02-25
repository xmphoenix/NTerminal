#include "sshclient.h"
#include <QDebug>
#include <QFile>

SshClient::SshClient(QObject *parent)
    : QObject(parent)
    , m_session(nullptr)
    , m_channel(nullptr)
    , m_state(Disconnected)
    , m_supportedAuthMethods(AuthNone)
    , m_socketNotifier(nullptr)
    , m_callbacks(nullptr)
    , m_ptyRequested(false)
{
    ssh_init();
}

SshClient::~SshClient()
{
    disconnect();
    ssh_finalize();
}

bool SshClient::connect(const SshSettings &settings)
{
    qDebug() << "[SshClient] connect - 开始连接" << settings.host << ":" << settings.port;
    
    m_settings = settings;
    m_lastError.clear();
    
    if (m_state != Disconnected) {
        qDebug() << "[SshClient] connect - 当前状态不是断开，先断开";
        disconnect();
    }
    
    setState(Connecting);
    
    if (!setupSession()) {
        setError("创建SSH会话失败");
        setState(Error);
        return false;
    }
    
    int rc = ssh_connect(m_session);
    if (rc != SSH_OK) {
        setError(QString("SSH连接失败: %1").arg(ssh_get_error(m_session)));
        setState(Error);
        return false;
    }
    
    qDebug() << "[SshClient] connect - SSH连接成功";
    
    if (!verifyHostKey()) {
        setState(Error);
        return false;
    }
    
    updateSupportedAuthMethods();
    setState(Connected);
    emit connected();
    
    return true;
}

void SshClient::disconnect()
{
    qDebug() << "[SshClient] disconnect - 断开连接";
    
    cleanupSocketNotifier();
    
    if (m_channel) {
        ssh_channel_close(m_channel);
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }
    
    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
    
    if (m_callbacks) {
        delete m_callbacks;
        m_callbacks = nullptr;
    }
    
    m_ptyRequested = false;
    setState(Disconnected);
    emit disconnected();
}

bool SshClient::setupSession()
{
    qDebug() << "[SshClient] setupSession - 创建SSH会话";
    
    m_session = ssh_new();
    if (!m_session) {
        setError("无法创建SSH会话对象");
        return false;
    }
    
    ssh_options_set(m_session, SSH_OPTIONS_HOST, m_settings.host.toUtf8().constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &m_settings.port);
    
    if (!m_settings.username.isEmpty()) {
        ssh_options_set(m_session, SSH_OPTIONS_USER, m_settings.username.toUtf8().constData());
    }
    
    int timeout = m_settings.timeout;
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    
    ssh_options_set(m_session, SSH_OPTIONS_STRICTHOSTKEYCHECK, 
                    m_settings.verifyHostKey ? "yes" : "no");
    
    m_callbacks = new ssh_callbacks_struct();
    memset(m_callbacks, 0, sizeof(ssh_callbacks_struct));
    m_callbacks->userdata = this;
    m_callbacks->log_function = sshLogCallback;
    ssh_set_callbacks(m_session, m_callbacks);
    
    return true;
}

bool SshClient::verifyHostKey()
{
    if (m_settings.verifyHostKey) {
        qDebug() << "[SshClient] verifyHostKey - 验证主机密钥";
        
        int state = ssh_is_server_known(m_session);
        switch (state) {
            case SSH_SERVER_KNOWN_OK:
                qDebug() << "[SshClient] verifyHostKey - 主机密钥已知且有效";
                break;
            case SSH_SERVER_KNOWN_CHANGED:
                setError("主机密钥已更改，可能存在中间人攻击");
                return false;
            case SSH_SERVER_FOUND_OTHER:
                setError("找到其他类型的主机密钥");
                return false;
            case SSH_SERVER_FILE_NOT_FOUND:
            case SSH_SERVER_NOT_KNOWN:
                qDebug() << "[SshClient] verifyHostKey - 主机密钥未知，自动接受";
                if (ssh_write_knownhost(m_session) != SSH_OK) {
                    qDebug() << "[SshClient] verifyHostKey - 无法保存主机密钥";
                }
                break;
            case SSH_SERVER_ERROR:
                setError(QString("验证主机密钥错误: %1").arg(ssh_get_error(m_session)));
                return false;
        }
    }
    
    return true;
}

void SshClient::updateSupportedAuthMethods()
{
    qDebug() << "[SshClient] updateSupportedAuthMethods - 查询支持的认证方法";
    
    m_supportedAuthMethods = AuthNone;
    
    int methods = ssh_userauth_list(m_session, nullptr);
    qDebug() << "[SshClient] updateSupportedAuthMethods - 返回的方法值:" << methods;
    
    if (methods < 0) {
        qDebug() << "[SshClient] updateSupportedAuthMethods - 获取认证方法失败:" << ssh_get_error(m_session);
        return;
    }
    
    if (methods & SSH_AUTH_METHOD_PASSWORD) {
        m_supportedAuthMethods = static_cast<AuthMethod>(m_supportedAuthMethods | AuthPassword);
        qDebug() << "[SshClient] updateSupportedAuthMethods - 支持密码认证";
    }
    if (methods & SSH_AUTH_METHOD_PUBLICKEY) {
        m_supportedAuthMethods = static_cast<AuthMethod>(m_supportedAuthMethods | AuthPublicKey);
        qDebug() << "[SshClient] updateSupportedAuthMethods - 支持公钥认证";
    }
    if (methods & SSH_AUTH_METHOD_INTERACTIVE) {
        m_supportedAuthMethods = static_cast<AuthMethod>(m_supportedAuthMethods | AuthKeyboardInteractive);
        qDebug() << "[SshClient] updateSupportedAuthMethods - 支持键盘交互认证";
    }
    
    if (m_supportedAuthMethods == AuthNone) {
        qDebug() << "[SshClient] updateSupportedAuthMethods - 未检测到支持的认证方法，将尝试默认认证";
    }
}

bool SshClient::authenticate()
{
    qDebug() << "[SshClient] authenticate - 开始认证，用户名:" << m_settings.username;
    qDebug() << "[SshClient] authenticate - 支持的认证方法:" << m_supportedAuthMethods;
    
    setState(Authenticating);
    
    if (m_settings.usePrivateKey && !m_settings.privateKeyPath.isEmpty()) {
        if (authenticateWithPublicKey()) {
            return true;
        }
    }
    
    if (m_settings.password.isEmpty()) {
        setError("未提供密码");
        emit authenticationFailed(m_lastError);
        setState(Error);
        return false;
    }
    
    if (m_supportedAuthMethods & AuthPassword) {
        if (authenticateWithPassword()) {
            return true;
        }
    } else if (m_supportedAuthMethods & AuthKeyboardInteractive) {
        if (authenticateWithKeyboardInteractive()) {
            return true;
        }
    } else {
        qDebug() << "[SshClient] authenticate - 未检测到支持的认证方法，尝试直接密码认证";
        if (authenticateWithPassword()) {
            return true;
        }
    }
    
    if (m_lastError.isEmpty()) {
        setError("认证失败: 不支持的认证方法");
    }
    emit authenticationFailed(m_lastError);
    setState(Error);
    return false;
}

bool SshClient::authenticateWithPassword()
{
    qDebug() << "[SshClient] authenticateWithPassword - 尝试密码认证";
    
    int rc = ssh_userauth_password(m_session, nullptr, 
                                   m_settings.password.toUtf8().constData());
    
    if (rc == SSH_AUTH_SUCCESS) {
        qDebug() << "[SshClient] authenticateWithPassword - 密码认证成功";
        setState(Authenticated);
        emit authenticated();
        return true;
    } else if (rc == SSH_AUTH_DENIED) {
        setError("密码认证被拒绝");
        return false;
    } else {
        setError(QString("密码认证失败: %1").arg(ssh_get_error(m_session)));
        return false;
    }
}

bool SshClient::authenticateWithPublicKey()
{
    qDebug() << "[SshClient] authenticateWithPublicKey - 尝试公钥认证，密钥:" << m_settings.privateKeyPath;
    
    ssh_key key = nullptr;
    int rc;
    
    if (m_settings.privateKeyPassphrase.isEmpty()) {
        rc = ssh_pki_import_privkey_file(m_settings.privateKeyPath.toUtf8().constData(), 
                                         nullptr, nullptr, nullptr, &key);
    } else {
        rc = ssh_pki_import_privkey_file(m_settings.privateKeyPath.toUtf8().constData(), 
                                         m_settings.privateKeyPassphrase.toUtf8().constData(), 
                                         nullptr, nullptr, &key);
    }
    
    if (rc != SSH_OK) {
        setError(QString("导入私钥失败: %1").arg(m_settings.privateKeyPath));
        return false;
    }
    
    rc = ssh_userauth_publickey(m_session, nullptr, key);
    ssh_key_free(key);
    
    if (rc == SSH_AUTH_SUCCESS) {
        qDebug() << "[SshClient] authenticateWithPublicKey - 公钥认证成功";
        setState(Authenticated);
        emit authenticated();
        return true;
    } else if (rc == SSH_AUTH_DENIED) {
        qDebug() << "[SshClient] authenticateWithPublicKey - 公钥认证被拒绝，尝试密码认证";
        return false;
    } else {
        setError(QString("公钥认证失败: %1").arg(ssh_get_error(m_session)));
        return false;
    }
}

bool SshClient::authenticateWithKeyboardInteractive()
{
    qDebug() << "[SshClient] authenticateWithKeyboardInteractive - 尝试键盘交互认证";
    
    int rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
    
    while (rc == SSH_AUTH_INFO) {
        int nprompts = ssh_userauth_kbdint_getnprompts(m_session);
        for (int i = 0; i < nprompts; i++) {
            char echo;
            const char *prompt = ssh_userauth_kbdint_getprompt(m_session, i, &echo);
            qDebug() << "[SshClient] authenticateWithKeyboardInteractive - 提示:" << prompt;
            
            rc = ssh_userauth_kbdint_setanswer(m_session, i, 
                                               m_settings.password.toUtf8().constData());
            if (rc < 0) {
                setError("设置键盘交互答案失败");
                return false;
            }
        }
        rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
    }
    
    if (rc == SSH_AUTH_SUCCESS) {
        qDebug() << "[SshClient] authenticateWithKeyboardInteractive - 认证成功";
        setState(Authenticated);
        emit authenticated();
        return true;
    } else {
        setError("键盘交互认证失败");
        return false;
    }
}

bool SshClient::openChannel()
{
    qDebug() << "[SshClient] openChannel - 打开SSH通道";
    
    if (m_state != Authenticated) {
        setError("未认证，无法打开通道");
        return false;
    }
    
    m_channel = ssh_channel_new(m_session);
    if (!m_channel) {
        setError(QString("创建SSH通道失败: %1").arg(ssh_get_error(m_session)));
        return false;
    }
    
    int rc = ssh_channel_open_session(m_channel);
    if (rc != SSH_OK) {
        setError(QString("打开SSH会话通道失败: %1").arg(ssh_get_error(m_session)));
        ssh_channel_free(m_channel);
        m_channel = nullptr;
        return false;
    }
    
    qDebug() << "[SshClient] openChannel - SSH通道已打开";
    return true;
}

bool SshClient::requestPty(const QString &termType, int cols, int rows)
{
    qDebug() << "[SshClient] requestPty - 请求PTY，类型:" << termType 
             << "尺寸:" << cols << "x" << rows;
    
    if (!m_channel) {
        setError("通道未打开");
        return false;
    }
    
    int rc = ssh_channel_request_pty_size(m_channel, 
                                           termType.toUtf8().constData(), 
                                           cols, rows);
    if (rc != SSH_OK) {
        setError(QString("请求PTY失败: %1").arg(ssh_get_error(m_session)));
        return false;
    }
    
    m_ptyRequested = true;
    qDebug() << "[SshClient] requestPty - PTY已分配";
    return true;
}

bool SshClient::requestShell()
{
    qDebug() << "[SshClient] requestShell - 请求Shell";
    
    if (!m_channel) {
        setError("通道未打开");
        return false;
    }
    
    int rc = ssh_channel_request_shell(m_channel);
    if (rc != SSH_OK) {
        setError(QString("请求Shell失败: %1").arg(ssh_get_error(m_session)));
        return false;
    }
    
    setState(Ready);
    emit ready();
    
    setupSocketNotifier();
    
    qDebug() << "[SshClient] requestShell - Shell已启动";
    return true;
}

qint64 SshClient::write(const QByteArray &data)
{
    if (!m_channel || ssh_channel_is_closed(m_channel)) {
        setError("通道未打开或已关闭");
        return -1;
    }
    
    int rc = ssh_channel_write(m_channel, data.constData(), data.size());
    if (rc == SSH_ERROR) {
        setError(QString("写入数据失败: %1").arg(ssh_get_error(m_session)));
        return -1;
    }
    
    return rc;
}

QByteArray SshClient::readAll()
{
    QByteArray result;
    
    if (!m_channel || ssh_channel_is_closed(m_channel)) {
        return result;
    }
    
    char buffer[8192];
    int nbytes;
    
    nbytes = ssh_channel_read_nonblocking(m_channel, buffer, sizeof(buffer), 0);
    while (nbytes > 0) {
        result.append(buffer, nbytes);
        nbytes = ssh_channel_read_nonblocking(m_channel, buffer, sizeof(buffer), 0);
    }
    
    if (ssh_channel_is_eof(m_channel)) {
        qDebug() << "[SshClient] readAll - 通道EOF";
        disconnect();
    }
    
    return result;
}

bool SshClient::hasDataAvailable() const
{
    if (!m_channel) {
        return false;
    }
    return ssh_channel_poll(m_channel, 0) > 0;
}

void SshClient::setTerminalSize(int cols, int rows)
{
    qDebug() << "[SshClient] setTerminalSize - 设置终端尺寸:" << cols << "x" << rows;
    
    if (m_channel && m_ptyRequested) {
        ssh_channel_change_pty_size(m_channel, cols, rows);
    }
}

void SshClient::setupSocketNotifier()
{
    if (m_socketNotifier) {
        cleanupSocketNotifier();
    }
    
    socket_t sock = ssh_get_fd(m_session);
    if (sock >= 0) {
        m_socketNotifier = new QSocketNotifier(sock, QSocketNotifier::Read, this);
        QObject::connect(m_socketNotifier, &QSocketNotifier::activated, 
                this, &SshClient::onSocketActivated);
        qDebug() << "[SshClient] setupSocketNotifier - Socket通知器已设置";
    }
}

void SshClient::cleanupSocketNotifier()
{
    if (m_socketNotifier) {
        delete m_socketNotifier;
        m_socketNotifier = nullptr;
    }
}

void SshClient::onSocketActivated(int socket)
{
    Q_UNUSED(socket);
    
    QByteArray data = readAll();
    if (!data.isEmpty()) {
        emit dataReceived(data);
    }
}

void SshClient::setError(const QString &error)
{
    m_lastError = error;
    qDebug() << "[SshClient] setError - 错误:" << error;
    emit errorOccurred(error);
}

void SshClient::setState(ConnectionState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

int SshClient::sshCallback(const char *message, ssh_session session, 
                           int priority, void *userdata)
{
    Q_UNUSED(session);
    Q_UNUSED(userdata);
    
    if (priority >= SSH_LOG_WARN) {
        qDebug() << "[libssh]" << message;
    }
    
    return 0;
}

void SshClient::sshLogCallback(ssh_session session, int priority, 
                               const char *message, void *userdata)
{
    Q_UNUSED(session);
    Q_UNUSED(userdata);
    
    if (priority >= SSH_LOG_WARN) {
        qDebug() << "[libssh log]" << message;
    }
}
