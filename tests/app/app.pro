QT       += core testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = tst_launchoptions

INCLUDEPATH += ../../src

SOURCES += \
    tst_launchoptions.cpp \
    ../../src/app/launchoptions.cpp

HEADERS += \
    ../../src/app/launchoptions.h
