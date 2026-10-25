QT += core network testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_nfs

# 被测模块：NFS v3 只读服务（RPC 记录标记 / XDR / MOUNT + NFS 过程）
INCLUDEPATH += ../../src
SOURCES += \
    tst_nfs.cpp \
    ../../src/services/nfsserver.cpp \
    ../../src/services/serverservice.cpp
HEADERS += \
    ../../src/services/nfsserver.h \
    ../../src/services/serverservice.h
