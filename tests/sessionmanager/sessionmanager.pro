QT += core testlib serialport
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_sessionmanager

# 被测模块：src/services/sessionmanager（sessions.json 持久化）
INCLUDEPATH += ../../src
SOURCES += tst_sessionmanager.cpp ../../src/services/sessionmanager.cpp
HEADERS += ../../src/services/sessionmanager.h
