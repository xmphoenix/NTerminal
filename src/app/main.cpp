#include "ui/mainwindow.h"
#include "session/sessionprocess.h"
#include "theme/thememanager.h"
#include "theme/fontmanager.h"
#include "ui/testwindow.h"
#include "app/launchoptions.h"
#include "common/appinfo.h"
#include "common/languagemanager.h"
#include "mcp/mcpbridge.h"
#include "update/updatemanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QDebug>
#include <QFile>
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

    // 上次下载的更新包：启动更新脚本后立即退出，由脚本等待本进程结束、
    // 替换程序文件并重新启动（不经过安装程序）。
    if (UpdateManager::applyPendingUpdate()) {
        return 0;
    }

    ThemeManager::instance()->initialize();
    FontManager::instance()->initialize();

    // 界面语言：源码字符串为中文（缺省），用户可在"设置 → 语言"切换到英文，
    // 选择结果写入配置，下次启动沿用。
    LanguageManager::instance()->initialize();

    MainWindow window;
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
