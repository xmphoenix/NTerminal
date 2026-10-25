#ifndef SSHRUNTIME_H
#define SSHRUNTIME_H

// ============================================================================
// libssh 全局初始化
//
// libssh 的全局状态（Windows 上是 Winsock，另有加密算法表等）必须在第一次
// 调用任何 libssh API 之前初始化。libssh 在支持 constructor 属性的平台会
// 自动初始化，但 MinGW 构建的 DLL 不会执行该构造函数，因此 SSH/SFTP 客户端
// 需要在建立会话之前显式调用这里。
//
// 进程退出时由操作系统回收全局资源，这里不调用 ssh_finalize()：会话对象与
// 静态对象的析构顺序无法保证，提前 finalize 反而会让仍在析构的会话访问
// 已释放的全局状态。
// ============================================================================

namespace SshRuntime {

// 线程安全且幂等；返回 false 表示 libssh 初始化失败。
bool ensureInitialized();

} // namespace SshRuntime

#endif // SSHRUNTIME_H
