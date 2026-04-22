QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets serialport network

CONFIG += c++17

unix {
    LIBS += -lssh
}

win32 {
    LIBSSH_SRC = $$PWD/libssh
    LIBSSH_PREBUILT = $$PWD/libssh-prebuilt
    LIBSSH_SOURCE_DIR = $$PWD/libssh-src
    LIBSSH_BUILD_DIR = $$PWD/libssh-build

    exists($$LIBSSH_SRC/include/libssh/libssh.h) {
        message("Using compiled libssh from source")
        LIBSSH_ROOT = $$LIBSSH_SRC
    } else {
        message("Using prebuilt libssh")
        LIBSSH_ROOT = $$LIBSSH_PREBUILT
    }

    INCLUDEPATH += $$LIBSSH_ROOT/include
    LIBS += -L$$LIBSSH_ROOT/lib -lssh
    LIBS += -lws2_32

    DESTDIR = $$PWD/bin

    build_libssh.commands = $$quote(powershell -ExecutionPolicy Bypass -File \"$$shell_path($$PWD/build_libssh.ps1)\")
    build_libssh.target = $$LIBSSH_SRC/include/libssh/libssh.h
    build_libssh.depends = $$PWD/build_libssh.ps1

    QMAKE_EXTRA_TARGETS += build_libssh

    !exists($$LIBSSH_SRC/include/libssh/libssh.h) {
        PRE_TARGETDEPS += $$LIBSSH_SRC/include/libssh/libssh.h
    }

    # Copy DLLs using PowerShell for reliable Windows file operations
    copy_libssh_dlls.commands = powershell -ExecutionPolicy Bypass -Command \"Copy-Item -Path '$$shell_path($$LIBSSH_ROOT/bin/*.dll)' -Destination '$$shell_path($$DESTDIR)' -Force -ErrorAction SilentlyContinue\"
    copy_libssh_dlls.depends = build_libssh
    QMAKE_EXTRA_TARGETS += copy_libssh_dlls

    QMAKE_POST_LINK += powershell -ExecutionPolicy Bypass -Command \"Copy-Item -Path '$$shell_path($$LIBSSH_ROOT/bin/*.dll)' -Destination '$$shell_path($$DESTDIR)' -Force -ErrorAction SilentlyContinue\" $$escape_expand(\\n\\t)
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
    calculatordialog.cpp \
    asciitabledialog.cpp \
    portscannerdialog.cpp \
    pythonscriptengine.cpp \
    scriptdialog.cpp \
    scriptlogviewer.cpp \
    scriptcontrolpanel.cpp \
    scriptstatuswidget.cpp \
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
    calculatordialog.h \
    asciitabledialog.h \
    portscannerdialog.h \
    pythonscriptengine.h \
    scriptdialog.h \
    scriptlogviewer.h \
    scriptcontrolpanel.h \
    scriptstatuswidget.h \
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
    NTerminal_en_US.ts \
    NTerminal_zh_CN.ts
CONFIG += lrelease
CONFIG += embed_translations

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target