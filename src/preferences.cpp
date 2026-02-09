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
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/


#include <QDir>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVBoxLayout>

#include "extension/variantmanager.h"
#include "extension/variantfactory.h"

#include "QtPropertyBrowser/qtvariantproperty.h"
#include "QtPropertyBrowser/qttreepropertybrowser.h"

#include "preferences.h"
#include "ui_preferences.h"

/*!*******************************************************************************************************************
 * \brief Constructs the Preferences dialog and initializes the settings panel.
 *
 * \param preferences A reference to the application's preferences map to be modified by the dialog.
 * \param parent      Pointer to the parent widget, typically the main window.
 **********************************************************************************************************************/
Preferences::Preferences(QMap<QString, QVariant> &preferences, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::Preferences)
    , m_preferences(preferences)
{
    m_ui->setupUi(this);
    setupPreferencesPanel();
    setWindowTitle("Preferences");
}

/*!*******************************************************************************************************************
 * \brief Destructor for the Preferences dialog. Cleans up the UI.
 **********************************************************************************************************************/
Preferences::~Preferences()
{
    delete m_ui;
}

/*!*******************************************************************************************************************
 * \brief Closes the Preferences dialog without applying any changes.
 **********************************************************************************************************************/
void Preferences::on_btnCancel_clicked()
{
    close();
}

/*!*******************************************************************************************************************
 * \brief Builds the Preferences property browser UI and initializes all configuration fields.
 *
 * Creates a QtTreePropertyBrowser with a custom VariantManager/VariantFactory and populates it with
 * grouped settings:
 *  - EMStudio: paths to model templates shipped with (or used by) the application.
 *  - OpenEMS: Python executable and OpenEMS install root.
 *  - Palace: WSL Python, run mode (executable vs. script) and corresponding path.
 *
 * For MODEL_TEMPLATES_DIR the function attempts the following initialization order:
 *  1) Use saved value from \c m_preferences if it points to a folder containing required templates.
 *  2) Otherwise, fallback to "<applicationDir>/scripts" if it exists and contains templates.
 *  3) Otherwise, leave the field empty.
 *
 * Field hints (WhatsThis/tooltips) are provided to help the user understand expected values.
 **********************************************************************************************************************/
