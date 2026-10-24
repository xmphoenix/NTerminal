#include "app/singleinstance.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>

namespace {

// 同一台机器可能同时有多个 Windows 用户运行 NTerminal：锁文件放在各自的
// 临时目录中，命名管道则用用户名哈希区分，避免互相抢占。
QString userSuffix()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USER");
    }
    if (user.isEmpty()) {
        user = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
}

} // namespace

SingleInstance::SingleInstance(const QString &applicationKey)
    : m_lockPath(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                     .filePath(applicationKey + QLatin1Char('-') + userSuffix()
                               + QStringLiteral(".lock")))
    , m_serverName(applicationKey + QStringLiteral("-single-instance-") + userSuffix())
    , m_lock(m_lockPath)
{
}

SingleInstance::~SingleInstance()
{
    if (m_server.isListening()) {
        m_server.close();
        QLocalServer::removeServer(m_serverName);
    }
    m_lock.unlock();
}

bool SingleInstance::tryAcquire()
{
    // 锁文件由 QLockFile 管理：持锁进程退出或崩溃后，残留的锁文件会被
    // 识别为过期并自动接管，不会留下“永远无法启动”的状态。
    if (!m_lock.tryLock(0)) {
        if (m_lock.error() == QLockFile::LockFailedError) {
            return false;   // 另一个实例持有锁
        }
        // 锁文件无法创建/读写（如临时目录权限异常）：放行启动，
        // 避免把环境问题误报成“程序已在运行”。
        qWarning() << "[SingleInstance] Cannot lock" << m_lockPath << ":"
                   << m_lock.error();
    }

    QObject::connect(&m_server, &QLocalServer::newConnection, &m_server, [this]() {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            socket->disconnectFromServer();
            socket->deleteLater();
        }
        if (m_activationHandler) {
            m_activationHandler();
        }
    });

    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_serverName)
            && m_server.serverError() == QAbstractSocket::AddressInUseError) {
        // 上次异常退出可能留下同名的本地套接字（Unix）；Windows 命名管道
        // 随进程关闭自动释放，removeServer 在此为空操作。
        QLocalServer::removeServer(m_serverName);
        m_server.listen(m_serverName);
    }

    if (!m_server.isListening()) {
        // 锁已持有，单实例仍然有效，只是无法激活已有窗口。
        qWarning() << "[SingleInstance] Cannot listen on" << m_serverName << ":"
                   << m_server.errorString();
    }

    return true;
}

void SingleInstance::notifyPrimary() const
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(500)) {
        return;
    }

    socket.write("activate");
    socket.flush();
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState) {
        socket.waitForDisconnected(500);
    }
}

void SingleInstance::setActivationHandler(std::function<void()> handler)
{
    m_activationHandler = std::move(handler);
}
