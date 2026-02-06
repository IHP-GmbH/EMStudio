# SPDX-License-Identifier: GPL-3.0-or-later
# EMStudio – electromagnetic simulation setup and analysis GUI

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

INCLUDEPATH += src extension QtPropertyBrowser

include(emstudio_sources.pri)
SOURCES += src/main.cpp

FORMS += \
    src/mainwindow.ui \
    src/preferences.ui

RC_FILE = appicon.rc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    icons.qrc

SCRIPTS_SRC_DIR = $$clean_path($$PWD/scripts)

# Determine output subdir (debug/release) for cases where it's actually used
OUT_SUBDIR = $$DESTDIR
isEmpty(OUT_SUBDIR): OUT_SUBDIR = .
equals(OUT_SUBDIR, .): OUT_SUBDIR =

isEmpty(OUT_SUBDIR) {
    CONFIG(debug, debug|release) {
        OUT_SUBDIR = debug
    } else {
        OUT_SUBDIR = release
    }
}

SCRIPTS_DST_DIR =
equals(OUT_PWD_CLEAN, $$PWD_CLEAN) {
    # in-source build (make in repo root)
    SCRIPTS_DST_DIR = $$clean_path($$OUT_PWD/scripts)
} else {
    # shadow build (QtCreator)
    isEmpty(OUT_SUBDIR) {
        SCRIPTS_DST_DIR = $$clean_path($$OUT_PWD/scripts)
    } else {
        SCRIPTS_DST_DIR = $$clean_path($$OUT_PWD/$$OUT_SUBDIR/scripts)
    }
}

win32 {
    SRC_WIN = $$system_path($$SCRIPTS_SRC_DIR)
    DST_WIN = $$system_path($$SCRIPTS_DST_DIR)

    QMAKE_POST_LINK += $$quote(cmd /c echo [EMStudio] Copy scripts: "$$SRC_WIN" to "$$DST_WIN") $$escape_expand(\\n\\t)

    # if DST exists and is a file -> delete it
    QMAKE_POST_LINK += $$quote(cmd /c if exist "$$DST_WIN" if not exist "$$DST_WIN\\NUL" del /F /Q "$$DST_WIN") $$escape_expand(\\n\\t)

    # ensure directory exists
    QMAKE_POST_LINK += $$quote(cmd /c if not exist "$$DST_WIN\\NUL" mkdir "$$DST_WIN") $$escape_expand(\\n\\t)

    # copy (note: no unquoted trailing backslash argument)
    QMAKE_POST_LINK += $$quote(cmd /c xcopy /E /I /H /K /Y "$$SRC_WIN\\*" "$$DST_WIN" >nul) $$escape_expand(\\n\\t)
}


unix {
    !equals(OUT_PWD_CLEAN, $$PWD_CLEAN) {
        QMAKE_POST_LINK += $$quote(echo "[EMStudio] Copy scripts: $$SCRIPTS_SRC_DIR to $$SCRIPTS_DST_DIR") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += $$quote(mkdir -p "$$SCRIPTS_DST_DIR") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += $$quote(cp -R "$$SCRIPTS_SRC_DIR"/. "$$SCRIPTS_DST_DIR"/) $$escape_expand(\\n\\t)
    } else {
        QMAKE_POST_LINK += $$quote(echo "[EMStudio] In-source build: scripts already in $$SCRIPTS_SRC_DIR") $$escape_expand(\\n\\t)
    }
}

