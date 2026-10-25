QT += core network testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_update

# 被测模块：src/update/updatemanager（纯函数：版本比较 / 清单解析 / URL 校验）
INCLUDEPATH += ../../src
SOURCES += \
    tst_updatemanager.cpp \
    ../../src/update/updatemanager.cpp
HEADERS += ../../src/update/updatemanager.h
