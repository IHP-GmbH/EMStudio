# SPDX-License-Identifier: GPL-3.0-or-later
# EMStudio – electromagnetic simulation setup and analysis GUI

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

INCLUDEPATH += src extension QtPropertyBrowser

SOURCES += \
    QtPropertyBrowser/qtbuttonpropertybrowser.cpp \
    QtPropertyBrowser/qteditorfactory.cpp \
    QtPropertyBrowser/qtgroupboxpropertybrowser.cpp \
    QtPropertyBrowser/qtpropertybrowser.cpp \
    QtPropertyBrowser/qtpropertybrowserutils.cpp \
    QtPropertyBrowser/qtpropertymanager.cpp \
    QtPropertyBrowser/qttreepropertybrowser.cpp \
    QtPropertyBrowser/qtvariantproperty.cpp \
    src/dielectric.cpp \
    extension/fileedit.cpp \
    extension/fileeditfactory.cpp \
    extension/filepathmanager.cpp \
    extension/qlineeditd2.cpp \
    extension/variantfactory.cpp \
    extension/variantmanager.cpp \
    src/finddialog.cpp \
    src/gdsreader.cpp \
    src/layer.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/material.cpp \
    src/preferences.cpp \
    src/pythoneditor.cpp \
    src/pythonparser.cpp \
    src/pythonsyntaxhighlighter.cpp \
    src/substrate.cpp \
    src/substrateview.cpp \
    src/xmlreader.cpp

HEADERS += \
    QtPropertyBrowser/qtbuttonpropertybrowser.h \
    QtPropertyBrowser/qteditorfactory.h \
    QtPropertyBrowser/qtgroupboxpropertybrowser.h \
    QtPropertyBrowser/qtpropertybrowser.h \
    QtPropertyBrowser/qtpropertybrowserutils_p.h \
    QtPropertyBrowser/qtpropertymanager.h \
    QtPropertyBrowser/qttreepropertybrowser.h \
    QtPropertyBrowser/qtvariantproperty.h \
    QtPropertyBrowser/SciDoubleSpinBox.h \
    src/dielectric.h \
    extension/fileedit.h \
    extension/fileeditfactory.h \
    extension/filepathmanager.h \
    extension/qlineeditd2.h \
    extension/variantfactory.h \
    extension/variantmanager.h \
    src/finddialog.h \
    src/layer.h \
    src/mainwindow.h \
    src/material.h \
    src/preferences.h \
    src/pythoneditor.h \
    src/pythonparser.h \
    src/pythonsyntaxhighlighter.h \
    src/substrate.h \
    src/substrateview.h

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

# Determine output subdir (debug/release)
OUT_SUBDIR = $$DESTDIR
isEmpty(OUT_SUBDIR) {
    CONFIG(debug, debug|release) {
        OUT_SUBDIR = debug
    } else {
        OUT_SUBDIR = release
    }
}

SCRIPTS_DST_DIR = $$clean_path($$OUT_PWD/$$OUT_SUBDIR/scripts)

win32 {
    SRC_WIN = $$shell_path($$SCRIPTS_SRC_DIR)
    DST_WIN = $$shell_path($$SCRIPTS_DST_DIR)

    QMAKE_POST_LINK += $$quote(cmd /c echo [EMStudio] Copy scripts: "$$SRC_WIN" ^> "$$DST_WIN") $$escape_expand(\\n\\t)

    # If destination exists as a FILE (not a dir), remove it (otherwise mkdir/xcopy fails)
    QMAKE_POST_LINK += $$quote(cmd /c if exist "$$DST_WIN" if not exist "$$DST_WIN\\NUL" del /F /Q "$$DST_WIN") $$escape_expand(\\n\\t)

    QMAKE_POST_LINK += $$quote(cmd /c if not exist "$$DST_WIN" mkdir "$$DST_WIN") $$escape_expand(\\n\\t)

    # Copy folder contents recursively
    QMAKE_POST_LINK += $$quote(cmd /c xcopy /E /I /H /K /Y "$$SRC_WIN\\*" "$$DST_WIN\\" ^>nul) $$escape_expand(\\n\\t)
}

unix {
    SCRIPTS_DST_DIR = $$clean_path($$OUT_PWD/$$OUT_SUBDIR/scripts)
    QMAKE_POST_LINK += $$quote(echo "[EMStudio] Copy scripts: $$SCRIPTS_SRC_DIR -> $$SCRIPTS_DST_DIR") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(mkdir -p "$$SCRIPTS_DST_DIR") $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$quote(cp -R "$$SCRIPTS_SRC_DIR"/. "$$SCRIPTS_DST_DIR"/) $$escape_expand(\\n\\t)
}
