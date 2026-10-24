#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QLocalServer>
#include <QLockFile>
#include <QString>

#include <functional>

// GUI 主程序的单实例守卫。
//
// 同一用户只允许运行一个 NTerminal 主程序：
//  - 第一个实例持有临时目录中的 QLockFile，并监听一个本地命名管道；
//  - 后续实例连接管道请求激活已有窗口，提示用户不能重复启动，然后退出。
//
// 锁文件带 PID 校验：主实例崩溃后残留的锁会被自动接管，无需手动清理。
// --session / --mcp-stdio / --test 等辅助进程不经过本类。
class SingleInstance
{
public:
    explicit SingleInstance(const QString &applicationKey);
    ~SingleInstance();

    // 返回 true 表示当前进程是唯一主实例。
    bool tryAcquire();

    // 第二个实例：通知已运行的主实例显示窗口。
    void notifyPrimary() const;

    // 主实例收到激活请求时调用（在 GUI 线程）。
    void setActivationHandler(std::function<void()> handler);

private:
    QString m_lockPath;
    QString m_serverName;
    QLockFile m_lock;
    QLocalServer m_server;
    std::function<void()> m_activationHandler;
};

#endif // SINGLEINSTANCE_H
