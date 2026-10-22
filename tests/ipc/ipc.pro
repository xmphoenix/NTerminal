QT       += core network testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = tst_ipcprotocol

INCLUDEPATH += ../../src

SOURCES += \
    tst_ipcprotocol.cpp \
    ../../src/ipc/ipcprotocol.cpp

HEADERS += \
    ../../src/ipc/ipcprotocol.h \
    ../../src/common/messagetypes.h
