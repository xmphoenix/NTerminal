QT       += core testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = tst_terminalparser

INCLUDEPATH += ../../src

SOURCES += \
    tst_terminalparser.cpp \
    ../../src/terminal/terminalparser.cpp

HEADERS += \
    ../../src/terminal/terminalparser.h
