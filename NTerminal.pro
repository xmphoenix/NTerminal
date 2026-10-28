# ============================================================================
# NTerminal - 多协议终端工具（qmake 工程）
#
# 模块结构（src/）：
#   app/       启动入口与命令行解析（GUI / 会话进程 / 测试窗口三模式）
#   common/    跨模块共享数据结构（连接设置、IPC 消息类型）
#   ipc/       主进程与会话进程的本地管道通信
#   terminal/  终端核心（VT100/ANSI 解析器、终端代理、主题数据）
#   session/   会话进程侧的协议连接处理器（工厂 + 7 种协议）
#   mcp/       MCP 服务（工具/资源/权限/审计）与 stdio 桥接器
#   files/     图形化文件管理器的 SFTP/SCP/FTP 客户端与工作线程
#   update/    自动更新（清单/下载/校验/独立更新脚本）
#   services/  GUI 侧会话管理服务
#   theme/     主题/字体管理（单例，生产 TerminalTheme）
#   ui/        对话框与主窗口
#
# 单元测试：tests/tests.pro（QtTest，覆盖 app/parser/proxy/ipc 四个模块）
# ============================================================================

QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets serialport network svg
win32: QT += axcontainer

CONFIG   += c++17

TEMPLATE  = app
TARGET    = NTerminal

# ---- 版本信息（单一来源） ----
# VERSION 同时用于 Windows 可执行文件的 VERSIONINFO 属性；
# APP_VERSION 注入 C++，供窗口标题与"关于"对话框读取。
VERSION   = 2.0.1
DEFINES  += APP_VERSION=\\\"$$VERSION\\\"
# 显示在 Windows 文件属性“详细信息”中的说明（保持 ASCII，避免资源脚本编码问题）
QMAKE_TARGET_DESCRIPTION = NTerminal - multi-protocol terminal tool

# 可执行文件图标（资源管理器/任务栏/快捷方式）；运行时窗口图标见 main.cpp
win32: RC_ICONS = resources/icons/app.ico

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
    # 注意：这里直接使用 LIBSSH_ROOT（D:/... 形式）。在 MSYS/Git Bash 下 qmake 的
    # shell_path() 会生成 /D/... 的 POSIX 路径，PowerShell 无法识别，导致后置命令失败。
    QMAKE_POST_LINK += powershell -ExecutionPolicy Bypass -Command \"Copy-Item -Path '$$LIBSSH_ROOT/bin/*.dll' -Destination '$$DESTDIR' -Force -ErrorAction SilentlyContinue\" $$escape_expand(\\n\\t)
}

# ---- 头文件搜索路径（源码内 include 形如 "ui/xxx.h"、"ipc/xxx.h"） ----
INCLUDEPATH += $$PWD/src

