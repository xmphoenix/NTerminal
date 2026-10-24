#ifndef SERVERMANAGER_H
#define SERVERMANAGER_H

#include <QList>
#include <QObject>

class ServerService;

// ============================================================================
// 本机网络服务注册表（进程级单例）
//
// "每种服务只实例化一个"由本类保证：实例在第一次访问时创建，之后无论
// 网络服务对话框打开/关闭多少次，拿到的都是同一个对象（运行状态与配置
// 也随之保留）。实例以本对象为父对象，程序退出时统一停止服务。
// ============================================================================
class ServerManager : public QObject
{
    Q_OBJECT

public:
    static ServerManager *instance();

    // 固定顺序：TFTP / FTP / HTTP / Telnet / NFS
    QList<ServerService *> services();
    ServerService *service(const QString &id);

    void stopAll();

private:
    explicit ServerManager(QObject *parent = nullptr);
    ~ServerManager() override;

    void ensureCreated();

    QList<ServerService *> m_services;
};

#endif // SERVERMANAGER_H
