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
    src/about.ui \
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

    QMAKE_POST_LINK += $$quote(cmd /c ""$$COPY_CMD" "$$OUT_DIR_WIN" "$$TARGET_EXE" "$$SCRIPTS_SRC_DIR_WIN" "$$KEYWORDS_SRC_DIR_WIN"")
}

unix {
    RUNTIME_DIR = $$clean_path($$OUT_PWD)
    !isEmpty(DESTDIR) {
        RUNTIME_DIR = $$clean_path($$OUT_PWD/$$DESTDIR)
    }

    SCRIPTS_DST_DIR  = $$clean_path($$RUNTIME_DIR/scripts)
    KEYWORDS_DST_DIR = $$clean_path($$RUNTIME_DIR/keywords)

    QMAKE_POST_LINK += $$quote(@echo EMStudio: Runtime dir: $$RUNTIME_DIR) $$escape_expand(\\n\\t)

    QMAKE_POST_LINK += $$quote(@if [ $$SCRIPTS_SRC_DIR != $$SCRIPTS_DST_DIR ]; then \
        mkdir -p $$SCRIPTS_DST_DIR && \
        cp -R $$SCRIPTS_SRC_DIR/. $$SCRIPTS_DST_DIR/ ; \
    else \
        echo EMStudio: Skip scripts copy - in-place build; \
    fi) $$escape_expand(\\n\\t)

    QMAKE_POST_LINK += $$quote(@if [ -d $$KEYWORDS_SRC_DIR ]; then \
        if [ $$KEYWORDS_SRC_DIR != $$KEYWORDS_DST_DIR ]; then \
            mkdir -p $$KEYWORDS_DST_DIR && \
            cp -R $$KEYWORDS_SRC_DIR/. $$KEYWORDS_DST_DIR/ ; \
        else \
            echo EMStudio: Skip keywords copy - in-place build; \
        fi; \
    fi) $$escape_expand(\\n\\t)
}

# -----------------------------------------------------------------------------
# Versioning: MAJOR.MINOR
# - MAJOR is set manually
# - MINOR = number of commits since tag v<MAJOR>.0
# - If tag does not exist, fallback to total commit count
# -----------------------------------------------------------------------------

EMSTUDIO_MAJOR = 1
EMSTUDIO_TAG   = v$${EMSTUDIO_MAJOR}.0

win32 {
    # Ensure git is available
    GIT_EXISTS = $$system(git --version >NUL 2>NUL && echo yes)

    equals(GIT_EXISTS, yes) {

        # Check if tag v<MAJOR>.0 exists
        HAS_TAG = $$system(git rev-parse -q --verify refs/tags/$${EMSTUDIO_TAG} >NUL 2>NUL && echo yes)

        equals(HAS_TAG, yes) {
            EMSTUDIO_MINOR = $$system(git rev-list --count $${EMSTUDIO_TAG}..HEAD)
        } else {
            EMSTUDIO_MINOR = $$system(git rev-list --count HEAD)
        }

    } else {
        EMSTUDIO_MINOR = 0
    }

    EMSTUDIO_VERSION = $${EMSTUDIO_MAJOR}.$${EMSTUDIO_MINOR}
}

DEFINES += EMSTUDIO_VERSION_STR=\\\"$$EMSTUDIO_VERSION\\\"
DEFINES += EMSTUDIO_MAJOR=$$EMSTUDIO_MAJOR
