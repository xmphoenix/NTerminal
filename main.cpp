#include "mainwindow.h"
#include "session/sessionprocess.h"
#include "thememanager.h"

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
    
    for (int i = 1; i < args.size(); ++i) {
        if ((args[i] == "-s" || args[i] == "--session") && i + 1 < args.size()) {
            isSessionMode = true;
            sessionId = args[i + 1];
            break;
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
    } else {
        QApplication app(argc, argv);
        QCoreApplication::setApplicationName("NTerminal");
        QCoreApplication::setApplicationVersion("2.0.0");
        
        qDebug() << "[GUI] Starting GUI application";
        
        ThemeManager::instance()->initialize();
        
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
