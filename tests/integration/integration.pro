QT += core network serialport testlib
QT -= gui
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_integration

# 真实环境功能测试（SSH/SFTP/SCP/串口）。
# 这些用例默认全部 QSKIP；设置对应的 NTERMINAL_TEST_* 环境变量后才会连接
# 真实主机/串口，因此不会在普通单元测试中产生外部依赖。
INCLUDEPATH += ../../src

# ---- libssh（与产品工程相同的 bundled 布局） ----
LIBSSH_ROOT = $$clean_path($$PWD/../../libssh)
!exists($$LIBSSH_ROOT/include/libssh/libssh.h) {
    error("libssh not found at $$LIBSSH_ROOT - integration tests need the bundled libssh")
}
INCLUDEPATH += $$LIBSSH_ROOT/include
LIBS += -L$$LIBSSH_ROOT/lib -lssh
LIBS += -lws2_32

# 让测试程序与 libssh 的 OpenSSL/pthread 运行时 DLL 位于同一目录
CONFIG(debug, debug|release): DESTDIR = $$OUT_PWD/debug
else: DESTDIR = $$OUT_PWD/release
QMAKE_POST_LINK += powershell -ExecutionPolicy Bypass -Command \"Copy-Item -Path '$$LIBSSH_ROOT/bin/*.dll' -Destination '$$DESTDIR' -Force -ErrorAction SilentlyContinue\" $$escape_expand(\\n\\t)

SOURCES += \
    tst_integration.cpp \
    ../../src/common/sshruntime.cpp \
    ../../src/session/connectionhandler.cpp \
    ../../src/session/connectionhandlerfactory.cpp \
    ../../src/session/serialhandler.cpp \
    ../../src/session/telnethandler.cpp \
    ../../src/session/ssshandler.cpp \
    ../../src/session/rloginhandler.cpp \
    ../../src/session/sftphandler.cpp \
    ../../src/session/ftphandler.cpp \
    ../../src/session/tftphandler.cpp \
    ../../src/files/sshfileclient.cpp

HEADERS += \
    ../../src/common/sshruntime.h \
    ../../src/session/connectionhandler.h \
    ../../src/session/connectionhandlerfactory.h \
    ../../src/session/serialhandler.h \
    ../../src/session/telnethandler.h \
    ../../src/session/ssshandler.h \
    ../../src/session/rloginhandler.h \
    ../../src/session/sftphandler.h \
    ../../src/session/ftphandler.h \
    ../../src/session/tftphandler.h
