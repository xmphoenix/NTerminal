#include "services/serverservice.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSettings>

ServerService::ServerService(QObject *parent)
    : QObject(parent)
{
}

ServerService::~ServerService()
{
    // 这里不能调用纯虚 stopServer()（析构阶段虚表已回到基类）；
    // 服务的套接字都是本对象的子对象，销毁时由 Qt 自动关闭。
    // 正常退出顺序由 ServerManager::~ServerManager 先 stop() 保证。
}

int ServerService::port() const
{
    return configInt(QStringLiteral("port"));
}

QString ServerService::configString(const QString &key) const
{
    return m_config.value(key).toString();
}

int ServerService::configInt(const QString &key) const
{
    return m_config.value(key).toInt();
}

bool ServerService::configBool(const QString &key) const
{
    return m_config.value(key).toBool();
}

void ServerService::loadConfig()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("NetworkServers"));
    const QString json = settings.value(id()).toString();
    settings.endGroup();

    m_config = defaultConfig();
    if (json.isEmpty()) {
        return;
    }
    const QJsonObject saved = QJsonDocument::fromJson(json.toUtf8()).object();
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        m_config.insert(it.key(), it.value());
    }
}

void ServerService::saveConfig()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("NetworkServers"));
    settings.setValue(id(),
                      QString::fromUtf8(QJsonDocument(m_config).toJson(QJsonDocument::Compact)));
    settings.endGroup();
}

bool ServerService::start(const QJsonObject &config)
{
    if (m_running) {
        return true;
    }

    // 界面只提交自己认识的字段：其余保持默认/上次保存的值
    m_config = defaultConfig();
    for (auto it = config.begin(); it != config.end(); ++it) {
        m_config.insert(it.key(), it.value());
    }
    m_error.clear();

    if (!startServer()) {
        if (m_error.isEmpty()) {
            m_error = tr("服务启动失败");
        }
        log(tr("启动失败：%1").arg(m_error));
        emit errorOccurred(m_error);
        return false;
    }

    setRunning(true);
    saveConfig();
    return true;
}

void ServerService::stop()
{
    if (!m_running) {
        return;
    }
    stopServer();
    setRunning(false);
    log(tr("服务已停止"));
}

void ServerService::failAndStop(const QString &message)
{
    m_error = message;
    log(tr("运行异常：%1").arg(message));
    emit errorOccurred(message);
    if (m_running) {
        stopServer();
        setRunning(false);
    }
}

void ServerService::setRunning(bool running)
{
    if (m_running == running) {
        return;
    }
    m_running = running;
    emit runningChanged(running);
}

void ServerService::log(const QString &message)
{
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                  message);
    m_recentLog.append(line);
    while (m_recentLog.size() > 200) {
        m_recentLog.removeFirst();
    }
    emit logMessage(line);
}

void ServerService::setError(const QString &message)
{
    m_error = message;
}
