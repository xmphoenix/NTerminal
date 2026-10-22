#ifndef CONNECTIONHANDLERFACTORY_H
#define CONNECTIONHANDLERFACTORY_H

#include <QObject>
#include "common/connectionsettings.h"

class ConnectionHandler;

// ============================================================================
// 连接协议处理器工厂（session 模块）
//
// 按 Common::ConnectionSettings::type 创建对应的 ConnectionHandler 实现。
// 工厂与全部具体协议实现（含 libssh 依赖）隔离在独立编译单元，
// 使 ConnectionHandler 接口可以脱离协议实现单独编译与测试。
// ============================================================================

class ConnectionHandlerFactory
{
public:
    // 返回按 settings.type 新建的处理器；未知类型返回 nullptr。
    // 所有权归调用方（parent 参数参与 QObject 父子回收）。
    static ConnectionHandler *create(const Common::ConnectionSettings &settings,
                                     QObject *parent = nullptr);
};

#endif // CONNECTIONHANDLERFACTORY_H
