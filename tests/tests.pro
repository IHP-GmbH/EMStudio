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

# -----------------------------------------------------------------------------
# Versioning: MAJOR.MINOR
# - MAJOR is set manually
# - MINOR = number of commits since tag v<MAJOR>.0
# - If tag does not exist, fallback to total commit count
# -----------------------------------------------------------------------------

EMSTUDIO_MAJOR = 1
EMSTUDIO_TAG   = v$${EMSTUDIO_MAJOR}.0
EMSTUDIO_MINOR = 0

win32 {
    GIT_EXISTS = $$system(git --version >NUL 2>NUL && echo yes)

    equals(GIT_EXISTS, yes) {
        HAS_TAG = $$system(git rev-parse -q --verify refs/tags/$${EMSTUDIO_TAG} >NUL 2>NUL && echo yes)

        equals(HAS_TAG, yes) {
            EMSTUDIO_MINOR = $$system(git rev-list --count $${EMSTUDIO_TAG}..HEAD)
        } else {
            EMSTUDIO_MINOR = $$system(git rev-list --count HEAD)
        }
    }
}

unix {
    GIT_EXISTS = $$system(git --version >/dev/null 2>&1 && echo yes)

    equals(GIT_EXISTS, yes) {
        HAS_TAG = $$system(git rev-parse -q --verify refs/tags/$${EMSTUDIO_TAG} >/dev/null 2>&1 && echo yes)

        equals(HAS_TAG, yes) {
            EMSTUDIO_MINOR = $$system(git rev-list --count $${EMSTUDIO_TAG}..HEAD)
        } else {
            EMSTUDIO_MINOR = $$system(git rev-list --count HEAD)
        }
    }
}

EMSTUDIO_VERSION = $${EMSTUDIO_MAJOR}.$${EMSTUDIO_MINOR}

message(EMStudio version: $${EMSTUDIO_VERSION})

DEFINES += EMSTUDIO_VERSION_STR=\\\"$${EMSTUDIO_VERSION}\\\"
DEFINES += EMSTUDIO_MAJOR=$$EMSTUDIO_MAJOR

EMSTUDIO_GIT_DATE = $$system(git log -1 --format=%cd --date=format:%Y-%m-%dT%H:%M:%S)
DEFINES += EMSTUDIO_GIT_DATE_STR=\\\"$${EMSTUDIO_GIT_DATE}\\\"


