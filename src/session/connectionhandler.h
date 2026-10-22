#ifndef CONNECTIONHANDLER_H
#define CONNECTIONHANDLER_H

#include <QObject>
#include <QByteArray>
#include "common/connectionsettings.h"

// ============================================================================
// 连接协议处理器抽象接口（session 模块）
//
// 每种协议（Serial/Telnet/SSH/Rlogin/SFTP/FTP/TFTP）提供一个实现：
//   open()/close()            -- 连接生命周期
//   sendData()                -- 终端输入 -> 协议通道
//   dataReceived 信号         -- 协议通道 -> 终端显示
//   setTerminalSize()         -- 窗口大小同步（支持 NAWS 的协议）
//
// 实例由 ConnectionHandlerFactory 按设置中的连接类型创建
// （见 connectionhandlerfactory.h）；本头文件不依赖任何具体协议实现，
// 因此可单独编译/测试（见 tests/session/tst_connectionhandler.cpp）。
// ============================================================================

class ConnectionHandler : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionHandler(QObject *parent = nullptr);
    virtual ~ConnectionHandler();

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool isConnected() const = 0;
    virtual void sendData(const QByteArray &data) = 0;
    virtual QString connectionInfo() const = 0;
    virtual void setTerminalSize(int columns, int rows) {
        Q_UNUSED(columns);
        Q_UNUSED(rows);
    }

signals:
    void dataReceived(const QByteArray &data);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
};

#endif // CONNECTIONHANDLER_H
