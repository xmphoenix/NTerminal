QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets serialport network

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    terminalwidget.cpp \
    connectiondialog.cpp \
    vt100parser.cpp \
    sessionmanager.cpp \
    logger.cpp \
    scriptrecorder.cpp

HEADERS += \
    terminalwidget.h \
    mainwindow.h \
    connectiondialog.h \
    vt100parser.h \
    sessionmanager.h \
    logger.h \
    scriptrecorder.h

TRANSLATIONS += \
    NTerminal_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    IssueFixing.md
