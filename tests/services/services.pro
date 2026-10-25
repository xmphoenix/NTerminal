QT += core network testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_services

# 被测模块：本机网络服务（ServiceUtil / ServerService / HTTP / FTP / TFTP / Telnet）
INCLUDEPATH += ../../src
SOURCES += \
    tst_services.cpp \
    ../../src/services/serverservice.cpp \
    ../../src/services/servermanager.cpp \
    ../../src/services/httpserver.cpp \
    ../../src/services/ftpserver.cpp \
    ../../src/services/tftpserver.cpp \
    ../../src/services/telnetserver.cpp \
    ../../src/services/nfsserver.cpp
HEADERS += \
    ../../src/services/serviceutil.h \
    ../../src/services/serverservice.h \
    ../../src/services/servermanager.h \
    ../../src/services/httpserver.h \
    ../../src/services/ftpserver.h \
    ../../src/services/tftpserver.h \
    ../../src/services/telnetserver.h \
    ../../src/services/nfsserver.h
