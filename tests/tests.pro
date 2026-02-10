QT += testlib core gui widgets
TEMPLATE = app
TARGET = emstudio_golden_tests
CONFIG += console c++17

TOP = $$clean_path($$PWD/..)
include($$TOP/emstudio_sources.pri)

SOURCES += \
    tst_palace_golden.cpp

HEADERS += \
    tst_palace_golden.h

FORMS += \
    $$PWD/../src/mainwindow.ui \
    $$PWD/../src/preferences.ui

RESOURCES += \
    $$PWD/../icons.qrc

DEPENDPATH += $$PWD
INCLUDEPATH += $$PWD

DEFINES += EMSTUDIO_TESTING

include($$TOP/emstudio_coverage.pri)

