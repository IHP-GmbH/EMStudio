# emstudio_sources.pri
# Common sources/headers for app + tests.
# Use TOP (repo root) to make paths robust for shadow builds and subdir projects.

isEmpty(TOP): TOP = $$PWD
TOP = $$clean_path($$TOP)

INCLUDEPATH += $$TOP $$TOP/src $$TOP/extension $$TOP/QtPropertyBrowser

SOURCES += \
    $$TOP/src/about.cpp \
    $$TOP/src/tips.cpp \
    $$TOP/src/keywordseditor.cpp \
    $$TOP/QtPropertyBrowser/qtbuttonpropertybrowser.cpp \
    $$TOP/QtPropertyBrowser/qteditorfactory.cpp \
    $$TOP/QtPropertyBrowser/qtgroupboxpropertybrowser.cpp \
    $$TOP/QtPropertyBrowser/qtpropertybrowser.cpp \
    $$TOP/QtPropertyBrowser/qtpropertybrowserutils.cpp \
    $$TOP/QtPropertyBrowser/qtpropertymanager.cpp \
    $$TOP/QtPropertyBrowser/qttreepropertybrowser.cpp \
    $$TOP/QtPropertyBrowser/qtvariantproperty.cpp \
    $$TOP/src/dielectric.cpp \
    $$TOP/extension/fileedit.cpp \
    $$TOP/extension/fileeditfactory.cpp \
    $$TOP/extension/filepathmanager.cpp \
    $$TOP/extension/qlineeditd2.cpp \
    $$TOP/extension/variantfactory.cpp \
    $$TOP/extension/variantmanager.cpp \
    $$TOP/src/finddialog.cpp \
    $$TOP/src/gdsreader.cpp \
    $$TOP/src/layer.cpp \
    $$TOP/src/mainwindow.cpp \
    $$TOP/src/material.cpp \
    $$TOP/src/preferences.cpp \
    $$TOP/src/pythonToEditor.cpp \
    $$TOP/src/pythonToStudio.cpp \
    $$TOP/src/pythoneditor.cpp \
    $$TOP/src/pythonparser.cpp \
    $$TOP/src/pythonsyntaxhighlighter.cpp \
    $$TOP/src/runOpenEms.cpp \
    $$TOP/src/runPalace.cpp \
    $$TOP/src/substrate.cpp \
    $$TOP/src/substrateview.cpp \
    $$TOP/src/verification.cpp \
    $$TOP/src/xmlreader.cpp

HEADERS += \
    $$TOP/src/about.h \
    $$TOP/src/keywordseditor.h \
    $$TOP/QtPropertyBrowser/qtbuttonpropertybrowser.h \
    $$TOP/QtPropertyBrowser/qteditorfactory.h \
    $$TOP/QtPropertyBrowser/qtgroupboxpropertybrowser.h \
    $$TOP/QtPropertyBrowser/qtpropertybrowser.h \
    $$TOP/QtPropertyBrowser/qtpropertybrowserutils_p.h \
    $$TOP/QtPropertyBrowser/qtpropertymanager.h \
    $$TOP/QtPropertyBrowser/qttreepropertybrowser.h \
    $$TOP/QtPropertyBrowser/qtvariantproperty.h \
    $$TOP/QtPropertyBrowser/SciDoubleSpinBox.h \
    $$TOP/src/dielectric.h \
    $$TOP/extension/fileedit.h \
    $$TOP/extension/fileeditfactory.h \
    $$TOP/extension/filepathmanager.h \
    $$TOP/extension/qlineeditd2.h \
    $$TOP/extension/variantfactory.h \
    $$TOP/extension/variantmanager.h \
    $$TOP/src/finddialog.h \
    $$TOP/src/layer.h \
    $$TOP/src/mainwindow.h \
    $$TOP/src/material.h \
    $$TOP/src/preferences.h \
    $$TOP/src/pythoneditor.h \
    $$TOP/src/pythonparser.h \
    $$TOP/src/pythonsyntaxhighlighter.h \
    $$TOP/src/substrate.h \
    $$TOP/src/substrateview.h
