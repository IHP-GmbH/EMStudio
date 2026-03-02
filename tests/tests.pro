QT += testlib core gui widgets
TEMPLATE = app
TARGET = emstudio_golden_tests
CONFIG += console c++17

TOP = $$clean_path($$PWD/..)
include($$TOP/emstudio_sources.pri)

SOURCES += \
    main.cpp \
    test_utils.cpp \
    tst_openems_golden.cpp \
    tst_palace_golden.cpp

HEADERS += \
    test_utils.h \
    tst_openems_golden.h \
    tst_palace_golden.h

FORMS += \
    $$PWD/../src/about.ui \
    $$PWD/../src/mainwindow.ui \
    $$PWD/../src/preferences.ui

RESOURCES += \
    $$PWD/../icons.qrc

DEPENDPATH += $$PWD
INCLUDEPATH += $$PWD

DEFINES += EMSTUDIO_TESTING

include($$TOP/emstudio_coverage.pri)

