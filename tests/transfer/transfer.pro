QT += core testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_transfer

# 被测模块：src/transfer 下的协议引擎（ASCII / XMODEM / YMODEM / ZMODEM / Kermit）
INCLUDEPATH += ../../src
SOURCES += \
    tst_transfer.cpp \
    ../../src/transfer/filetransfer.cpp \
    ../../src/transfer/asciitransfer.cpp \
    ../../src/transfer/xmodemtransfer.cpp \
    ../../src/transfer/ymodemtransfer.cpp \
    ../../src/transfer/zmodemtransfer.cpp \
    ../../src/transfer/kermittransfer.cpp
HEADERS += \
    ../../src/transfer/filetransfer.h \
    ../../src/transfer/asciitransfer.h \
    ../../src/transfer/xmodemtransfer.h \
    ../../src/transfer/ymodemtransfer.h \
    ../../src/transfer/zmodemtransfer.h \
    ../../src/transfer/kermittransfer.h \
    ../../src/transfer/xmodemcodec.h
