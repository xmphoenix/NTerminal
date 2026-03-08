QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets serialport network

CONFIG += c++17

unix {
    LIBS += -lssh
}

win32 {
    LIBSSH_ROOT = D:/Ai/NTerminal/libssh-prebuilt
    INCLUDEPATH += $$LIBSSH_ROOT/include
    LIBS += -L$$LIBSSH_ROOT/lib -lssh
    LIBS += -lws2_32  # For Windows socket API
}

INCLUDEPATH += $$PWD/common $$PWD/ipc $$PWD/session

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    terminalproxy.cpp \
    connectiondialog.cpp \
    terminalparser.cpp \
    terminaltester.cpp \
    testwindow.cpp \
    sessionmanager.cpp \
    logger.cpp \
    logconfigdialog.cpp \
    scriptrecorder.cpp \
    sshclient.cpp \
    thememanager.cpp \
    themesettingsdialog.cpp \
    fontmanager.cpp \
    ipc/ipcprotocol.cpp \
    ipc/ipcclient.cpp \
    ipc/ipcserver.cpp \
    session/sessionprocess.cpp \
    session/sessionlogger.cpp \
    session/connectionhandler.cpp \
    session/serialhandler.cpp \
    session/telnethandler.cpp \
    session/ssshandler.cpp \
    session/rloginhandler.cpp \
    session/sftphandler.cpp \
    session/ftphandler.cpp \
    session/tftphandler.cpp

HEADERS += \
    mainwindow.h \
    terminalproxy.h \
    connectiondialog.h \
    terminalparser.h \
    terminaltester.h \
    testwindow.h \
    sessionmanager.h \
    logger.h \
    logconfigdialog.h \
    scriptrecorder.h \
    sshclient.h \
    thememanager.h \
    themesettingsdialog.h \
    fontmanager.h \
    common/messagetypes.h \
    common/connectionsettings.h \
    ipc/ipcprotocol.h \
    ipc/ipcclient.h \
    ipc/ipcserver.h \
    session/sessionprocess.h \
    session/sessionlogger.h \
    session/connectionhandler.h \
    session/serialhandler.h \
    session/telnethandler.h \
    session/ssshandler.h \
    session/rloginhandler.h \
    session/sftphandler.h \
    session/ftphandler.h \
    session/tftphandler.h

RESOURCES += \
    resources/resources.qrc

TRANSLATIONS += \
    NTerminal_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
