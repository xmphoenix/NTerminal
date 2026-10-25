#include "common/sshruntime.h"

#include <libssh/libssh.h>

namespace SshRuntime {

bool ensureInitialized()
{
    // 函数内静态变量的初始化由 C++11 保证线程安全，且只执行一次。
    static const bool initialized = ssh_init() == SSH_OK;
    return initialized;
}

} // namespace SshRuntime
