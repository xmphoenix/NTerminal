# ============================================================================
# NTerminal - 多协议终端工具（qmake 工程）
#
# 模块结构（src/）：
#   app/       启动入口与命令行解析（GUI / 会话进程 / 测试窗口三模式）
#   common/    跨模块共享数据结构（连接设置、IPC 消息类型）
#   ipc/       主进程与会话进程的本地管道通信
#   terminal/  终端核心（VT100/ANSI 解析器、终端代理、主题数据）
#   session/   会话进程侧的协议连接处理器（工厂 + 7 种协议）
#   script/    Python 脚本引擎（QProcess 驱动外部解释器）
#   services/  GUI 侧会话管理服务
#   theme/     主题/字体管理（单例，生产 TerminalTheme）
#   ui/        对话框与主窗口
#
# 单元测试：tests/tests.pro（QtTest，覆盖 app/parser/proxy/ipc 四个模块）
# ============================================================================

QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets serialport network

CONFIG   += c++17

TEMPLATE  = app
TARGET    = NTerminal

# ---- libssh（仅 SSH/SFTP 处理器依赖） ----
unix {
    LIBS += -lssh
}

win32 {
    # libssh 布局约定（由 build_libssh.ps1 管理）：
    #   libssh-src/     源码（tarball 解压）
    #   libssh-build/   CMake 构建目录（临时）
    #   openssl-deps/   OpenSSL/zlib 运行时（libssh 的编译依赖，非产品链接依赖）
    #   libssh/         libssh 安装产物（include/lib/bin），产品只链接这里的 -lssh
    LIBSSH_ROOT = $$clean_path($$PWD/libssh)

    !exists($$LIBSSH_ROOT/include/libssh/libssh.h) {
        error("libssh not found at $$LIBSSH_ROOT - run first: powershell -ExecutionPolicy Bypass -File build_libssh.ps1")
    }
    message("Using libssh at $$LIBSSH_ROOT")

    INCLUDEPATH += $$LIBSSH_ROOT/include
    LIBS += -L$$LIBSSH_ROOT/lib -lssh
    LIBS += -lws2_32

    DESTDIR = $$PWD/bin

    # 部署 libssh 及其运行时依赖（OpenSSL/pthread/libgcc DLL 已并入 libssh/bin）
    QMAKE_POST_LINK += powershell -ExecutionPolicy Bypass -Command \"Copy-Item -Path '$$shell_path($$LIBSSH_ROOT/bin/*.dll)' -Destination '$$shell_path($$DESTDIR)' -Force -ErrorAction SilentlyContinue\" $$escape_expand(\\n\\t)
}

# ---- 头文件搜索路径（源码内 include 形如 "ui/xxx.h"、"ipc/xxx.h"） ----
INCLUDEPATH += $$PWD/src

# ---- 源文件（按模块分组） ----
SOURCES += \
    src/app/main.cpp \
    src/app/launchoptions.cpp \
    src/terminal/terminalproxy.cpp \
    src/terminal/terminalparser.cpp \
    src/terminal/terminaltheme.cpp \
    src/ipc/ipcprotocol.cpp \
    src/ipc/ipcclient.cpp \
    src/ipc/ipcserver.cpp \
    src/session/sessionprocess.cpp \
    src/session/sessionlogger.cpp \
    src/session/connectionhandler.cpp \
    src/session/connectionhandlerfactory.cpp \
    src/session/serialhandler.cpp \
    src/session/telnethandler.cpp \
    src/session/ssshandler.cpp \
    src/session/rloginhandler.cpp \
    src/session/sftphandler.cpp \
    src/session/ftphandler.cpp \
    src/session/tftphandler.cpp \
    src/script/pythonscriptengine.cpp \
    src/services/sessionmanager.cpp \
    src/theme/thememanager.cpp \
    src/theme/fontmanager.cpp \
    src/ui/mainwindow.cpp \
    src/ui/connectiondialog.cpp \
    src/ui/terminaltester.cpp \
    src/ui/testwindow.cpp \
    src/ui/logconfigdialog.cpp \
    src/ui/themesettingsdialog.cpp \
    src/ui/calculatordialog.cpp \
    src/ui/asciitabledialog.cpp \
    src/ui/portscannerdialog.cpp \
    src/ui/scriptdialog.cpp \
    src/ui/scriptlogviewer.cpp \
    src/ui/scriptcontrolpanel.cpp

# ---- 头文件（Q_OBJECT 类必须列出以生成 moc） ----
HEADERS += \
    src/app/launchoptions.h \
    src/common/connectionsettings.h \
    src/common/messagetypes.h \
    src/terminal/terminalproxy.h \
    src/terminal/terminalparser.h \
    src/terminal/terminaltheme.h \
    src/ipc/ipcprotocol.h \
    src/ipc/ipcclient.h \
    src/ipc/ipcserver.h \
    src/session/sessionprocess.h \
    src/session/sessionlogger.h \
    src/session/connectionhandler.h \
    src/session/connectionhandlerfactory.h \
    src/session/serialhandler.h \
    src/session/telnethandler.h \
    src/session/ssshandler.h \
    src/session/rloginhandler.h \
    src/session/sftphandler.h \
    src/session/ftphandler.h \
    src/session/tftphandler.h \
    src/script/pythonscriptengine.h \
    src/services/sessionmanager.h \
    src/theme/thememanager.h \
    src/theme/fontmanager.h \
    src/ui/mainwindow.h \
    src/ui/connectiondialog.h \
    src/ui/terminaltester.h \
    src/ui/testwindow.h \
    src/ui/logconfigdialog.h \
    src/ui/themesettingsdialog.h \
    src/ui/calculatordialog.h \
    src/ui/asciitabledialog.h \
    src/ui/portscannerdialog.h \
    src/ui/scriptdialog.h \
    src/ui/scriptlogviewer.h \
    src/ui/scriptcontrolpanel.h

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
