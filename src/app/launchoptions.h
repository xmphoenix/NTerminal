#ifndef LAUNCHOPTIONS_H
#define LAUNCHOPTIONS_H

#include <QString>
#include <QStringList>

// ============================================================================
// 应用启动模式（app 模块）
//
// 同一可执行文件按命令行参数分流为三种进程：
//   -s/--session <id>  会话工作进程（QCoreApplication + SessionProcess）
//   -t/--test          终端兼容性测试窗口
//   （默认）           GUI 主程序
//
// 解析逻辑为纯函数，无 Qt GUI 依赖，可单元测试
// （见 tests/app/tst_launchoptions.cpp）。
// ============================================================================

namespace App {

enum class LaunchMode {
    Gui,      // 默认：GUI 主程序
    Session,  // -s/--session <id>：会话工作进程
    Test      // -t/--test：测试窗口
};

struct LaunchOptions {
    LaunchMode mode = LaunchMode::Gui;
    QString sessionId;   // 仅 Session 模式有效

    // 解析命令行参数（argv 内容，不含程序名亦可）
    static LaunchOptions parse(const QStringList &args);
};

} // namespace App

#endif // LAUNCHOPTIONS_H
