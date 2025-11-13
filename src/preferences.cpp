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
    setWindowTitle("Prefernces");
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
 * \brief Sets up the preferences property browser, creating editable fields for environment variables like Python path
 *        and OPENEMS installation path.
 **********************************************************************************************************************/
void Preferences::setupPreferencesPanel()
{
    m_propertyBrowser = new QtTreePropertyBrowser(this);
    m_variantManager = new VariantManager(m_propertyBrowser);

    m_propertyBrowser->setResizeMode(QtTreePropertyBrowser::ResizeToContents);
    m_propertyBrowser->setPropertiesWithoutValueMarked(true);
    m_propertyBrowser->setHeaderVisible(false);

    QVBoxLayout* layout = new QVBoxLayout(m_ui->wgtPreferences);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_propertyBrowser);

    QtVariantEditorFactory* factory = new VariantFactory();
    m_propertyBrowser->setFactoryForManager(m_variantManager, factory);

    QtVariantProperty* envGroup = m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), "Environment");

    QtVariantProperty* pythonPathProp = m_variantManager->addProperty(VariantManager::filePathTypeId(), "Python Path");
    pythonPathProp->setWhatsThis("file");
    pythonPathProp->setValue(m_preferences.value("Python Path", ""));
    envGroup->addSubProperty(pythonPathProp);

    QtVariantProperty* pythonWslPathProp = m_variantManager->addProperty(VariantManager::filePathTypeId(), "PALACE_WSL_PYTHON");
    pythonWslPathProp->setWhatsThis("file");
    pythonWslPathProp->setValue(m_preferences.value("PALACE_WSL_PYTHON", ""));
    envGroup->addSubProperty(pythonWslPathProp);

    QtVariantProperty* openemsPathProp = m_variantManager->addProperty(VariantManager::filePathTypeId(), "OPENEMS_INSTALL_PATH");
    openemsPathProp->setValue(m_preferences.value("OPENEMS_INSTALL_PATH", ""));
    openemsPathProp->setWhatsThis("folder");
    envGroup->addSubProperty(openemsPathProp);

    QtVariantProperty* palacePathProp = m_variantManager->addProperty(VariantManager::filePathTypeId(), "PALACE_INSTALL_PATH");
    palacePathProp->setValue(m_preferences.value("PALACE_INSTALL_PATH", ""));
    palacePathProp->setWhatsThis("folder");
    envGroup->addSubProperty(palacePathProp);

    m_propertyBrowser->addProperty(envGroup);
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


