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

    QtVariantProperty *openemsGroup =
        m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), tr("OpenEMS"));

    QtVariantProperty *pythonPathProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), "Python Path");
    pythonPathProp->setWhatsThis("file");
    pythonPathProp->setValue(m_preferences.value("Python Path", ""));
    openemsGroup->addSubProperty(pythonPathProp);

    QtVariantProperty *openemsPathProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), "OPENEMS_INSTALL_PATH");
    openemsPathProp->setWhatsThis("folder");
    openemsPathProp->setValue(m_preferences.value("OPENEMS_INSTALL_PATH", ""));
    openemsGroup->addSubProperty(openemsPathProp);

    QtVariantProperty *palaceGroup =
        m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), tr("Palace"));

    QtVariantProperty *pythonWslPathProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), "PALACE_WSL_PYTHON");
    pythonWslPathProp->setWhatsThis("file");
    pythonWslPathProp->setValue(m_preferences.value("PALACE_WSL_PYTHON", ""));
    palaceGroup->addSubProperty(pythonWslPathProp);

    m_palaceRunModeProp =
        m_variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), "PALACE_RUN_MODE");

    QStringList modes;
    modes << tr("Executable") << tr("Script");
    m_palaceRunModeProp->setAttribute("enumNames", modes);
    m_palaceRunModeProp->setValue(m_preferences.value("PALACE_RUN_MODE", 0));
    palaceGroup->addSubProperty(m_palaceRunModeProp);

    m_propertyBrowser->addProperty(openemsGroup);
    m_propertyBrowser->addProperty(palaceGroup);

    m_palaceInstallPathProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), "PALACE_INSTALL_PATH");
    m_palaceInstallPathProp->setWhatsThis("folder");
    m_palaceInstallPathProp->setValue(m_preferences.value("PALACE_INSTALL_PATH", ""));
    palaceGroup->addSubProperty(m_palaceInstallPathProp);

    m_palaceRunScriptProp =
        m_variantManager->addProperty(VariantManager::filePathTypeId(), "PALACE_RUN_SCRIPT");
    m_palaceRunScriptProp->setWhatsThis("file");
    m_palaceRunScriptProp->setValue(m_preferences.value("PALACE_RUN_SCRIPT", ""));
    palaceGroup->addSubProperty(m_palaceRunScriptProp);

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


