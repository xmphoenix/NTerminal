QT += core testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_sessionlogger

# 被测模块：src/session/sessionlogger（ANSI 过滤、行缓冲、日志文件生命周期）
INCLUDEPATH += ../../src
SOURCES += tst_sessionlogger.cpp ../../src/session/sessionlogger.cpp
HEADERS += ../../src/session/sessionlogger.h
