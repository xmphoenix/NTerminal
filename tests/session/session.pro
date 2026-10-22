QT += core testlib serialport
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_connectionhandler

# 被测模块：src/session/connectionhandler（抽象接口，不含具体协议实现；
# 工厂分发依赖 libssh 与全部 handler，由集成构建覆盖，不做单元链接）
# serialport：connectionsettings.h 使用 QSerialPort 枚举
INCLUDEPATH += ../../src

SOURCES += tst_connectionhandler.cpp ../../src/session/connectionhandler.cpp

HEADERS += ../../src/session/connectionhandler.h