void Preferences::setupPreferencesPanel()
{
    m_propertyBrowser = new QtTreePropertyBrowser(this);
    m_variantManager  = new VariantManager(m_propertyBrowser);

    m_propertyBrowser->setResizeMode(QtTreePropertyBrowser::ResizeToContents);
    m_propertyBrowser->setPropertiesWithoutValueMarked(true);
    m_propertyBrowser->setHeaderVisible(false);

    QVBoxLayout *layout = new QVBoxLayout(m_ui->wgtPreferences);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_propertyBrowser);

    QtVariantEditorFactory *factory = new VariantFactory();
    m_propertyBrowser->setFactoryForManager(m_variantManager, factory);

    // -------------------------------------------------------------------------------------------------------------
    // EMStudio
    // -------------------------------------------------------------------------------------------------------------
    QtVariantProperty *emstudioGroup =
        m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), tr("EMStudio"));

    QtVariantProperty *tmplDirProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), QLatin1String("MODEL_TEMPLATES_DIR"));
    tmplDirProp->setWhatsThis("folder");
    tmplDirProp->setToolTip(tr("Folder containing EMStudio Python model templates.\n\n"
                               "Expected files:\n"
                               "  - palace_model.py\n"
                               "  - openems_model.py\n\n"
                               "If empty, EMStudio will try to use the templates shipped with the application "
                               "(<app>/scripts)."));

    auto dirLooksValid = [](const QString& p) -> bool {
        const QString pp = p.trimmed();
        if (pp.isEmpty()) return false;

        QDir d(pp);
        if (!d.exists()) return false;

        return QFileInfo(d.filePath(QStringLiteral("palace_model.py"))).exists() &&
               QFileInfo(d.filePath(QStringLiteral("openems_model.py"))).exists();
    };

    QString tmplDir = m_preferences.value(QStringLiteral("MODEL_TEMPLATES_DIR")).toString().trimmed();

    if (!dirLooksValid(tmplDir)) {
        const QString appLoc    = QCoreApplication::applicationDirPath();
        const QString scriptsDir = QDir(appLoc).filePath(QStringLiteral("scripts"));
        if (dirLooksValid(scriptsDir))
            tmplDir = scriptsDir;
        else
            tmplDir.clear();
    }

    tmplDirProp->setValue(QDir::toNativeSeparators(tmplDir));
    emstudioGroup->addSubProperty(tmplDirProp);

    m_propertyBrowser->addProperty(emstudioGroup);

    // -------------------------------------------------------------------------------------------------------------
    // OpenEMS
    // -------------------------------------------------------------------------------------------------------------
    QtVariantProperty *openemsGroup =
        m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), tr("OpenEMS"));

    QtVariantProperty *pythonPathProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), QLatin1String("Python Path"));
    pythonPathProp->setWhatsThis("file");
    pythonPathProp->setToolTip(tr("Path to the Python executable used to run OpenEMS models.\n"
                                  "Examples:\n"
                                  "  - C:\\\\Python311\\\\python.exe\n"
                                  "  - /usr/bin/python3"));
    pythonPathProp->setValue(m_preferences.value(QStringLiteral("Python Path"), QString()));
    openemsGroup->addSubProperty(pythonPathProp);

    // -------------------------------------------------------------------------------------------------------------
    // Palace
    // -------------------------------------------------------------------------------------------------------------
    QtVariantProperty *palaceGroup =
        m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), tr("Palace"));

    QtVariantProperty *pythonWslPathProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), QLatin1String("PALACE_PYTHON"));
    pythonWslPathProp->setWhatsThis("file");
    pythonWslPathProp->setToolTip(tr("Path to the Python executable inside WSL used for Palace workflows.\n"
                                     "Example:\n"
                                     "  - /usr/bin/python3\n\n"
                                     "This is typically needed when EMStudio runs Palace inside WSL."));
    pythonWslPathProp->setValue(m_preferences.value(QStringLiteral("PALACE_PYTHON"), QString()));
    palaceGroup->addSubProperty(pythonWslPathProp);

    m_palaceRunModeProp =
        m_variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), QLatin1String("PALACE_RUN_MODE"));
    m_palaceRunModeProp->setToolTip(tr("Select how Palace should be launched:\n"
                                       "  - Executable: run Palace from PALACE_INSTALL_PATH/bin/palace\n"
                                       "  - Script: run a custom launcher script specified by PALACE_RUN_SCRIPT"));
    {
        QStringList modes;
        modes << tr("Executable") << tr("Script");
        m_palaceRunModeProp->setAttribute(QStringLiteral("enumNames"), modes);
        m_palaceRunModeProp->setValue(m_preferences.value(QStringLiteral("PALACE_RUN_MODE"), 0));
    }
    palaceGroup->addSubProperty(m_palaceRunModeProp);

    m_palaceInstallPathProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), QLatin1String("PALACE_INSTALL_PATH"));
    m_palaceInstallPathProp->setWhatsThis("folder");
    m_palaceInstallPathProp->setToolTip(tr("Palace installation root folder.\n\n"
                                           "Used when PALACE_RUN_MODE is set to 'Executable'.\n"
                                           "Expected executable: <path>\n"
                                           "EMStudio will automatically append the required sub-path "
                                           "(e.g. bin/palace)."));
    m_palaceInstallPathProp->setValue(m_preferences.value(QStringLiteral("PALACE_INSTALL_PATH"), QString()));
    palaceGroup->addSubProperty(m_palaceInstallPathProp);

    m_palaceRunScriptProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), QLatin1String("PALACE_RUN_SCRIPT"));
    m_palaceRunScriptProp->setWhatsThis("file");
    m_palaceRunScriptProp->setToolTip(tr("Custom Palace launcher script.\n\n"
                                         "Used when PALACE_RUN_MODE is set to 'Script'.\n"
                                         "The script must be directly executable from the host environment."));
    m_palaceRunScriptProp->setValue(m_preferences.value(QStringLiteral("PALACE_RUN_SCRIPT"), QString()));
    palaceGroup->addSubProperty(m_palaceRunScriptProp);

    m_propertyBrowser->addProperty(openemsGroup);
    m_propertyBrowser->addProperty(palaceGroup);

    connect(m_variantManager, &QtVariantPropertyManager::valueChanged,
            this, &Preferences::onVariantValueChanged);

    onVariantValueChanged(m_palaceRunModeProp, m_palaceRunModeProp->value());
}

/*!*******************************************************************************************************************
 * \brief Updates the internal preferences map when a property value changes
 *        and maintains the mutual exclusivity between Palace install path
 *        and Palace run script.
 *
 * This slot is connected to QtVariantPropertyManager::valueChanged and keeps
 * the QMap-based preferences in sync with the property browser. When either
 * PALACE_INSTALL_PATH or PALACE_RUN_SCRIPT is edited, it enables one of them
 * and disables the other depending on which value is currently set.
 **********************************************************************************************************************/
void Preferences::onVariantValueChanged(QtProperty *property, const QVariant &value)
{
    if (!property)
        return;

    const QString name = property->propertyName();

    m_preferences.insert(name, value);

    if (name == QLatin1String("PALACE_RUN_MODE")) {
        const int mode = value.toInt();
        const bool useExecutable = (mode == 0);

        if (m_palaceInstallPathProp)
            m_palaceInstallPathProp->setEnabled(useExecutable);

        if (m_palaceRunScriptProp)
            m_palaceRunScriptProp->setEnabled(!useExecutable);
    }
}

/*!*******************************************************************************************************************
 * \brief Applies changes made in the preferences UI to the internal preferences map and closes the dialog.
 **********************************************************************************************************************/
void Preferences::on_btnApply_clicked()
{
    for (QtProperty* topLevelProp : m_propertyBrowser->properties()) {
        for (QtProperty* prop : topLevelProp->subProperties()) {
            QString name = prop->propertyName();
            QVariant value = m_variantManager->value(prop);
            if (m_preferences[name] != value) {
                m_preferences[name] = value;
            }
        }
    }

    close();
}


