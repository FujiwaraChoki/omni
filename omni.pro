QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = omni
TEMPLATE = app

HEADERS += \
    src/apexdriver.h \
    src/backend.h \
    src/config.h \
    src/effects.h \
    src/hidraw.h \
    src/keyboard.h \
    src/layout.h \
    src/portal.h \
    src/screen.h \
    src/service.h \
    src/theme.h \
    src/translator.h \
    src/wootingdriver.h

SOURCES += \
    src/main.cpp \
    src/apexdriver.cpp \
    src/backend.cpp \
    src/config.cpp \
    src/effects.cpp \
    src/hidraw.cpp \
    src/keyboard.cpp \
    src/layout.cpp \
    src/portal.cpp \
    src/screen.cpp \
    src/service.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/wootingdriver.cpp

RESOURCES += src/resources.qrc
