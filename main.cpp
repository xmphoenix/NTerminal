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

        qDebug() << "[SessionProcess] Starting session:" << sessionId;

        SessionProcess session(sessionId);
        if (!session.initialize()) {
            qCritical() << "[SessionProcess] Failed to initialize session process";
            return 1;
        }

        session.run();

        return app.exec();
    } else if (isTestMode) {
        // VT100 测试模式
        QApplication app(argc, argv);
        QCoreApplication::setApplicationName("NTerminal-Tester");
        QCoreApplication::setApplicationVersion("1.0.0");

        qDebug() << "[Tester] Starting VT100 terminal emulation tester";

        TestWindow testWindow;
        testWindow.show();

        return app.exec();
    } else {
        // 启用高 DPI 支持，确保多显示器颜色一致
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
        
        QApplication app(argc, argv);
        QCoreApplication::setApplicationName("NTerminal");
        QCoreApplication::setApplicationVersion("2.0.0");

        qDebug() << "[GUI] Starting GUI application";

        ThemeManager::instance()->initialize();
        FontManager::instance()->initialize();

        QTranslator translator;
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString &locale : uiLanguages) {
            const QString baseName = "NTerminal_" + QLocale(locale).name();
            if (translator.load(":/i18n/" + baseName)) {
                app.installTranslator(&translator);
                break;
            }
        }

        MainWindow window;
        window.show();
        return app.exec();
    }
}
