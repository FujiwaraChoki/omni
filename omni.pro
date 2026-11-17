QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = omni
TEMPLATE = app

HEADERS += \
    src/apexdriver.h \
    src/backend.h \
    src/hidraw.h \
    src/keyboard.h \
    src/layout.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/apexdriver.cpp \
    src/backend.cpp \
    src/hidraw.cpp \
    src/keyboard.cpp \
    src/layout.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
