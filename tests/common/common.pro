QT += core testlib serialport
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_common

# 被测模块：src/common（header-only）
INCLUDEPATH += ../../src

SOURCES += tst_common.cpp
