#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QDateTime>
#include "common/connectionsettings.h"

// ============================================================================
// 会话持久化服务（services 层）
//
// 将用户保存的连接配置（Common::ConnectionSettings）持久化到
// ~/.nterminal/sessions.json，供主窗口"Load Session"菜单使用。
//
// 模块边界：
//  - 依赖方向 services -> common，不依赖任何 UI 头文件；
//  - settings 的 JSON 序列化复用 Common::ConnectionSettings 的稳定契约，
//    读取时兼容历史格式（详见 common/connectionsettings.h）。
// ============================================================================

class SessionManager : public QObject
{
    Q_OBJECT

public:
    struct Session {
        QString name;
        Common::ConnectionSettings settings;
        QDateTime lastUsed;

        Session() {}
        Session(const QString &name, const Common::ConnectionSettings &settings)
            : name(name), settings(settings), lastUsed(QDateTime::currentDateTime()) {}
    };

    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager();

    bool loadSessions();
    bool saveSessions();

    QList<Session> getSessions() const;
    Common::ConnectionSettings getSession(const QString &name) const;

    void addSession(const QString &name, const Common::ConnectionSettings &settings);
    void updateSession(const QString &name, const Common::ConnectionSettings &settings);
    void removeSession(const QString &name);

    QString getConfigFilePath() const;

signals:
    void sessionsLoaded();
    void sessionAdded(const QString &name);
    void sessionUpdated(const QString &name);
    void sessionRemoved(const QString &name);
    void errorOccurred(const QString &error);

private:
    QList<Session> m_sessions;
    QString m_configFilePath;
};

#endif // SESSIONMANAGER_H
