#include "services/sessionmanager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <algorithm>

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
    // 设置配置文件路径：用户目录下的 .nterminal/sessions.json
    QString configDir = QDir::homePath() + "/.nterminal";
    QDir().mkpath(configDir);
    m_configFilePath = configDir + "/sessions.json";
}

SessionManager::~SessionManager()
{
    saveSessions();
}

bool SessionManager::loadSessions()
{
    QFile file(m_configFilePath);
    if (!file.exists()) {
        // 配置文件不存在，不是错误
        return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(QString("无法打开会话配置文件: %1").arg(file.errorString()));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("JSON解析错误: %1").arg(parseError.errorString()));
        return false;
    }

    if (!doc.isArray()) {
        emit errorOccurred("会话配置文件格式错误: 根元素不是数组");
        return false;
    }

    m_sessions.clear();
    QJsonArray sessionsArray = doc.array();

    for (const QJsonValue &sessionValue : sessionsArray) {
        if (!sessionValue.isObject()) {
            qWarning() << "跳过无效的会话项（不是对象）";
            continue;
        }

        QJsonObject sessionObj = sessionValue.toObject();

        if (!sessionObj.contains("name") || !sessionObj["name"].isString()) {
            qWarning() << "跳过无效的会话项（缺少或无效的名称）";
            continue;
        }

        QString name = sessionObj["name"].toString();

        if (!sessionObj.contains("settings") || !sessionObj["settings"].isObject()) {
            qWarning() << "跳过无效的会话项" << name << "（缺少或无效的设置）";
            continue;
        }

        QJsonObject settingsObj = sessionObj["settings"].toObject();

        // 序列化契约由 Common::ConnectionSettings 提供（含历史格式兼容）
        Common::ConnectionSettings settings = Common::ConnectionSettings::fromJson(settingsObj);

        // 兼容历史行为：settings 中无 sessionName 时回退为会话名
        if (!settingsObj.contains("sessionName") || !settingsObj["sessionName"].isString()) {
            settings.sessionName = name;
        }

        // 兼容历史行为：settings 中无 port 时按连接类型取默认端口
        if (!settingsObj.contains("port")) {
            settings.port = (settings.type == Common::ConnectionType::SSH) ? 22 : 23;
        }

        QDateTime lastUsed = QDateTime::currentDateTime();
        if (sessionObj.contains("lastUsed") && sessionObj["lastUsed"].isString()) {
            lastUsed = QDateTime::fromString(sessionObj["lastUsed"].toString(), Qt::ISODate);
        }

        Session session(name, settings);
        session.lastUsed = lastUsed;
        m_sessions.append(session);
    }

    // 按最后使用时间排序（最近使用的在前）
    std::sort(m_sessions.begin(), m_sessions.end(),
              [](const Session &a, const Session &b) {
                  return a.lastUsed > b.lastUsed;
              });

    emit sessionsLoaded();
    return true;
}

bool SessionManager::saveSessions()
{
    QJsonArray sessionsArray;

    for (const Session &session : m_sessions) {
        QJsonObject sessionObj;
        sessionObj["name"] = session.name;
        sessionObj["lastUsed"] = session.lastUsed.toString(Qt::ISODate);
        sessionObj["settings"] = session.settings.toJson();
        sessionsArray.append(sessionObj);
    }

    QJsonDocument doc(sessionsArray);
    QByteArray data = doc.toJson(QJsonDocument::Indented);

    QFile file(m_configFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit errorOccurred(QString("无法保存会话配置文件: %1").arg(file.errorString()));
        return false;
    }

    qint64 bytesWritten = file.write(data);
    file.close();

    if (bytesWritten != data.size()) {
        emit errorOccurred("写入会话配置文件时发生错误");
        return false;
    }

    return true;
}

QList<SessionManager::Session> SessionManager::getSessions() const
{
    return m_sessions;
}

Common::ConnectionSettings SessionManager::getSession(const QString &name) const
{
    for (const Session &session : m_sessions) {
        if (session.name == name) {
            return session.settings;
        }
    }

    // 返回默认设置
    Common::ConnectionSettings settings;
    settings.sessionName = name;
    return settings;
}

void SessionManager::addSession(const QString &name, const Common::ConnectionSettings &settings)
{
    // 检查是否已存在同名会话
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].name == name) {
            // 更新现有会话
            m_sessions[i].settings = settings;
            m_sessions[i].lastUsed = QDateTime::currentDateTime();
            emit sessionUpdated(name);
            return;
        }
    }

    // 添加新会话
    Session session(name, settings);
    m_sessions.append(session);

    // 排序
    std::sort(m_sessions.begin(), m_sessions.end(),
              [](const Session &a, const Session &b) {
                  return a.lastUsed > b.lastUsed;
              });

    emit sessionAdded(name);
}

void SessionManager::updateSession(const QString &name, const Common::ConnectionSettings &settings)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].name == name) {
            m_sessions[i].settings = settings;
            m_sessions[i].lastUsed = QDateTime::currentDateTime();

            // 排序
            std::sort(m_sessions.begin(), m_sessions.end(),
                      [](const Session &a, const Session &b) {
                          return a.lastUsed > b.lastUsed;
                      });

            emit sessionUpdated(name);
            return;
        }
    }

    // 如果不存在，则添加
    addSession(name, settings);
}

void SessionManager::removeSession(const QString &name)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].name == name) {
            m_sessions.removeAt(i);
            emit sessionRemoved(name);
            return;
        }
    }
}

QString SessionManager::getConfigFilePath() const
{
    return m_configFilePath;
}
