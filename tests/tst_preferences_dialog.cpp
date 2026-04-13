/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 ************************************************************************/

#include "tst_preferences_dialog.h"

#include <QtTest/QtTest>

#include <QMap>
#include <QVariant>

#include "preferences.h"
#include "extension/variantmanager.h"
#include "QtPropertyBrowser/qttreepropertybrowser.h"
#include "QtPropertyBrowser/qtvariantproperty.h"

/*!*******************************************************************************************************************
 * \brief Finds the Preferences property browser inside the dialog.
 *
 * \param dlg Preferences dialog instance.
 * \return Pointer to the property browser or nullptr if not found.
 **********************************************************************************************************************/
static QtTreePropertyBrowser* findPropertyBrowser(Preferences& dlg)
{
    return dlg.findChild<QtTreePropertyBrowser*>();
}

/*!*******************************************************************************************************************
 * \brief Finds the VariantManager inside the dialog.
 *
 * \param dlg Preferences dialog instance.
 * \return Pointer to the variant manager or nullptr if not found.
 **********************************************************************************************************************/
static VariantManager* findVariantManager(Preferences& dlg)
{
    return dlg.findChild<VariantManager*>();
}

/*!*******************************************************************************************************************
 * \brief Finds a property by name among all top-level groups and their sub-properties.
 *
 * \param browser Property browser to search in.
 * \param name    Property name to locate.
 * \return Pointer to the property or nullptr if not found.
 **********************************************************************************************************************/
static QtProperty* findPropertyByName(QtTreePropertyBrowser* browser, const QString& name)
{
    if (!browser)
        return nullptr;

    for (QtProperty* top : browser->properties()) {
        if (!top)
            continue;

        if (top->propertyName() == name)
            return top;

        for (QtProperty* sub : top->subProperties()) {
            if (sub && sub->propertyName() == name)
                return sub;
        }
    }

    return nullptr;
}

/*!*******************************************************************************************************************
 * \brief Verifies that the Preferences dialog initializes values from the input map
 *        and applies the correct enabled state for Palace run mode related fields.
 *
 * The test checks that:
 *  - known values are propagated into the property browser,
 *  - PALACE_RUN_MODE is initialized correctly,
 *  - PALACE_INSTALL_PATH is enabled in Executable mode,
 *  - PALACE_RUN_SCRIPT is disabled in Executable mode.
 **********************************************************************************************************************/
void PreferencesDialogTest::setupPreferencesPanel_initializesValues_and_runModeState()
{
    QMap<QString, QVariant> prefs;
    prefs["MODEL_TEMPLATES_DIR"] = QString("/tmp/templates");
    prefs["Python Path"] = QString("/usr/bin/python3");
    prefs["PALACE_PYTHON"] = QString("/usr/bin/python3");
    prefs["PALACE_RUN_MODE"] = 0;
    prefs["PALACE_INSTALL_PATH"] = QString("/opt/palace");
    prefs["PALACE_RUN_SCRIPT"] = QString("/tmp/palace_launcher.sh");

    Preferences dlg(prefs);

    QtTreePropertyBrowser* browser = findPropertyBrowser(dlg);
    VariantManager* manager = findVariantManager(dlg);

    QVERIFY2(browser, "QtTreePropertyBrowser not found");
    QVERIFY2(manager, "VariantManager not found");

    QtProperty* pythonPathProp = findPropertyByName(browser, "Python Path");
    QtProperty* palacePythonProp = findPropertyByName(browser, "PALACE_PYTHON");
    QtProperty* palaceRunModeProp = findPropertyByName(browser, "PALACE_RUN_MODE");
    QtProperty* palaceInstallProp = findPropertyByName(browser, "PALACE_INSTALL_PATH");
    QtProperty* palaceScriptProp = findPropertyByName(browser, "PALACE_RUN_SCRIPT");

    QVERIFY2(pythonPathProp, "Python Path property not found");
    QVERIFY2(palacePythonProp, "PALACE_PYTHON property not found");
    QVERIFY2(palaceRunModeProp, "PALACE_RUN_MODE property not found");
    QVERIFY2(palaceInstallProp, "PALACE_INSTALL_PATH property not found");
    QVERIFY2(palaceScriptProp, "PALACE_RUN_SCRIPT property not found");

    QCOMPARE(manager->value(pythonPathProp).toString(), QString("/usr/bin/python3"));
    QCOMPARE(manager->value(palacePythonProp).toString(), QString("/usr/bin/python3"));
    QCOMPARE(manager->value(palaceRunModeProp).toInt(), 0);
    QCOMPARE(manager->value(palaceInstallProp).toString(), QString("/opt/palace"));
    QCOMPARE(manager->value(palaceScriptProp).toString(), QString("/tmp/palace_launcher.sh"));

    QVERIFY2(palaceInstallProp->isEnabled(), "PALACE_INSTALL_PATH shall be enabled in Executable mode");
    QVERIFY2(!palaceScriptProp->isEnabled(), "PALACE_RUN_SCRIPT shall be disabled in Executable mode");
}

