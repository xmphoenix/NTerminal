#ifndef SERVERSERVICE_H
#define SERVERSERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

// ============================================================================
// 本机网络服务基类（网络服务对话框）
//
// 每个服务在进程内只有一个实例，由 ServerManager 持有：对话框可以反复
// 打开/关闭、反复启动停止，服务对象和配置始终是同一份。
//
// 子类实现 defaultConfig()/startServer()/stopServer()：
//   - start() 先把界面传入的配置合并进 m_config 再启动；
//   - 启动成功后配置写入 QSettings，下次打开对话框/重启程序仍是同一份；
//   - 运行中再次 start() 是空操作（避免重复实例化/重复绑定端口）。
// ============================================================================
class ServerService : public QObject
{
    Q_OBJECT

public:
    explicit ServerService(QObject *parent = nullptr);
    ~ServerService() override;

    // 服务标识（同时用作 QSettings 分组名），如 "tftp"
    virtual QString id() const = 0;
    // 界面显示名，如 "TFTP 服务器"
    virtual QString displayName() const = 0;
    // 一句话说明（显示在设置页标题下）
    virtual QString description() const = 0;
    // 默认监听端口
    virtual int defaultPort() const = 0;

    bool isRunning() const { return m_running; }
    QString errorString() const { return m_error; }
    QJsonObject config() const { return m_config; }
    int port() const;
    // 最近 200 条日志：对话框重新打开时回显（服务实例是长期存活的）
    QStringList recentLog() const { return m_recentLog; }

    // 从 QSettings 读取上次保存的配置（由 ServerManager 在实例化后调用，
    // 不能放在构造函数里：defaultConfig() 是虚函数）
    void loadConfig();

    // 用给定配置启动；服务已在运行时为空操作
    bool start(const QJsonObject &config);
    void stop();

signals:
    // 运行状态变化（启动成功/停止/异常退出）
    void runningChanged(bool running);
    // 带时间戳的运行日志
    void logMessage(const QString &line);
    // 启动失败/运行中异常
    void errorOccurred(const QString &message);

protected:
    virtual QJsonObject defaultConfig() const = 0;
    // 返回 false 时需设置 errorString（m_error）
    virtual bool startServer() = 0;
    virtual void stopServer() = 0;

    // 服务内部异常：记录并停止
    void failAndStop(const QString &message);

    void log(const QString &message);
    void setError(const QString &message);

    // 配置读取辅助（缺省值在 defaultConfig 里给出）
    QString configString(const QString &key) const;
    int configInt(const QString &key) const;
    bool configBool(const QString &key) const;

private:
    void setRunning(bool running);
    void saveConfig();

    QJsonObject m_config;
    QString m_error;
    QStringList m_recentLog;
    bool m_running = false;
};

#endif // SERVERSERVICE_H