# ---- 源文件（按模块分组） ----
SOURCES += \
    src/app/main.cpp \
    src/app/launchoptions.cpp \
    src/app/singleinstance.cpp \
    src/common/languagemanager.cpp \
    src/common/sshruntime.cpp \
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
    src/services/sessionmanager.cpp \
    src/transfer/filetransfer.cpp \
    src/transfer/xmodemtransfer.cpp \
    src/transfer/ymodemtransfer.cpp \
    src/transfer/kermittransfer.cpp \
    src/transfer/zmodemtransfer.cpp \
    src/transfer/asciitransfer.cpp \
    src/services/serverservice.cpp \
    src/services/servermanager.cpp \
    src/services/tftpserver.cpp \
    src/services/ftpserver.cpp \
    src/services/httpserver.cpp \
    src/services/telnetserver.cpp \
    src/services/nfsserver.cpp \
    src/mcp/mcpserver.cpp \
    src/mcp/mcpbridge.cpp \
    src/files/sshfileclient.cpp \
    src/files/ftpfileclient.cpp \
    src/files/remotefilesession.cpp \
    src/update/updatemanager.cpp \
    src/theme/thememanager.cpp \
    src/theme/fontmanager.cpp \
    src/ui/mainwindow.cpp \
    src/ui/aboutdialog.cpp \
    src/ui/connectiondialog.cpp \
    src/ui/iconprovider.cpp \
    src/ui/sessionitemdelegate.cpp \
    src/ui/terminaltabbar.cpp \
    src/ui/terminalsplitpage.cpp \
    src/ui/rdpwidget.cpp \
    src/ui/terminaltester.cpp \
    src/ui/testwindow.cpp \
    src/ui/logconfigdialog.cpp \
    src/ui/themesettingsdialog.cpp \
    src/ui/calculatordialog.cpp \
    src/ui/asciitabledialog.cpp \
    src/ui/portscannerdialog.cpp \
    src/ui/lanscandialog.cpp \
    src/ui/networkservicesdialog.cpp \
    src/ui/transferdialog.cpp \
    src/ui/mcpsettingsdialog.cpp \
    src/ui/mcplogviewer.cpp \
    src/ui/updatedialog.cpp \
    src/ui/updatesettingsdialog.cpp \
    src/ui/filemanagerdialog.cpp \
    src/ui/systemstatswidget.cpp

# ---- 头文件（Q_OBJECT 类必须列出以生成 moc） ----
HEADERS += \
    src/app/launchoptions.h \
    src/app/singleinstance.h \
    src/common/appinfo.h \
    src/common/connectionsettings.h \
    src/common/messagetypes.h \
    src/common/languagemanager.h \
    src/common/sshruntime.h \
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
    src/services/sessionmanager.h \
    src/transfer/filetransfer.h \
    src/transfer/xmodemcodec.h \
    src/transfer/xmodemtransfer.h \
    src/transfer/ymodemtransfer.h \
    src/transfer/kermittransfer.h \
    src/transfer/zmodemtransfer.h \
    src/transfer/asciitransfer.h \
    src/services/serverservice.h \
    src/services/servermanager.h \
    src/services/tftpserver.h \
    src/services/ftpserver.h \
    src/services/httpserver.h \
    src/services/telnetserver.h \
    src/services/nfsserver.h \
    src/mcp/mcpserver.h \
    src/mcp/mcpbridge.h \
    src/files/remotefileentry.h \
    src/files/remotefileclient.h \
    src/files/sshfileclient.h \
    src/files/ftpfileclient.h \
    src/files/remotefilesession.h \
    src/update/updatemanager.h \
    src/theme/thememanager.h \
    src/theme/fontmanager.h \
    src/ui/mainwindow.h \
    src/ui/aboutdialog.h \
    src/ui/connectiondialog.h \
    src/ui/iconprovider.h \
    src/ui/protocolstyle.h \
    src/ui/sessionitemdelegate.h \
    src/ui/terminaltabbar.h \
    src/ui/terminalsplitpage.h \
    src/ui/rdpwidget.h \
    src/ui/terminaltester.h \
    src/ui/testwindow.h \
    src/ui/logconfigdialog.h \
    src/ui/themesettingsdialog.h \
    src/ui/calculatordialog.h \
    src/ui/asciitabledialog.h \
    src/ui/portscannerdialog.h \
    src/ui/lanscandialog.h \
    src/ui/networkservicesdialog.h \
    src/ui/transferdialog.h \
    src/ui/mcpsettingsdialog.h \
    src/ui/mcplogviewer.h \
    src/ui/updatedialog.h \
    src/ui/updatesettingsdialog.h \
    src/ui/filemanagerdialog.h \
    src/ui/systemstatswidget.h

RESOURCES += \
    resources/resources.qrc

# ---- 国际化 ----
# 源字符串为中文（缺省界面语言），英文翻译由 translations/NTerminal_en_US.ts 提供，
# 构建时 lrelease 生成 .qm 并内嵌到 :/i18n/ 资源，运行时按配置加载。
TRANSLATIONS += \
    translations/NTerminal_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