/*!*******************************************************************************************************************
 * \brief Verifies that Apply stores edited values back into the preferences map.
 *
 * The test modifies several properties through the VariantManager, switches Palace
 * run mode to Script, applies the dialog, and checks that:
 *  - the preferences map contains the edited values,
 *  - PALACE_RUN_SCRIPT becomes enabled,
 *  - PALACE_INSTALL_PATH becomes disabled.
 **********************************************************************************************************************/
void PreferencesDialogTest::apply_updates_preferences_map()
{
    QMap<QString, QVariant> prefs;
    prefs["Python Path"] = QString("/usr/bin/python3");
    prefs["PALACE_RUN_MODE"] = 0;
    prefs["PALACE_INSTALL_PATH"] = QString("/opt/palace");
    prefs["PALACE_RUN_SCRIPT"] = QString();

    Preferences dlg(prefs);

    QtTreePropertyBrowser* browser = findPropertyBrowser(dlg);
    VariantManager* manager = findVariantManager(dlg);

    QVERIFY2(browser, "QtTreePropertyBrowser not found");
    QVERIFY2(manager, "VariantManager not found");

    QtProperty* pythonPathProp = findPropertyByName(browser, "Python Path");
    QtProperty* palaceRunModeProp = findPropertyByName(browser, "PALACE_RUN_MODE");
    QtProperty* palaceInstallProp = findPropertyByName(browser, "PALACE_INSTALL_PATH");
    QtProperty* palaceScriptProp = findPropertyByName(browser, "PALACE_RUN_SCRIPT");

    QVERIFY2(pythonPathProp, "Python Path property not found");
    QVERIFY2(palaceRunModeProp, "PALACE_RUN_MODE property not found");
    QVERIFY2(palaceInstallProp, "PALACE_INSTALL_PATH property not found");
    QVERIFY2(palaceScriptProp, "PALACE_RUN_SCRIPT property not found");

    manager->setValue(pythonPathProp, QString("/custom/python"));
    manager->setValue(palaceRunModeProp, 1);
    manager->setValue(palaceInstallProp, QString("/ignored/install/path"));
    manager->setValue(palaceScriptProp, QString("/custom/palace_launcher.sh"));

    QVERIFY2(!palaceInstallProp->isEnabled(), "PALACE_INSTALL_PATH shall be disabled in Script mode");
    QVERIFY2(palaceScriptProp->isEnabled(), "PALACE_RUN_SCRIPT shall be enabled in Script mode");

    QMetaObject::invokeMethod(&dlg, "on_btnApply_clicked", Qt::DirectConnection);

    QCOMPARE(prefs.value("Python Path").toString(), QString("/custom/python"));
    QCOMPARE(prefs.value("PALACE_RUN_MODE").toInt(), 1);
    QCOMPARE(prefs.value("PALACE_INSTALL_PATH").toString(), QString("/ignored/install/path"));
    QCOMPARE(prefs.value("PALACE_RUN_SCRIPT").toString(), QString("/custom/palace_launcher.sh"));
}

/*!*******************************************************************************************************************
 * \brief Verifies that Cancel closes the dialog.
 *
 * The dialog is shown briefly, Cancel is invoked directly, and the test verifies
 * that the dialog is no longer visible afterwards.
 **********************************************************************************************************************/
void PreferencesDialogTest::cancel_closes_dialog()
{
    QMap<QString, QVariant> prefs;

    Preferences dlg(prefs);
    dlg.show();

    QVERIFY(QTest::qWaitForWindowExposed(&dlg));

    QMetaObject::invokeMethod(&dlg, "on_btnCancel_clicked", Qt::DirectConnection);

    QVERIFY2(!dlg.isVisible(), "Preferences dialog shall be closed after Cancel");
}
