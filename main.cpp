#include "mainwindow.h"
#include "session/sessionprocess.h"
#include "thememanager.h"
#include "fontmanager.h"
#include "testwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QLocale>
#include <QTranslator>
#include <QDebug>
#include <QFile>
#include <QTextStream>

// Custom log file handler
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

int main(int argc, char *argv[])
{
    QStringList args;
    for (int i = 0; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }

    bool isSessionMode = false;
    QString sessionId;
    bool isTestMode = false;

    for (int i = 1; i < args.size(); ++i) {
        if ((args[i] == "-s" || args[i] == "--session") && i + 1 < args.size()) {
            isSessionMode = true;
            sessionId = args[i + 1];
            break;
        } else if (args[i] == "-t" || args[i] == "--test") {
            isTestMode = true;
        }
    }

    if (isSessionMode) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName("NTerminal-Session");
        QCoreApplication::setApplicationVersion("2.0.0");

        // Initialize log file
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
    } else if (isTestMode) {
        // Terminal tester mode
        QApplication app(argc, argv);
        QCoreApplication::setApplicationName("NTerminal-Tester");
        QCoreApplication::setApplicationVersion("1.0.0");

        TestWindow testWindow;
        testWindow.show();

        return app.exec();
    } else {
        // Enable high DPI support
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);

        QApplication app(argc, argv);
        QCoreApplication::setApplicationName("NTerminal");
        QCoreApplication::setApplicationVersion("2.0.0");

        ThemeManager::instance()->initialize();
        FontManager::instance()->initialize();

        // Detect system language: if Chinese, load Chinese translation; otherwise use English source
        QTranslator translator;
        QLocale systemLocale = QLocale::system();
        QString languageName = systemLocale.name(); // e.g. "zh_CN", "en_US", "zh_TW"

        if (languageName.startsWith("zh")) {
            // Try to load Chinese translation
            if (translator.load(":/i18n/NTerminal_zh_CN")) {
                app.installTranslator(&translator);
            }
            // If zh_CN.qm not found, falls through to source strings
        }
        // Non-Chinese locales: no translator installed, English source strings used as-is

        MainWindow window;
        window.show();
        return app.exec();
    }
}