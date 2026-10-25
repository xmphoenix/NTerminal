#include "ui/mainwindow.h"
#include "session/sessionprocess.h"
#include "theme/thememanager.h"
#include "theme/fontmanager.h"
#include "ui/testwindow.h"
#include "app/launchoptions.h"
#include "app/singleinstance.h"
#include "common/appinfo.h"
#include "common/languagemanager.h"
#include "mcp/mcpbridge.h"
#include "update/updatemanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QStringList>
#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <QTextStream>

// 会话进程调试日志（写入 rlogin_debug_<sessionId>.log）
class LogFile {
public:
    static void init(const QString &sessionId) {
        QString logPath = QCoreApplication::applicationDirPath() + "/rlogin_debug_" + sessionId + ".log";
        logFile.setFileName(logPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            ts.setDevice(&logFile);
            ts << "========== Session Started ==========\n";
            ts.flush();
        }
    }

    static void log(const QString &msg) {
        if (ts.device()) {
            ts << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
            ts.flush();
        }
    }

    static void close() {
        if (logFile.isOpen()) {
            ts << "========== Session Ended ==========\n";
            ts.flush();
            logFile.close();
        }
    }

private:
    static QFile logFile;
    static QTextStream ts;
};

QFile LogFile::logFile;
QTextStream LogFile::ts;

static int runSessionProcess(const QString &sessionId, int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("NTerminal-Session");
    QCoreApplication::setApplicationVersion(AppInfo::version());

    LogFile::init(sessionId);
    LogFile::log("Session process starting, ID: " + sessionId);

    SessionProcess session(sessionId);
    if (!session.initialize()) {
        LogFile::log("ERROR: Failed to initialize session process");
        LogFile::close();
        return 1;
    }

    session.run();

    int result = app.exec();

    LogFile::log("Session process exiting with code: " + QString::number(result));
    LogFile::close();

    return result;
}

static int runMcpStdio(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("NTerminal-MCP");
    QCoreApplication::setApplicationVersion(AppInfo::version());

    McpBridge bridge;
    return bridge.run();
}

static int runTestWindow(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("NTerminal-Tester");
    QCoreApplication::setApplicationVersion(AppInfo::version());

    TestWindow testWindow;
    testWindow.show();

    return app.exec();
}

static int runGui(int argc, char *argv[])
{
    // Qt 6 使用 RHI 自动选择渲染后端，不再需要 AA_UseOpenGLES（该属性已废弃）
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("NTerminal");
    // QSettings 的缺省构造只取 organizationName()；为空时 Windows 后端不会创建
    // 注册表路径，主题/字体的保存会静默失效。统一到与其它模块相同的组织名。
    QCoreApplication::setOrganizationName("NTerminal");
    QCoreApplication::setApplicationVersion(AppInfo::version());

    // 窗口/任务栏图标（可执行文件图标由 RC_ICONS 嵌入，这里保证运行时
    // 标题栏与任务栏也使用同一枚图标，而不是 Qt 的默认占位图）
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    ThemeManager::instance()->initialize();
    FontManager::instance()->initialize();

    // 界面语言：源码字符串为中文（缺省），用户可在"设置 → 语言"切换到英文，
    // 选择结果写入配置，下次启动沿用。
    LanguageManager::instance()->initialize();

    // 单实例：第二个主程序只负责唤起已运行的窗口并提示错误，然后退出。
    // 放在读取待安装更新之前，避免重复启动的进程误触发更新脚本。
    SingleInstance singleInstance(QStringLiteral("NTerminal"));
    if (!singleInstance.tryAcquire()) {
        singleInstance.notifyPrimary();
        QMessageBox::warning(nullptr,
            QCoreApplication::translate("SingleInstance", "NTerminal"),
            QCoreApplication::translate("SingleInstance",
                                        "NTerminal 已经在运行，只能启动一个实例。"));
        return 0;
    }

    // 上次下载的更新包：启动更新脚本后立即退出，由脚本等待本进程结束、
    // 替换程序文件并重新启动（不经过安装程序）。
    if (UpdateManager::applyPendingUpdate()) {
        return 0;
    }

    MainWindow window;
    // 另一个实例启动时，把已有窗口从最小化恢复并带到前台。
    singleInstance.setActivationHandler([&window]() {
        window.setWindowState((window.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        window.show();
        window.raise();
        window.activateWindow();
    });
    window.show();
    return app.exec();
}

int main(int argc, char *argv[])
{
    QStringList args;
    for (int i = 0; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }

    const App::LaunchOptions options = App::LaunchOptions::parse(args.mid(1));

    switch (options.mode) {
        case App::LaunchMode::Session:
            return runSessionProcess(options.sessionId, argc, argv);
        case App::LaunchMode::Test:
            return runTestWindow(argc, argv);
        case App::LaunchMode::McpStdio:
            return runMcpStdio(argc, argv);
        case App::LaunchMode::Gui:
        default:
            return runGui(argc, argv);
    }
}
