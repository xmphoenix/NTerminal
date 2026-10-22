QT       += core gui widgets serialport network testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = tst_terminalproxy

INCLUDEPATH += ../../src

# 终端屏幕模型测试：直接编译 terminal 核心及其编译依赖
# （ipcclient 为成员类型、pythonscriptengine 为成员类型，均不发起真实连接/进程）
SOURCES += \
    tst_terminalproxy.cpp \
    ../../src/terminal/terminalproxy.cpp \
    ../../src/terminal/terminalparser.cpp \
    ../../src/terminal/terminaltheme.cpp \
    ../../src/ipc/ipcclient.cpp \
    ../../src/ipc/ipcprotocol.cpp \
    ../../src/script/pythonscriptengine.cpp

HEADERS += \
    ../../src/terminal/terminalproxy.h \
    ../../src/terminal/terminalparser.h \
    ../../src/terminal/terminaltheme.h \
    ../../src/ipc/ipcclient.h \
    ../../src/ipc/ipcprotocol.h \
    ../../src/script/pythonscriptengine.h \
    ../../src/common/connectionsettings.h \
    ../../src/common/messagetypes.h
