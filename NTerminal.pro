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

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    terminalwidget.cpp \
    connectiondialog.cpp \
    vt100parser.cpp \
    sessionmanager.cpp \
    logger.cpp \
    scriptrecorder.cpp \
    sshclient.cpp

HEADERS += \
    terminalwidget.h \
    mainwindow.h \
    connectiondialog.h \
    vt100parser.h \
    sessionmanager.h \
    logger.h \
    scriptrecorder.h \
    sshclient.h

TRANSLATIONS += \
    NTerminal_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
