QT += core gui widgets testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tst_theme

# 被测模块：src/theme/thememanager + src/terminal/terminaltheme
# 资源用于验证 app.qss 占位符替换（:/styles/app.qss）
INCLUDEPATH += ../../src
SOURCES += \
    tst_theme.cpp \
    ../../src/theme/thememanager.cpp \
    ../../src/terminal/terminaltheme.cpp
HEADERS += \
    ../../src/theme/thememanager.h \
    ../../src/terminal/terminaltheme.h
RESOURCES += ../../resources/resources.qrc
