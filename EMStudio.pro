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

# -----------------------------------------------------------------------------
# Post-link copy: scripts/ and keywords/ next to produced executable
# (Win: detect debug/release by checking where TARGET.exe exists)
# -----------------------------------------------------------------------------

SCRIPTS_SRC_DIR  = $$clean_path($$PWD/scripts)
KEYWORDS_SRC_DIR = $$clean_path($$PWD/keywords)

win32 {
    SCRIPTS_SRC_DIR_WIN  = $$system_path($$clean_path($$PWD/scripts))
    KEYWORDS_SRC_DIR_WIN = $$system_path($$clean_path($$PWD/keywords))
    OUT_DIR_WIN          = $$system_path($$clean_path($$OUT_PWD))
    TARGET_EXE           = $${TARGET}.exe
    COPY_CMD             = $$system_path($$clean_path($$PWD/tools/copy_assets.cmd))

    QMAKE_POST_LINK += cmd /c call \"$$COPY_CMD\" \"$$OUT_DIR_WIN\" \"$$TARGET_EXE\" \"$$SCRIPTS_SRC_DIR_WIN\" \"$$KEYWORDS_SRC_DIR_WIN\"
}

unix {
    # On Unix, prefer DESTDIR if used, otherwise OUT_PWD
    RUNTIME_DIR = $$clean_path($$OUT_PWD)
    !isEmpty(DESTDIR) {
        RUNTIME_DIR = $$clean_path($$OUT_PWD/$$DESTDIR)
    }

    SCRIPTS_DST_DIR  = $$clean_path($$RUNTIME_DIR/scripts)
    KEYWORDS_DST_DIR = $$clean_path($$RUNTIME_DIR/keywords)

    QMAKE_POST_LINK += $$quote(echo "[EMStudio] Runtime dir: $$RUNTIME_DIR") $$escape_expand(\\n\\t)

    # scripts
    QMAKE_POST_LINK += $$quote(echo "[EMStudio] Copy scripts: $$SCRIPTS_SRC_DIR to $$SCRIPTS_DST_DIR") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(mkdir -p "$$SCRIPTS_DST_DIR") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(cp -R "$$SCRIPTS_SRC_DIR"/. "$$SCRIPTS_DST_DIR"/) $$escape_expand(\\n\\t)

    # keywords (copy only if source dir exists)
    QMAKE_POST_LINK += $$quote(test -d "$$KEYWORDS_SRC_DIR" && echo "[EMStudio] Copy keywords: $$KEYWORDS_SRC_DIR to $$KEYWORDS_DST_DIR" || true) $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(test -d "$$KEYWORDS_SRC_DIR" && mkdir -p "$$KEYWORDS_DST_DIR" || true) $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(test -d "$$KEYWORDS_SRC_DIR" && cp -R "$$KEYWORDS_SRC_DIR"/. "$$KEYWORDS_DST_DIR"/ || true) $$escape_expand(\\n\\t)
}
