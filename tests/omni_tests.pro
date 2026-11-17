QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = omni_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += $$files(../src/*.h)
SOURCES += omni_tests.cpp $$files(../src/*.cpp)
SOURCES -= ../src/main.cpp
