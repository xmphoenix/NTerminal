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
}

INCLUDEPATH += $$PWD/common $$PWD/ipc $$PWD/session

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    terminalproxy.cpp \
    connectiondialog.cpp \
    vt100parser.cpp \
    vt100tester.cpp \
    testwindow.cpp \
    sessionmanager.cpp \
    logger.cpp \
    scriptrecorder.cpp \
    sshclient.cpp \
    thememanager.cpp \
    themesettingsdialog.cpp \
    ipc/ipcprotocol.cpp \
    ipc/ipcclient.cpp \
    ipc/ipcserver.cpp \
    session/sessionprocess.cpp \
    session/sessionlogger.cpp \
    session/connectionhandler.cpp \
    session/serialhandler.cpp \
    session/telnethandler.cpp \
    session/ssshandler.cpp

HEADERS += \
    mainwindow.h \
    terminalproxy.h \
    connectiondialog.h \
    vt100parser.h \
    vt100tester.h \
    testwindow.h \
    sessionmanager.h \
    logger.h \
    scriptrecorder.h \
    sshclient.h \
    thememanager.h \
    themesettingsdialog.h \
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
    session/ssshandler.h

TRANSLATIONS += \
    NTerminal_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
