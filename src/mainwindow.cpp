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


#include <QFile>
#include <QDebug>
#include <QProcess>
#include <QFileInfo>
#include <QSettings>
#include <QJsonArray>
#include <QJsonValue>
#include <QFileDialog>
#include <QJsonObject>
#include <QMessageBox>
#include <QCloseEvent>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QProcessEnvironment>

#include "extension/variantmanager.h"
#include "extension/variantfactory.h"

#include "QtPropertyBrowser/qtvariantproperty.h"
#include "QtPropertyBrowser/qttreepropertybrowser.h"

#include "mainwindow.h"
#include "preferences.h"
#include "ui_mainwindow.h"
#include "substrateview.h"
#include "pythonparser.h"

/*!*******************************************************************************************************************
 * \brief Checks whether a string represents an integer value.
 *
 * Converts the trimmed string using QString::toInt() and returns whether the conversion
 * succeeded. Useful to distinguish between numeric GDS layer labels and named layers.
 *
 * \param s  Input string to test.
 * \return   True if \a s can be parsed as an integer (after trimming); otherwise false.
 **********************************************************************************************************************/
static bool strIsInt(const QString& s)
{
    bool ok=false; s.trimmed().toInt(&ok); return ok;
}

/*!*******************************************************************************************************************
 * \brief Adds an item to a combo box only if it does not already exist.
 *
 * This helper is similar to ensureComboHasItem() and is used when a lightweight
 * uniqueness guard is needed while building/updating the list of items.
 * No-op when \a box is nullptr.
 *
 * \param box   Target QComboBox.
 * \param text  Item text to add if missing.
 **********************************************************************************************************************/
static void addUnique(QComboBox* box, const QString& text)
{
    if (!box) return;
    if (box->findText(text) < 0) box->addItem(text);
}

/*!*******************************************************************************************************************
 * \brief Comparator that sorts numbers before non-numbers, with numeric strings
 *        sorted by value and non-numeric strings sorted lexicographically.
 *
 * Examples of the resulting order: "1", "2", "10", "TopMetal1", "Via1".
 *
 * \param a  First string.
 * \param b  Second string.
 * \return   True if \a a should come before \a b according to the described policy.
 **********************************************************************************************************************/
static bool numFirstComparator(const QString& a, const QString& b)
{
    bool oka=false, okb=false;
    const int ia = a.toInt(&oka);
    const int ib = b.toInt(&okb);
    if (oka && okb) return ia < ib;            // both numbers → numeric sort
    if (oka != okb) return oka;                // numbers before non-numbers
    return QString::localeAwareCompare(a, b) < 0;
}

/*!*******************************************************************************************************************
 * \brief Rebuilds a combo box’s items using the current layer mapping and target mode,
 *        while preserving the current selection.
 *
 * The function iterates the existing items, converts each entry using the provided
 * GDS<->Name maps depending on \a namesMode, de-duplicates entries, and then repopulates
 * the combo box with a deterministically sorted list:
 *  - Names mode: locale-aware alphabetical sort.
 *  - Numbers mode: numeric strings first (by value), then non-numeric names.
 *
 * If the current selection can be mapped to the target mode, it is converted and re-selected.
 * If it cannot be found after rebuilding, it is appended and selected as a fallback.
 *
 * \param box         Combo box to rebuild (no-op if nullptr).
 * \param gdsToName   Map from GDS layer number to substrate layer name.
 * \param nameToGds   Reverse map from substrate layer name to GDS layer number.
 * \param namesMode   True to display names, false to display numeric GDS layers.
 **********************************************************************************************************************/
static void rebuildComboWithMapping(QComboBox* box,
                                    const QHash<int, QString>& gdsToName,
                                    const QHash<QString, int>& nameToGds,
                                    bool namesMode)
{
    if (!box) return;

    QString cur = box->currentText().trimmed();

    QStringList newItems;
    QSet<QString> seen;

    const int N = box->count();
    newItems.reserve(N);
    for (int i = 0; i < N; ++i) {
        QString t = box->itemText(i).trimmed();
        bool ok=false; const int n = t.toInt(&ok);

        if (namesMode) {
            if (ok && gdsToName.contains(n)) t = gdsToName.value(n);
        } else {
            if (!ok && nameToGds.contains(t)) t = QString::number(nameToGds.value(t));
        }

        if (!seen.contains(t)) { newItems << t; seen.insert(t); }
    }

    if (namesMode) {
        std::sort(newItems.begin(), newItems.end(),
                  [](const QString& a, const QString& b){
                      return QString::localeAwareCompare(a, b) < 0;
                  });
    } else {
        std::sort(newItems.begin(), newItems.end(), numFirstComparator);
    }

    if (namesMode) {
        bool ok=false; int n = cur.toInt(&ok);
        if (ok && gdsToName.contains(n)) cur = gdsToName.value(n);
    } else {
        if (nameToGds.contains(cur)) cur = QString::number(nameToGds.value(cur));
    }

    box->clear();
    box->addItems(newItems);
    int idx = box->findText(cur);
    if (idx < 0 && !cur.isEmpty()) { box->addItem(cur); idx = box->count()-1; }
    if (idx >= 0) box->setCurrentIndex(idx);
}

/*!*******************************************************************************************************************
 * \brief Constructs the main window and initializes all components and settings.
 * \param parent Pointer to the parent widget, default is nullptr.
 **********************************************************************************************************************/
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_blockPortChanges(false)
{
    m_ui->setupUi(this);

    this->setWindowTitle("EMStudio");
    this->setWindowIcon(QIcon(":/logo"));

    connect(m_ui->tblPorts, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem*){
                if (!m_blockPortChanges) setStateChanged();
            });

    connect(m_ui->editRunPythonScript, &QTextEdit::textChanged,
            this, [this]() {
                setStateChanged();
            });

    addDockWidget(Qt::BottomDockWidgetArea, m_ui->dockLog);

    m_ui->btnAddPort->setEnabled(false);

    setupSettingsPanel();

    setupTabMapping();
    showTab(m_tabMap.value("Main", 0));

    loadSettings();
    updateSubLayerNamesCheckboxState();

    refreshSimToolOptions();

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_ui->editSimulationLog->setFont(mono);

    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Destructor of the MainWindow class. Cleans up UI resources.
 **********************************************************************************************************************/
MainWindow::~MainWindow()
{
    delete m_ui;
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the "Simulation Tool" combo box (cbxSimTool) based on configured install paths.
 *
 * Reads OPENEMS_INSTALL_PATH and PALACE_INSTALL_PATH from \c m_preferences, validates them with
 * pathLooksValid(), and repopulates \c cbxSimTool with the available tools ("OpenEMS", "Palace").
 * If none are valid, a placeholder item is shown and the combo is disabled. Emits an info() message
 * summarizing what is enabled.
 **********************************************************************************************************************/
void MainWindow::refreshSimToolOptions()
{
    QSignalBlocker blocker(m_ui->cbxSimTool);

    const QString openemsPath = m_preferences.value("OPENEMS_INSTALL_PATH").toString();
    const QString palacePath  = m_preferences.value("PALACE_INSTALL_PATH").toString();

    const bool hasOpenEMS = pathLooksValid(openemsPath);
    const bool hasPalace  = pathLooksValid(palacePath, "bin/palace");

    m_ui->cbxSimTool->clear();

    int items = 0;
    if (hasOpenEMS) {
        m_ui->cbxSimTool->addItem("OpenEMS", "openems");
        ++items;
    }
    if (hasPalace) {
        m_ui->cbxSimTool->addItem("Palace", "palace");
        ++items;
    }

    if (items == 0) {
        m_ui->cbxSimTool->addItem("No simulation tool configured");
        m_ui->cbxSimTool->setEnabled(false);
        info("No valid simulation tools found. Set OPENEMS_INSTALL_PATH and/or PALACE_INSTALL_PATH in Preferences.");
    } else {
        m_ui->cbxSimTool->setEnabled(true);
        m_ui->cbxSimTool->setCurrentIndex(0);

        QStringList enabled;
        if (hasOpenEMS) enabled << "OpenEMS";
        if (hasPalace)  enabled << "Palace";
        info(QString("Enabled simulation tools: %1").arg(enabled.join(", ")));

        int restoreIdx = -1;
        const QString wantedKey = m_preferences.value("SIMULATION_TOOL_KEY").toString().trimmed().toLower();
        if (!wantedKey.isEmpty())
            restoreIdx = m_ui->cbxSimTool->findData(wantedKey);

        if (restoreIdx >= 0) {
            m_ui->cbxSimTool->setCurrentIndex(restoreIdx);
        } else {
            const int savedIdx = m_preferences.value("SIMULATION_TOOL_INDEX", 0).toInt();
            if (savedIdx >= 0 && savedIdx < m_ui->cbxSimTool->count())
                m_ui->cbxSimTool->setCurrentIndex(savedIdx);
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Heuristically validates a tool installation path on Windows/WSL.
 *
 * For Windows paths, checks that the directory exists and, if \p relativeExe is provided,
 * verifies that the expected executable file also exists (e.g. "<path>/bin/palace").
 * For Linux/WSL-like paths ("/..." or "\\\\wsl$"), existence cannot be reliably checked from
 * a Windows process, so any non-empty value is accepted and the actual launch will be the truth test.
 *
 * \param path        The install root path to validate (Windows or WSL-style).
 * \param relativeExe Optional relative executable path to verify under \p path (e.g. "bin/palace").
 * \return \c true if the path looks usable according to the above rules, otherwise \c false.
 **********************************************************************************************************************/
bool MainWindow::pathLooksValid(const QString &path, const QString &relativeExe) const
{
    if (path.trimmed().isEmpty())
        return false;

    const bool looksLinux = path.startsWith('/') || path.startsWith("\\\\wsl$");
    if (looksLinux) {
        return true;
    }

    QFileInfo dirInfo(path);
    if (!dirInfo.exists() || !dirInfo.isDir())
        return false;

    if (!relativeExe.isEmpty()) {
        const QString exePath = QDir(path).filePath(relativeExe);
        QFileInfo exeInfo(exePath);
        return exeInfo.exists() && exeInfo.isFile();
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Converts a Windows path (e.g. "C:\foo\bar") to a WSL path ("/mnt/c/foo/bar").
 *        If the path already looks Linux-like, returns it unchanged.
 **********************************************************************************************************************/
QString MainWindow::toWslPath(const QString &winPath)
{
    if (winPath.startsWith('/')) return winPath;
    if (winPath.startsWith("\\\\wsl$")) return winPath;

    QString p = winPath;
    p.replace('\\', '/');
    if (p.size() >= 2 && p[1] == ':') {
        const QChar drive = p[0].toLower();
        p.remove(0, 2); // remove "C:"
        if (!p.startsWith('/')) p.prepend('/');
        p.prepend(QString("/mnt/%1").arg(drive));
    }
    return p;
}


/*!*******************************************************************************************************************
 * \brief This event handler is called when a close event is triggered. It checks for unsaved changes,
 * prompts the user to save them, and saves application settings if closing proceeds.
 * \param event Pointer to the QCloseEvent object.
 **********************************************************************************************************************/
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (isStateChanged()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Unsaved Changes"),
            tr("The run file has been modified. Do you want to save your changes?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
            );

        if (reply == QMessageBox::Yes) {
            if (m_runFile.isEmpty()) {
                QString filePath = QFileDialog::getSaveFileName(this, tr("Save Run File"), QString(), tr("Run Files (*.json);;All Files (*)"));
                if (!filePath.isEmpty()) {
                    saveRunFile(filePath);
                    setStateSaved();
                } else {
                    event->ignore();
                    return;
                }
            } else {
                saveRunFile(m_runFile);
                setStateSaved();
            }
        } else if (reply == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
    }

    saveSettings();
    QMainWindow::closeEvent(event);
}

/*!*******************************************************************************************************************
 * \brief Appends an informational message to the log window. Optionally clears the log first.
 * \param msg The message to display.
 * \param clear If true, clears the log before displaying the message.
 **********************************************************************************************************************/
void MainWindow::info(const QString &msg, bool clear)
{
    if (clear)
        m_ui->txtLog->clear();

    m_ui->txtLog->setTextColor(Qt::black);
    m_ui->txtLog->append(QString("[INFO] ") + msg);
}

/*!*******************************************************************************************************************
 * \brief Appends an error message to the log window. Optionally clears the log first.
 * \param msg The message to display.
 * \param clear If true, clears the log before displaying the message.
 **********************************************************************************************************************/
void MainWindow::error(const QString &msg, bool clear)
{
    if (clear)
        m_ui->txtLog->clear();

    m_ui->txtLog->setTextColor(Qt::red);
    m_ui->txtLog->append(QString("[ERROR] ") + msg);
}


/*!*******************************************************************************************************************
 * \brief Initializes internal mappings of tab names to widgets and indices for tab navigation.
 **********************************************************************************************************************/
void MainWindow::setupTabMapping()
{
    m_tabMap.clear();
    m_tabWidgets.clear();
    m_tabTitles.clear();

    QTabWidget *tabs = m_ui->tabSettings;

    for (int i = 0; i < tabs->count(); ++i) {
        QString title = tabs->tabText(i);
        QWidget *widget = tabs->widget(i);

        m_tabTitles << title;
        m_tabWidgets << widget;
        m_tabMap.insert(title, i);
    }
}


/*!*******************************************************************************************************************
 * \brief Displays the requested settings tab and handles synchronization with the Python script.
 *
 * Replaces the current tab widget in tabSettings with the widget corresponding to \a indexToShow.
 * When leaving a Python-related tab with a modified script, the user is prompted to either apply
 * the changes (save and re-parse the script, updating simulation settings) or discard them.
 * When entering a Python-related tab while the simulation state has unsaved changes, the user is
 * prompted to save the state so that an up-to-date Python script can be regenerated and shown.
 *
 * \param indexToShow Index of the tab to display.
 **********************************************************************************************************************/
void MainWindow::showTab(int indexToShow)
{
    QTabWidget *tabs = m_ui->tabSettings;

    QString prevTitle;
    if (tabs->count() > 0)
        prevTitle = tabs->tabText(0);

    if (tabs->count() > 0 &&
        prevTitle.toLower().contains("python") &&
        m_ui->editRunPythonScript->document()->isModified() &&
        QFileInfo(m_ui->txtRunPythonScript->text()).exists())
    {
        QMessageBox msg(this);
        msg.setIcon(QMessageBox::Question);
        msg.setWindowTitle(tr("Apply Python changes"));
        msg.setText(tr("The Python script was modified.\n"
                       "Do you want to apply these changes to the simulation settings?"));
        QPushButton *applyBtn   = msg.addButton(tr("&Apply"),   QMessageBox::AcceptRole);
        QPushButton *discardBtn = msg.addButton(tr("&Discard"), QMessageBox::RejectRole);
        msg.setDefaultButton(applyBtn);

        msg.exec();

        if (msg.clickedButton() == applyBtn) {
            if (!applyPythonScriptFromEditor())
                return; // parsing failed, stay on current tab
        } else if (msg.clickedButton() == discardBtn) {
            m_ui->editRunPythonScript->document()->setModified(false);
        }
    }

    tabs->clear();

    if (indexToShow >= 0 && indexToShow < m_tabWidgets.size()) {
        QWidget *w = m_tabWidgets[indexToShow];
        QString title = m_tabTitles[indexToShow];
        tabs->addTab(w, title);

        if (isStateChanged() &&
            title.toLower().contains("python") &&
            QFileInfo(m_ui->txtRunPythonScript->text()).exists())
        {
            QMessageBox msg(this);
            msg.setIcon(QMessageBox::Warning);
            msg.setWindowTitle(tr("Unsaved changes"));
            msg.setText(tr("The simulation state has been changed.\n"
                           "To see the updated Python script, please save the state first."));
            QPushButton *saveBtn   = msg.addButton(tr("&Save"),   QMessageBox::AcceptRole);
            QPushButton *ignoreBtn = msg.addButton(tr("&Ignore"), QMessageBox::RejectRole);
            msg.setDefaultButton(saveBtn);

            msg.exec();

            if (msg.clickedButton() == saveBtn) {
                on_actionSave_triggered();
            }
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Saves the current application settings including geometry, state, system settings, and preferences.
 **********************************************************************************************************************/
void MainWindow::saveSettings()
{
    QSettings settings("EMStudio", "EMStudioApp");
    settings.setValue("MainWindow/geometry", saveGeometry());
    settings.setValue("MainWindow/state", saveState());

    settings.beginGroup("SystemSettings");
    for (auto it = m_sysSettings.constBegin(); it != m_sysSettings.constEnd(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();

    settings.beginGroup("Preferences");
    for (auto it = m_preferences.constBegin(); it != m_preferences.constEnd(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

/*!*******************************************************************************************************************
 * \brief Loads application settings such as window geometry, state, system settings, and preferences.
 **********************************************************************************************************************/
void MainWindow::loadSettings()
{
    QSettings settings("EMStudio", "EMStudioApp");
    restoreGeometry(settings.value("MainWindow/geometry").toByteArray());
    restoreState(settings.value("MainWindow/state").toByteArray());

    settings.beginGroup("SystemSettings");
    QStringList keys = settings.childKeys();
    for (const QString &key : keys) {
        m_sysSettings[key] = settings.value(key);
    }
    settings.endGroup();

    settings.beginGroup("Preferences");
    const QStringList prefKeys = settings.childKeys();
    for (const QString& key : prefKeys) {
        m_preferences[key] = settings.value(key);
    }
    settings.endGroup();
}

/*!*******************************************************************************************************************
 * \brief Sets up the simulation settings panel using QtPropertyBrowser to allow user configuration of parameters.
 **********************************************************************************************************************/
void MainWindow::setupSettingsPanel()
{
    m_propertyBrowser = new QtTreePropertyBrowser(this);
    m_variantManager = new VariantManager(m_propertyBrowser);

    m_propertyBrowser->setResizeMode(QtTreePropertyBrowser::ResizeToContents);
    m_propertyBrowser->setPropertiesWithoutValueMarked(true);
    m_propertyBrowser->setHeaderVisible(false);

    QVBoxLayout* layout = new QVBoxLayout(m_ui->wdgSettings);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_propertyBrowser);

    QtVariantEditorFactory* factory = new VariantFactory();
    m_propertyBrowser->setFactoryForManager(m_variantManager, factory);

    QtVariantProperty* simGroup = m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), "Simulation Settings");
    m_simSettingsGroup = simGroup;

    auto addDouble = [&](const QString& name, double value) {
        QtVariantProperty* prop = m_variantManager->addProperty(QVariant::Double, name);

        prop->setAttribute(QLatin1String("decimals"), 12);
        prop->setAttribute(QLatin1String("minimum"), -std::numeric_limits<double>::max());
        prop->setAttribute(QLatin1String("maximum"),  std::numeric_limits<double>::max());
        prop->setAttribute(QLatin1String("singleStep"), 0.0);

        prop->setValue(value);
        simGroup->addSubProperty(prop);
    };

    auto addInt = [&](const QString& name, int value) {
        QtVariantProperty* prop = m_variantManager->addProperty(QVariant::Int, name);
        prop->setValue(value);
        simGroup->addSubProperty(prop);
    };

    auto addBool = [&](const QString& name, bool value) {
        QtVariantProperty* prop = m_variantManager->addProperty(QVariant::Bool, name);
        prop->setValue(value);
        simGroup->addSubProperty(prop);
    };

    addBool("preview_only", false);
    addBool("postprocess_only", false);
    addDouble("unit", 1e-6);
    addInt("margin", 50);
    addDouble("fstart", 0.0);
    addDouble("fstop", 110e9);
    addInt("numfreq", 401);
    addDouble("refined_cellsize", 1.0);

    QtVariantProperty* boundariesGroup = m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), "Boundaries");

    QStringList boundaryOptions = { "PEC", "PMC", "MUR", "PML_8" };
    QStringList boundaryNames = { "X-", "X+", "Y-", "Y+", "Z-", "Z+" };
    QStringList boundaryDefaults = { "PEC", "PEC", "PEC", "PEC", "PEC", "PEC" };

    for (int i = 0; i < boundaryNames.size(); ++i) {
        QtVariantProperty* bndProp = m_variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), boundaryNames[i]);
        bndProp->setAttribute("enumNames", boundaryOptions);
        bndProp->setValue(boundaryOptions.indexOf(boundaryDefaults[i]));
        boundariesGroup->addSubProperty(bndProp);
    }

    m_propertyBrowser->addProperty(simGroup);
    m_propertyBrowser->addProperty(boundariesGroup);

    connect(m_variantManager, &QtVariantPropertyManager::valueChanged,
            this, &MainWindow::onSimulationSettingChanged);
}

/*!*******************************************************************************************************************
 * \brief Handles updates to simulation settings when a property value changes in the property browser.
 * \param property The property that was changed.
 * \param value The new value assigned to the property.
 **********************************************************************************************************************/
void MainWindow::onSimulationSettingChanged(QtProperty* property, const QVariant& value)
{
    QString name = property->propertyName();

    QtProperty* parent = nullptr;
    for (QtProperty* topProp : m_propertyBrowser->properties()) {
        for (QtProperty* prop : topProp->subProperties()) {
            if (prop->subProperties().contains(property)) {
                parent = prop;
                break;
            }
        }
        if (parent)
            break;
    }

    if (parent && parent->propertyName() == "Boundaries") {
        QVariantMap bndMap;
        for (QtProperty* sub : parent->subProperties()) {
            QString side = sub->propertyName();
            int idx = m_variantManager->value(sub).toInt();
            QStringList enumNames = m_variantManager->attributeValue(sub, "enumNames").toStringList();
            QString text = enumNames.value(idx);
            bndMap[side] = text;
        }
        m_simSettings["Boundaries"] = bndMap;
    } else {
        m_simSettings[name] = value;
    }

    setStateChanged();
}



/*!*******************************************************************************************************************
 * \brief Handles item clicks in the run control list, triggering tab switching based on selected item.
 * \param item The clicked list item.
 **********************************************************************************************************************/
void MainWindow::on_lstRunControl_itemClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    QString tabName = item->text();
    int tabIndex = m_tabMap.value(tabName, -1);
    if (tabIndex != -1) {
        showTab(tabIndex);
    }
}

/*!*******************************************************************************************************************
 * \brief Triggered when the "Exit" action is invoked. Closes the application.
 **********************************************************************************************************************/
void MainWindow::on_actionExit_triggered()
{
    close();
}

/*!*******************************************************************************************************************
 * \brief Triggered when the "Save As" action is invoked.
 *
 * Opens a file dialog to let the user choose a new file name and location
 * for saving the current simulation configuration. Internally calls
 * on_actionSave_triggered() after the file is selected.
 **********************************************************************************************************************/
void MainWindow::on_actionSave_As_triggered()
{
    on_actionSave_triggered();
}


/*!*******************************************************************************************************************
 * \brief Triggered when the "Save" action is invoked.
 *
 * If no run file is currently associated with the session, this method opens
 * a "Save File" dialog and suggests a default JSON filename derived from the
 * currently selected GDS file (if available). Otherwise, it saves the
 * simulation configuration to the existing run file.
 **********************************************************************************************************************/
void MainWindow::on_actionSave_triggered()
{
    if (m_runFile.isEmpty()) {
        const QString gdsPath        = m_ui->txtGdsFile->text().trimmed();
        const QString scriptPath     = m_ui->txtRunPythonScript->text().trimmed();
        const QString substratePath  = m_ui->txtSubstrate->text().trimmed();

        const bool hasGds        = !gdsPath.isEmpty() && QFileInfo(gdsPath).exists();
        const bool hasScript     = !scriptPath.isEmpty() && QFileInfo(scriptPath).exists();
        const bool hasSubstrate  = !substratePath.isEmpty() && QFileInfo(substratePath).exists();

        if (hasGds && hasScript && hasSubstrate) {
            QFileInfo gdsInfo(gdsPath);
            QString defaultDir    = gdsInfo.absolutePath();
            QString suggestedName = gdsInfo.completeBaseName() + ".json";
            m_runFile             = QDir(defaultDir).filePath(suggestedName);
        } else {
            QString defaultDir    = QDir::homePath();
            QString suggestedName = "simulation.json";

            if (hasGds) {
                QFileInfo gdsInfo(gdsPath);
                suggestedName = gdsInfo.completeBaseName() + ".json";
                defaultDir    = gdsInfo.absolutePath();
            }

            QString defaultPath = QDir(defaultDir).filePath(suggestedName);
            m_runFile = QFileDialog::getSaveFileName(
                this,
                tr("Save Simulation Run File"),
                defaultPath,
                tr("JSON Files (*.json);;All Files (*)")
                );

            if (m_runFile.isEmpty())
                return;
        }
    }

    const QString scriptPath = m_ui->txtRunPythonScript->text().trimmed();
    if (!scriptPath.isEmpty()) {
        QFile pyFile(scriptPath);
        if (pyFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&pyFile);
            out << m_ui->editRunPythonScript->toPlainText();
            pyFile.close();
            m_simSettings["RunPythonScript"] = scriptPath;
        } else {
            error(QString("Failed to save Python script to '%1': %2")
                      .arg(scriptPath, pyFile.errorString()));
        }

        loadPythonScriptToEditor(m_ui->txtRunPythonScript->text());
    }

    saveRunFile(m_runFile);
    setStateSaved();
}



/*!*******************************************************************************************************************
 * \brief Saves the simulation configuration and port definitions into a JSON file.
 * \param filePath Path to the file where the run configuration will be saved.
 **********************************************************************************************************************/
void MainWindow::saveRunFile(const QString& filePath)
{
    for (QtProperty* topProp : m_propertyBrowser->properties()) {
        QString topName = topProp->propertyName();

        if (topName == "Boundaries") {
            QVariantMap bndMap;
            for (QtProperty* sub : topProp->subProperties()) {
                QString side = sub->propertyName();
                int idx = m_variantManager->value(sub).toInt();
                QStringList enumNames = m_variantManager->attributeValue(sub, "enumNames").toStringList();
                QString text = enumNames.value(idx);
                bndMap[side] = text;
            }
            m_simSettings["Boundaries"] = bndMap;
        } else if (topName == "Simulation Settings") {
            for (QtProperty* prop : topProp->subProperties()) {
                QString name = prop->propertyName();
                QVariant value = m_variantManager->value(prop);
                m_simSettings[name] = value;
            }
        }
    }

    QJsonArray portsArray;
    int rowCount = m_ui->tblPorts->rowCount();
    int colCount = m_ui->tblPorts->columnCount();

    for (int row = 0; row < rowCount; ++row) {
        QJsonObject port;
        for (int col = 0; col < colCount; ++col) {
            QString header = m_ui->tblPorts->horizontalHeaderItem(col)->text();

            QWidget* widget = m_ui->tblPorts->cellWidget(row, col);
            if (QComboBox* combo = qobject_cast<QComboBox*>(widget)) {
                port[header] = combo->currentText();
            } else {
                QTableWidgetItem* item = m_ui->tblPorts->item(row, col);
                port[header] = item ? item->text() : "";
            }
        }
        portsArray.append(port);
    }

    QJsonObject root;

    if (m_simSettings.contains("Boundaries")) {
        QVariant val = m_simSettings["Boundaries"];
        if (val.type() == QVariant::Map) {
            QJsonObject bndObj = QJsonObject::fromVariantMap(val.toMap());
            root["Boundaries"] = bndObj;
        }
    }

    for (auto it = m_simSettings.constBegin(); it != m_simSettings.constEnd(); ++it) {
        if (it.key() == "Boundaries")
            continue;
        root[it.key()] = QJsonValue::fromVariant(it.value());
    }

    root["Ports"] = portsArray;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();

        info(QString("Saved run file to '%1'").arg(filePath), true);
        m_runFile = filePath;
        setStateSaved();
    } else {
        error(QString("Failed to save run file to '%1'").arg(filePath), true);
    }
}

/*!*******************************************************************************************************************
 * \brief Loads a previously saved simulation configuration from a JSON run file.
 * \param filePath Path to the JSON file to load.
 **********************************************************************************************************************/
void MainWindow::loadRunFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error(QString("Failed to open run file '%1'").arg(filePath), false);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error(QString("JSON parse error in run file: %1").arg(parseError.errorString()), false);
        return;
    }

    if (!doc.isObject()) {
        error("Invalid format: JSON root is not an object", false);
        return;
    }

    QJsonObject root = doc.object();
    m_simSettings.clear();

    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() == "Boundaries" && it.value().isObject()) {
            QJsonObject bounds = it.value().toObject();
            QVariantMap bndMap;
            for (auto b = bounds.begin(); b != bounds.end(); ++b) {
                bndMap[b.key()] = b.value().toString();
            }
            m_simSettings["Boundaries"] = bndMap;
        } else {
            m_simSettings[it.key()] = it.value().toVariant();
        }
    }

    m_runFile = filePath;
    info(QString("Loaded run file from '%1'").arg(filePath), false);

    updateSimulationSettings();

    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Updates simulation parameters in the GUI using values stored in \c m_simSettings.
 * This includes restoring property browser values, filling in text fields, loading port table, and reading GDS/substrate data.
 **********************************************************************************************************************/
void MainWindow::updateSimulationSettings()
{
    if (!m_variantManager)
        return;

    const QList<QtProperty*> topLevelProps = m_propertyBrowser->properties();

    for (QtProperty* groupProp : topLevelProps)
    {
        const QString groupName = groupProp->propertyName();
        const QList<QtProperty*> subProps = groupProp->subProperties();

        if (groupName == "Boundaries" && m_simSettings.contains("Boundaries")) {
            QVariantMap bndMap = m_simSettings["Boundaries"].toMap();
            for (QtProperty* prop : subProps) {
                const QString side = prop->propertyName();
                if (bndMap.contains(side)) {
                    QString valueStr = bndMap.value(side).toString();
                    QStringList options = m_variantManager->attributeValue(prop, "enumNames").toStringList();
                    int idx = options.indexOf(valueStr);
                    if (idx >= 0)
                        m_variantManager->setValue(prop, idx);
                }
            }
        }
        else {
            for (QtProperty* prop : subProps) {
                const QString name = prop->propertyName();
                if (m_simSettings.contains(name)) {
                    QVariant val = m_simSettings.value(name);

                    if (val.isValid())
                        m_variantManager->setValue(prop, val);
                }
            }
        }
    }

    if (m_simSettings.contains("RunDir"))
        m_ui->txtRunDir->setText(m_simSettings["RunDir"].toString());

    if (m_simSettings.contains("GdsFile"))
        m_ui->txtGdsFile->setText(m_simSettings["GdsFile"].toString());

    if (m_simSettings.contains("SubstrateFile"))
        m_ui->txtSubstrate->setText(m_simSettings["SubstrateFile"].toString());

    if (m_simSettings.contains("TopCell"))
        m_ui->cbxTopCell->setCurrentText(m_simSettings["TopCell"].toString());

    if (m_simSettings.contains("RunPythonScript")) {
        m_ui->txtRunPythonScript->setText(m_simSettings["RunPythonScript"].toString());
        if(QFileInfo().exists(m_simSettings["RunPythonScript"].toString())) {
            loadPythonScriptToEditor(m_simSettings["RunPythonScript"].toString());
        }
    }

    if(QFileInfo().exists(m_ui->txtGdsFile->text())) {
        updateGdsUserInfo();
    }

    if(QFileInfo().exists(m_ui->txtSubstrate->text())) {
        m_subLayers = readSubstrateLayers(m_ui->txtSubstrate->text());
        drawSubstrate(m_ui->txtSubstrate->text());
    }

    m_ui->tblPorts->setRowCount(0);

    if (m_simSettings.contains("Ports")) {
        rebuildLayerMapping();

        QList<int> gdsNums; gdsNums.reserve(m_layers.size());
        for (const auto& layer : m_layers) gdsNums.push_back(layer.first);
        std::sort(gdsNums.begin(), gdsNums.end());

        QStringList subNames = m_subLayers;
        subNames.removeDuplicates();
        std::sort(subNames.begin(), subNames.end(),
                  [](const QString& a, const QString& b){
                      return QString::localeAwareCompare(a, b) < 0;
                  });

        const bool namesMode = m_ui->cbSubLayerNames->isChecked();

        const QVariant portsVar = m_simSettings["Ports"];
        if (portsVar.canConvert<QVariantList>()) {
            const QVariantList portsList = portsVar.toList();

            auto setCurrentSafe = [](QComboBox* box, const QString& value){
                if (!box || value.isEmpty()) return;
                QSignalBlocker blocker(box);
                if (box->findText(value) < 0) box->addItem(value);
                box->setCurrentText(value);
            };

            m_blockPortChanges = true;

            for (const QVariant& v : portsList) {
                const QVariantMap portMap = v.toMap();

                const int row = m_ui->tblPorts->rowCount();
                m_ui->tblPorts->insertRow(row);

                m_ui->tblPorts->setItem(row, 0, new QTableWidgetItem(portMap.value("Num").toString()));
                m_ui->tblPorts->setItem(row, 1, new QTableWidgetItem(portMap.value("Voltage").toString()));
                m_ui->tblPorts->setItem(row, 2, new QTableWidgetItem(portMap.value("Z0").toString()));

                auto* sourceLayerBox = new QComboBox();
                auto* fromLayerBox   = new QComboBox();
                auto* toLayerBox     = new QComboBox();
                auto* directionBox   = new QComboBox();

                for (int n : gdsNums) {
                    const QString s = QString::number(n);
                    sourceLayerBox->addItem(s);
                    fromLayerBox->addItem(s);
                    toLayerBox->addItem(s);
                }
                for (const QString& nm : subNames) {
                    sourceLayerBox->addItem(nm);
                    fromLayerBox->addItem(nm);
                    toLayerBox->addItem(nm);
                }

                directionBox->addItems(QStringList() << "x" << "y" << "z");

                const QString src  = portMap.value("Source Layer").toString().trimmed();
                const QString from = portMap.value("From Layer").toString().trimmed();
                const QString to   = portMap.value("To Layer").toString().trimmed();
                QString dir        = portMap.value("Direction").toString().trimmed();
                if (dir.isEmpty()) dir = "z";

                setCurrentSafe(sourceLayerBox, src);
                setCurrentSafe(fromLayerBox,   from);
                setCurrentSafe(toLayerBox,     to);
                {
                    QSignalBlocker b(directionBox);
                    directionBox->setCurrentText(dir);
                }

                m_ui->tblPorts->setCellWidget(row, 3, sourceLayerBox);
                m_ui->tblPorts->setCellWidget(row, 4, fromLayerBox);
                m_ui->tblPorts->setCellWidget(row, 5, toLayerBox);
                m_ui->tblPorts->setCellWidget(row, 6, directionBox);

                rebuildComboWithMapping(sourceLayerBox, m_gdsToSubName, m_subNameToGds, namesMode);
                rebuildComboWithMapping(fromLayerBox,   m_gdsToSubName, m_subNameToGds, namesMode);
                rebuildComboWithMapping(toLayerBox,     m_gdsToSubName, m_subNameToGds, namesMode);

                hookPortCombo(sourceLayerBox);
                hookPortCombo(fromLayerBox);
                hookPortCombo(toLayerBox);
                hookPortCombo(directionBox);
            }

            m_blockPortChanges = false;
        }
    }


    if (m_ui->cbSubLayerNames->isEnabled() && m_ui->cbSubLayerNames->isChecked())
        applySubLayerNamesToPorts(true);
}


/*!*******************************************************************************************************************
 * \brief Updates the text color of a QLineEdit depending on the existence of the specified path.
 * \param lineEdit The QLineEdit to modify.
 * \param path The file or directory path to check.
 **********************************************************************************************************************/
void MainWindow::setLineEditPalette(QLineEdit* lineEdit, const QString& path)
{
    QPalette palette = lineEdit->palette();

    if (QFile::exists(path)) {
        palette.setColor(QPalette::Text, Qt::blue);
    } else {
        palette.setColor(QPalette::Text, Qt::red);
    }

    lineEdit->setPalette(palette);
}

/*!*******************************************************************************************************************
 * \brief Updates internal GDS information from the selected GDS file.
 * This includes extracting cells and layers, updating the top cell combo box, and enabling the "Add Port" button.
 **********************************************************************************************************************/
void MainWindow::updateGdsUserInfo()
{
    QString filePath = m_ui->txtGdsFile->text();
    if(!QFileInfo().exists(filePath)) {
        return;
    }

    m_sysSettings["GdsDir"] = QFileInfo(filePath).absolutePath();

    m_ui->btnAddPort->setEnabled(true);

    m_cells.clear();
    m_layers.clear();

    m_cells = extractGdsCellNames(filePath);
    m_layers = extractGdsLayerNumbers(filePath);

    m_ui->cbxTopCell->clear();
    m_ui->cbxTopCell->addItems(m_cells);
    m_ui->cbxTopCell->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    m_simSettings["GdsFile"] = m_ui->txtGdsFile->text();
    m_simSettings["TopCell"] = m_ui->cbxTopCell->currentText();

    updateSubLayerNamesCheckboxState();
}

/*!*******************************************************************************************************************
 * \brief Opens a file dialog for selecting a GDS file and updates UI and settings accordingly.
 **********************************************************************************************************************/
void MainWindow::on_btnGdsFile_clicked()
{
    QString defaultDir = QDir::homePath();

    if (m_sysSettings.contains("GdsDir")) {
        QString dirPath = m_sysSettings["GdsDir"].toString();
        if (QDir(dirPath).exists()) {
            defaultDir = dirPath;
        }
    }

    QString filePath = QFileDialog::getOpenFileName(this, tr("Select GDS File"), defaultDir,
                                                    tr("GDS Files (*.gds *.gdsii);;All Files (*)"));
    if (!filePath.isEmpty()) {
        m_ui->txtGdsFile->setText(filePath);
        updateGdsUserInfo();
        setStateChanged();
    }
}

/*!*******************************************************************************************************************
 * \brief Triggered when the GDS file text is edited by the user. Updates text color and simulation state.
 * \param arg1 The new path entered.
 **********************************************************************************************************************/
void MainWindow::on_txtGdsFile_textEdited(const QString &arg1)
{
    setLineEditPalette(m_ui->txtGdsFile, arg1);
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Triggered when the GDS file text is changed. Updates the UI and simulation state.
 * \param arg1 The new GDS file path.
 **********************************************************************************************************************/
void MainWindow::on_txtGdsFile_textChanged(const QString &arg1)
{
    setLineEditPalette(m_ui->txtGdsFile, arg1);
    updateGdsUserInfo();
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Opens a directory dialog for selecting the run output directory and updates simulation settings.
 **********************************************************************************************************************/
void MainWindow::on_btnRunDir_clicked()
{
    QString defaultDir = QDir::homePath();

    if (m_simSettings.contains("RunDir")) {
        QString dirPath = m_simSettings["RunDir"].toString();
        if (QDir(dirPath).exists()) {
            defaultDir = dirPath;
        }
    }

    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select Run Directory"), defaultDir,
                                                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dirPath.isEmpty()) {
        m_ui->txtRunDir->setText(dirPath);
        m_simSettings["RunDir"] = dirPath;
        setStateChanged();
    }
}

/*!*******************************************************************************************************************
 * \brief Triggered when the run directory text is edited. Updates text color and simulation state.
 * \param arg1 The newly typed path.
 **********************************************************************************************************************/
void MainWindow::on_txtRunDir_textEdited(const QString &arg1)
{
    setLineEditPalette(m_ui->txtRunDir, arg1);
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Triggered when the run directory text is changed. Updates path highlighting and simulation state.
 * \param arg1 The new directory path.
 **********************************************************************************************************************/
void MainWindow::on_txtRunDir_textChanged(const QString &arg1)
{
    setLineEditPalette(m_ui->txtRunDir, arg1);
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Opens a file dialog to select a Python script and loads it into the script editor.
 * Also updates the simulation settings accordingly.
 **********************************************************************************************************************/
void MainWindow::on_btnRunPythonScript_clicked()
{
    const bool noPortsYet = (m_ui->tblPorts->rowCount() == 0);

    const QString gdsPath = m_ui->txtGdsFile->text().trimmed();
    const QString subPath = m_ui->txtSubstrate->text().trimmed();

    const bool haveGds = !gdsPath.isEmpty() && QFileInfo().exists(gdsPath);
    const bool haveSub = !subPath.isEmpty() && QFileInfo().exists(subPath);

    if (noPortsYet && (!haveGds || !haveSub)) {
        const QString msg =
            tr("No ports are defined yet, but the GDS and/or substrate files are not set or do not exist.\n\n")
            + tr("Do you want to continue loading the Python script WITHOUT importing ports?\n\n")
            + tr("Choose \"Cancel\" to set GDS and substrate first.");

        this->raise();
        this->activateWindow();

        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Import Ports From Script"));
        box.setText(msg);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.setWindowModality(Qt::WindowModal);

        const int ret = box.exec();

        if (ret == QMessageBox::Cancel) {
            return;
        }
    }

    QString defaultDir = QDir::homePath();

    if (m_simSettings.contains("RunPythonScript")) {
        QFileInfo fileInfo(m_simSettings["RunPythonScript"].toString());
        if (fileInfo.exists()) {
            defaultDir = fileInfo.absolutePath();
        }
    }
    else if(QFileInfo().exists(m_ui->txtGdsFile->text())) {
        defaultDir = QFileInfo(m_ui->txtGdsFile->text()).absolutePath();
    }
    else if(QFileInfo().exists(m_ui->txtSubstrate->text())) {
        defaultDir = QFileInfo(m_ui->txtSubstrate->text()).absolutePath();
    }

    QString filePath = QFileDialog::getOpenFileName(this, tr("Select Python Script"), defaultDir,
                                                    tr("Python Files (*.py);;All Files (*)"));
    if (!filePath.isEmpty()) {
        m_ui->txtRunPythonScript->setText(filePath);
        m_simSettings["RunPythonScript"] = filePath;
        loadPythonScriptToEditor(filePath);
        setStateChanged();
    }
}

/*!*******************************************************************************************************************
 * \brief Triggered when the Python script path is edited. Updates text color and marks state as changed.
 * \param arg1 The newly typed script path.
 **********************************************************************************************************************/
void MainWindow::on_txtRunPythonScript_textEdited(const QString &arg1)
{
    setLineEditPalette(m_ui->txtRunPythonScript, arg1);
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Triggered when the Python script path is changed. Updates line color and marks state as changed.
 * \param arg1 The new script path.
 **********************************************************************************************************************/
void MainWindow::on_txtRunPythonScript_textChanged(const QString &arg1)
{
    setLineEditPalette(m_ui->txtRunPythonScript, arg1);
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Loads the Python simulation script into the editor and updates its parameters according to current settings.
 *
 * Depending on the active simulation tool, replaces either OpenEMS-style parameters
 * (top-level assignments like \c unit = 1e-6) or Palace-style parameters
 * (dictionary-style assignments like \c settings['unit'] = 1e-6) with the current values
 * from \c m_simSettings. The replacement ensures that the Python script reflects the
 * simulation parameters currently stored in the GUI and the run configuration JSON.
 *
 * Additionally, this function updates:
 * - The GDS and XML file paths (\c gds_filename, \c XML_filename)
 * - The list of defined simulation ports
 *
 * The modified script is then displayed in \c editRunPythonScript for user review and editing.
 *
 * \param filePath Path to the Python script file to load and update.
 **********************************************************************************************************************/
void MainWindow::loadPythonScriptToEditor(const QString &filePath)
{
    QString script;

    if (!m_ui->editRunPythonScript->document()->isModified()) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            error(QString("Failed to load Python script: %1").arg(file.errorString()), false);
            return;
        }
        script = QString::fromUtf8(file.readAll());
        file.close();
    } else {
        script = m_ui->editRunPythonScript->toPlainText();
    }

    const QString simKey = currentSimToolKey().toLower();

    if (simKey == QLatin1String("openems")) {
        const QStringList keysToReplace = {
            "unit", "margin", "fstart", "fstop", "numfreq", "refined_cellsize",
            "preview_only", "postprocess_only"
        };

        for (const QString &key : keysToReplace) {
            if (!m_simSettings.contains(key))
                continue;

            QVariant value = m_simSettings.value(key);
            QString strValue;

            if (value.type() == QVariant::Double)
                strValue = QString::number(value.toDouble(), 'g', 12);
            else if (value.type() == QVariant::Int)
                strValue = QString::number(value.toInt());
            else if (value.type() == QVariant::Bool)
                strValue = value.toBool() ? "True" : "False";
            else
                continue;

            QRegularExpression reVar(
                QString("^\\s*%1\\s*=.*$")
                    .arg(QRegularExpression::escape(key)),
                QRegularExpression::MultilineOption);
            script.replace(reVar, QString("%1 = %2").arg(key, strValue));

            QRegularExpression reDict(
                QString("^\\s*(\\w+)\\s*\\[\\s*['\"]%1['\"]\\s*\\]\\s*=.*$")
                    .arg(QRegularExpression::escape(key)),
                QRegularExpression::MultilineOption);
            script.replace(reDict,
                           QString("\\1['%1'] = %2").arg(key, strValue));
        }

        QStringList bndKeys = {"X-", "X+", "Y-", "Y+", "Z-", "Z+"};
        QStringList bndValues;
        QVariantMap bndMap;
        if (m_simSettings.contains("Boundaries"))
            bndMap = m_simSettings["Boundaries"].toMap();
        for (const QString &key : bndKeys)
            bndValues << bndMap.value(key, "PEC").toString();

        const QString bndPython = QString("['%1']").arg(bndValues.join("', '"));

        QRegularExpression reSettings(
            R"(^\s*(\w+)\s*\[\s*['"]Boundaries['"]\s*\]\s*=\s*.*$)",
            QRegularExpression::MultilineOption);
        script.replace(reSettings, QString("\\1['Boundaries'] = %1").arg(bndPython));

        QRegularExpression reBnd("^Boundaries\\s*=.*$", QRegularExpression::MultilineOption);
        script.replace(reBnd, QString("Boundaries = %1").arg(bndPython));
    }
    else if (simKey == QLatin1String("palace")) {
        for (auto it = m_simSettings.constBegin(); it != m_simSettings.constEnd(); ++it) {
            const QString &key = it.key();

            if (key == QLatin1String("Boundaries") ||
                key == QLatin1String("Ports") ||
                key == QLatin1String("GdsFile") ||
                key == QLatin1String("SubstrateFile") ||
                key == QLatin1String("RunDir") ||
                key == QLatin1String("RunPythonScript"))
                continue;

            QVariant value = it.value();
            QString strValue;

            if (value.type() == QVariant::Double)
                strValue = QString::number(value.toDouble(), 'g', 12);
            else if (value.type() == QVariant::Int ||
                     value.type() == QVariant::LongLong ||
                     value.type() == QVariant::UInt ||
                     value.type() == QVariant::ULongLong)
                strValue = QString::number(value.toLongLong());
            else if (value.type() == QVariant::Bool)
                strValue = value.toBool() ? "True" : "False";
            else
                continue;

            QString pattern = QString(
                                  R"(^\s*(\w+)\s*\[\s*['"]%1['"]\s*\]\s*=\s*.*$)"
                                  ).arg(QRegularExpression::escape(key));

            QRegularExpression re(pattern, QRegularExpression::MultilineOption);

            script.replace(re, QString("\\1['%1'] = %2").arg(key, strValue));
        }

        if (m_simSettings.contains("Boundaries")) {
            QStringList bndKeys = {"X-", "X+", "Y-", "Y+", "Z-", "Z+"};
            QStringList bndValues;
            QVariantMap bndMap = m_simSettings["Boundaries"].toMap();
            for (const QString &key : bndKeys)
                bndValues << bndMap.value(key, "PEC").toString();

            const QString bndPython = QString("['%1']").arg(bndValues.join("', '"));

            QRegularExpression reSettings(
                R"(^\s*(\w+)\s*\[\s*['"]Boundaries['"]\s*\]\s*=\s*.*$)",
                QRegularExpression::MultilineOption);
            script.replace(reSettings, QString("\\1['Boundaries'] = %1").arg(bndPython));
        }
    }

    if (m_simSettings.contains("GdsFile")) {
        QRegularExpression re("^gds_filename\\s*=.*$", QRegularExpression::MultilineOption);
        script.replace(re, QString("gds_filename = \"%1\"")
                               .arg(m_simSettings["GdsFile"].toString()));
    }
    if (m_simSettings.contains("SubstrateFile")) {
        QRegularExpression re("^XML_filename\\s*=.*$", QRegularExpression::MultilineOption);
        script.replace(re, QString("XML_filename = \"%1\"")
                               .arg(m_simSettings["SubstrateFile"].toString()));
    }

    QRegularExpression portBlock(
        R"(simulation_ports\s*=\s*simulation_setup\.all_simulation_ports\(\)\n(?:.|\n)*?(?=#[^\n]*simulation\s*={3,}))",
        QRegularExpression::MultilineOption
        );

    QString portCode = "simulation_ports = simulation_setup.all_simulation_ports()\n";

    auto toLayerName = [&](const QString& s) -> QString {
        bool ok = false;
        int n = s.toInt(&ok);
        if (ok && m_gdsToSubName.contains(n))
            return m_gdsToSubName.value(n);
        return s;
    };

    auto pyQuote = [](QString s) -> QString {
        s.replace('\\', "\\\\");
        s.replace('\'', "\\'");
        return "'" + s + "'";
    };

    for (int row = 0; row < m_ui->tblPorts->rowCount(); ++row) {
        const QString num  = m_ui->tblPorts->item(row, 0)->text().trimmed();
        const QString volt = m_ui->tblPorts->item(row, 1)->text().trimmed();
        const QString z0   = m_ui->tblPorts->item(row, 2)->text().trimmed();

        auto* srcBox  = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 3));
        auto* fromBox = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 4));
        auto* toBox   = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 5));
        auto* dirBox  = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 6));

        const QString srcVal  = srcBox  ? srcBox->currentText().trimmed()  : QString();
        const QString fromVal = fromBox ? fromBox->currentText().trimmed() : QString();
        const QString toVal   = toBox   ? toBox->currentText().trimmed()   : QString();
        const QString dirVal  = dirBox  ? dirBox->currentText().trimmed()  : QString("z");

        bool srcIsInt = false;
        const int srcNum = srcVal.toInt(&srcIsInt);

        const QString fromName = toLayerName(fromVal);
        const QString toName   = toLayerName(toVal);

        const QString srcField = srcIsInt
                                     ? QString("source_layernum=%1").arg(srcNum)
                                     : QString("source_layername=%1").arg(pyQuote(srcVal));

        portCode += QString(
                        "simulation_ports.add_port(simulation_setup.simulation_port("
                        "portnumber=%1, voltage=%2, port_Z0=%3, %4, "
                        "from_layername=%5, to_layername=%6, direction=%7))\n")
                        .arg(num, volt, z0, srcField,
                             pyQuote(fromName), pyQuote(toName), pyQuote(dirVal));
    }

    if (m_ui->tblPorts->rowCount() == 0) {
        rebuildLayerMapping();

        const auto parsed = parsePortsFromScript(script);
        if (!parsed.isEmpty())
            appendParsedPortsToTable(parsed);
    }

    updateSubLayerNamesAutoCheck();

    script.replace(portBlock, portCode);

    QSignalBlocker blocker(m_ui->editRunPythonScript);
    m_ui->editRunPythonScript->setPlainText(script);
    m_ui->editRunPythonScript->document()->setModified(false);
}

/*!*******************************************************************************************************************
 * \brief Adds a new port row to the port table with default values and available layers.
 **********************************************************************************************************************/
void MainWindow::on_btnAddPort_clicked()
{
    const int row = m_ui->tblPorts->rowCount();
    m_ui->tblPorts->insertRow(row);

    QList<int> gdsNums;
    gdsNums.reserve(m_layers.size());
    for (const auto& layer : m_layers)
        gdsNums.push_back(layer.first);
    std::sort(gdsNums.begin(), gdsNums.end());

    QStringList subNames = m_subLayers;
    subNames.removeDuplicates();
    std::sort(subNames.begin(), subNames.end(),
              [](const QString& a, const QString& b){
                  return QString::localeAwareCompare(a, b) < 0;
              });

    auto* sourceLayerBox = new QComboBox();
    auto* fromLayerBox   = new QComboBox();
    auto* toLayerBox     = new QComboBox();
    auto* directionBox   = new QComboBox();

    for (int n : gdsNums) {
        const QString s = QString::number(n);
        sourceLayerBox->addItem(s);
        fromLayerBox->addItem(s);
        toLayerBox->addItem(s);
    }

    for (const QString& nm : subNames) {
        sourceLayerBox->addItem(nm);
        fromLayerBox->addItem(nm);
        toLayerBox->addItem(nm);
    }

    directionBox->addItems(QStringList() << "x" << "y" << "z");
    directionBox->setCurrentText("z");

    m_ui->tblPorts->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
    m_ui->tblPorts->setItem(row, 1, new QTableWidgetItem("1"));
    m_ui->tblPorts->setItem(row, 2, new QTableWidgetItem("50"));
    m_ui->tblPorts->setCellWidget(row, 3, sourceLayerBox);
    m_ui->tblPorts->setCellWidget(row, 4, fromLayerBox);
    m_ui->tblPorts->setCellWidget(row, 5, toLayerBox);
    m_ui->tblPorts->setCellWidget(row, 6, directionBox);

    const bool namesMode = m_ui->cbSubLayerNames->isChecked();
    rebuildComboWithMapping(sourceLayerBox, m_gdsToSubName, m_subNameToGds, namesMode);
    rebuildComboWithMapping(fromLayerBox,   m_gdsToSubName, m_subNameToGds, namesMode);
    rebuildComboWithMapping(toLayerBox,     m_gdsToSubName, m_subNameToGds, namesMode);

    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Removes the currently selected port from the table if any is selected.
 **********************************************************************************************************************/
void MainWindow::on_btnReomovePort_clicked()
{
    int row = m_ui->tblPorts->currentRow();
    if (row >= 0) {
        m_ui->tblPorts->removeRow(row);
        setStateChanged();
    } else {
        error("No port selected to remove.", true);
    }
}

/*!*******************************************************************************************************************
 * \brief Removes all ports from the port table.
 **********************************************************************************************************************/
void MainWindow::on_btnRemovePorts_clicked()
{
    m_ui->tblPorts->setRowCount(0);
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Opens a file dialog to select a substrate XML file, updates view and settings.
 **********************************************************************************************************************/
void MainWindow::on_btnSubstrate_clicked()
{
    QString defaultDir = QDir::homePath();

    if (m_sysSettings.contains("SubstrateDir")) {
        QString dirPath = m_sysSettings["SubstrateDir"].toString();
        if (QDir(dirPath).exists()) {
            defaultDir = dirPath;
        }
    }

    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Select Substrate File"),
        defaultDir,
        tr("Substrate Definition (*.xml);;All Files (*)"));

    if (!filePath.isEmpty()) {
        m_ui->txtSubstrate->setText(filePath);
        setStateChanged();
        drawSubstrate(filePath);
        m_subLayers = readSubstrateLayers(m_ui->txtSubstrate->text());
        m_simSettings["SubstrateFile"] = m_ui->txtSubstrate->text();
        m_sysSettings["SubstrateDir"] = QFileInfo(filePath).absolutePath();
    }

    if(QFileInfo().exists(m_ui->txtGdsFile->text())) {
        m_ui->cbSubLayerNames->setCheckState(Qt::Checked);
    }
}

/*!*******************************************************************************************************************
 * \brief Triggered when the substrate file path is manually edited. Updates line color and marks state as changed.
 * \param arg1 The new text of the substrate file path.
 **********************************************************************************************************************/
void MainWindow::on_txtSubstrate_textEdited(const QString &arg1)
{
    setLineEditPalette(m_ui->txtSubstrate, arg1);
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Triggered when the substrate file path changes. Updates substrate view and state.
 * \param arg1 The updated path to the substrate file.
 **********************************************************************************************************************/
void MainWindow::on_txtSubstrate_textChanged(const QString &arg1)
{
    setLineEditPalette(m_ui->txtSubstrate, arg1);
    drawSubstrate(arg1);
    setStateChanged();
    updateSubLayerNamesCheckboxState();
}

/*!*******************************************************************************************************************
 * \brief Marks the simulation state as changed, updates script title and reloads Python script if file exists.
 **********************************************************************************************************************/
void MainWindow::setStateChanged()
{
    QFileInfo fi(m_runFile);
    QString name = fi.absoluteFilePath();
    if (name.isEmpty())
        name = "EMStudio*";
    else
        name = QString("EMStudio (%1*)").arg(name);

    setWindowTitle(name);
}

/*!*******************************************************************************************************************
 * \brief Marks the simulation state as saved, updates script title and reloads Python script if file exists.
 **********************************************************************************************************************/
void MainWindow::setStateSaved()
{
    QFileInfo fi(m_runFile);
    QString name = fi.absoluteFilePath();
    if (name.isEmpty())
        name = "EMStudio";
    else
        name = QString("EMStudio (%1)").arg(name);

    if(QFileInfo().exists(m_ui->txtRunPythonScript->text())) {
        loadPythonScriptToEditor(m_ui->txtRunPythonScript->text());
    }

    setWindowTitle(name);
}

/*!*******************************************************************************************************************
 * \brief Checks whether the current simulation state is marked as changed (i.e., window title contains '*').
 * \return True if the state is changed (unsaved), false otherwise.
 **********************************************************************************************************************/
bool MainWindow::isStateChanged() const
{
    QString title = windowTitle();
    return title.contains('*');
}

/*!*******************************************************************************************************************
 * \brief Draws the substrate based on XML file definition and updates the substrate view widget.
 * \param filePath Path to the XML substrate definition file.
 **********************************************************************************************************************/
void MainWindow::drawSubstrate(const QString &filePath)
{
    if (filePath.isEmpty()) {
        error("Substrate file path is empty", false);
        return;
    }

    QFile file(filePath);
    if (!file.exists()) {
        error(QString("Substrate file not found: %1").arg(filePath), false);
        return;
    }

    Substrate substrate;
    if (!substrate.parseXmlFile(filePath)) {
        error(QString("Failed to parse substrate file: %1").arg(filePath), false);
        return;
    }

    if (m_ui->substrateView) {
        m_ui->substrateView->setSubstrate(substrate);
        m_ui->substrateView->update();
    } else {
        error("SubstrateView is not initialized", false);
    }
}


/*!*******************************************************************************************************************
 * \brief Opens the preferences dialog window for modifying global application settings.
 **********************************************************************************************************************/
void MainWindow::on_actionPrefernces_triggered()
{
    Preferences dlg(m_preferences, this);
    dlg.exec();
}

/*!*******************************************************************************************************************
 * \brief Starts the simulation by dispatching to the selected backend (OpenEMS or Palace).
 **********************************************************************************************************************/
void MainWindow::on_btnRun_clicked()
{
    const QString key = currentSimToolKey();
    if (key.isEmpty()) {
        error("No simulation tool selected/configured.");
        return;
    }

    if (key == "openems") {
        runOpenEMS();
    } else if (key == "palace") {
        runPalace();
    } else {
        error(QString("Unsupported simulation tool: %1").arg(key));
    }
}

/*!*******************************************************************************************************************
 * \brief Returns the stable key of the currently selected sim tool ("openems"/"palace"), or empty if none.
 **********************************************************************************************************************/
QString MainWindow::currentSimToolKey() const
{
    const int idx = m_ui->cbxSimTool->currentIndex();
    if (idx < 0 || !m_ui->cbxSimTool->isEnabled())
        return {};

    return m_ui->cbxSimTool->itemData(idx).toString().trimmed().toLower();
}

/*!*******************************************************************************************************************
 * \brief Runs OpenEMS: writes the Python script, configures env, and launches the Python process.
 **********************************************************************************************************************/
void MainWindow::runOpenEMS()
{
    if (m_simProcess) {
        info("Simulation is already running.", true);
        return;
    }

    on_actionSave_triggered();

    QString pythonPath = m_preferences.value("Python Path").toString();
    if (pythonPath.isEmpty())
        pythonPath = "python";

    QString runDir = m_simSettings.value("RunDir").toString();
    if (runDir.isEmpty() || !QDir(runDir).exists()) {
        error("Run directory not found or not specified.", true);
        return;
    }

    const QString topCell = m_simSettings.value("TopCell").toString();

    const QString scriptPath = QDir(runDir).filePath(topCell + ".py");
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error(QString("Failed to save simulation script to %1").arg(scriptPath), true);
        return;
    }

    QTextStream out(&scriptFile);
    out << m_ui->editRunPythonScript->toPlainText();
    scriptFile.close();

    m_simProcess = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = m_preferences.constBegin(); it != m_preferences.constEnd(); ++it) {
        const QString key = it.key();
        const QString value = it.value().toString();

        if (key == "Python Path") {
            QFileInfo pythonFile(value);
            QString pythonDir = pythonFile.absolutePath();
            QString currentPath = env.value("PATH");

            if (!currentPath.contains(pythonDir, Qt::CaseInsensitive)) {
                env.insert("PATH", pythonDir + QDir::listSeparator() + currentPath);
            }
        } else if (!env.contains(key)) {
            env.insert(key, value);
        }
    }

    if (!env.contains("OPENEMS_INSTALL_PATH") && m_preferences.contains("OPENEMS_INSTALL_PATH")) {
        env.insert("OPENEMS_INSTALL_PATH", m_preferences.value("OPENEMS_INSTALL_PATH").toString());
    }

    const QString origScriptPath = QFileInfo(m_ui->txtRunPythonScript->text()).absolutePath();
#ifdef Q_OS_WIN
    const QString pathSep = ";";
#else
    const QString pathSep = ":";
#endif
    if (env.contains("PYTHONPATH")) {
        env.insert("PYTHONPATH", origScriptPath + pathSep + env.value("PYTHONPATH"));
    } else {
        env.insert("PYTHONPATH", origScriptPath);
    }

    env.remove("PYTHONHOME");

    m_simProcess->setProcessEnvironment(env);
    m_simProcess->setWorkingDirectory(runDir);

    connect(m_simProcess, &QProcess::readyReadStandardOutput, this, [=]() {
        const QByteArray data = m_simProcess->readAllStandardOutput();
        if (!data.isEmpty()) {
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
            m_ui->editSimulationLog->insertPlainText(QString::fromUtf8(data));
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
        }
    });
    connect(m_simProcess, &QProcess::readyReadStandardError, this, [=]() {
        const QByteArray data = m_simProcess->readAllStandardError();
        if (!data.isEmpty()) {
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
            m_ui->editSimulationLog->insertPlainText(QString::fromUtf8(data));
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
        }
    });
    connect(m_simProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int exitCode, QProcess::ExitStatus) {
                const QString msg = QString("\n[Simulation finished with exit code %1]\n").arg(exitCode);
                m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                m_ui->editSimulationLog->insertPlainText(msg);
                m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                m_simProcess->deleteLater();
                m_simProcess = nullptr;
            });

    m_ui->editSimulationLog->clear();
    m_ui->editSimulationLog->insertPlainText("Starting OpenEMS simulation...\n");

    m_simProcess->start(pythonPath, QStringList() << scriptPath);
    if (!m_simProcess->waitForStarted(3000)) {
        error("Failed to start simulation process.", false);
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
    }
}

/*!*******************************************************************************************************************
 * \brief Runs the Palace workflow under WSL in two stages.
 *
 * Stage 1: Executes the Palace Python model script (RunPythonScript) under WSL. The script
 *          is expected to generate a Palace configuration file in the run directory
 *          (RunDir) based on the current simulation settings.
 *
 * Stage 2: After the Python script finishes successfully, searches the run directory
 *          for a Palace config file (currently the newest *.json file) and launches
 *          the Palace binary under WSL with that config.
 *
 * All commands are executed inside WSL using "wsl -d <distro> -- ...". No use is made
 * of m_runFile in this flow; the Palace config is entirely produced by the Python model.
 **********************************************************************************************************************/
void MainWindow::runPalace()
{
    if (m_simProcess) {
        info("Simulation is already running.", true);
        return;
    }

    const QString simKey = currentSimToolKey().toLower();
    if (simKey != QLatin1String("palace")) {
        error("Current simulation tool is not Palace.", true);
        return;
    }

    const QString modelWin = m_simSettings.value("RunPythonScript").toString().trimmed();
    if (modelWin.isEmpty() || !QFileInfo::exists(modelWin)) {
        error("Palace Python model script is not specified or does not exist.", true);
        return;
    }

    QString runDirWin = m_simSettings.value("RunDir").toString().trimmed();
    if (runDirWin.isEmpty()) {
        QFileInfo fi(modelWin);
        const QString baseName = fi.completeBaseName();
        if (baseName.isEmpty()) {
            error("Cannot infer Palace run directory (empty model basename).", true);
            return;
        }

        QDir baseDir(fi.absolutePath());
        runDirWin = baseDir.filePath(QStringLiteral("palace_model/%1_data").arg(baseName));

        m_simSettings["RunDir"] = runDirWin;
        m_ui->txtRunDir->setText(runDirWin);
    }

    QString palaceRoot = m_preferences.value("PALACE_INSTALL_PATH").toString().trimmed();
    if (palaceRoot.isEmpty()) {
        error("PALACE_INSTALL_PATH is not configured in Preferences.", true);
        return;
    }

    const QString distro = m_simSettings.value("WSL_DISTRO", "Ubuntu").toString().trimmed();

    if (!palaceRoot.startsWith('/'))
        palaceRoot = toWslPath(palaceRoot);
    const QString palaceExeLinux = QDir(palaceRoot).filePath("bin/palace");

    const QString modelDirLinux = toWslPath(QFileInfo(modelWin).absolutePath());
    const QString modelLinux    = toWslPath(modelWin);

    QString pythonCmd = m_preferences.value("PALACE_WSL_PYTHON").toString().trimmed();
    if (pythonCmd.isEmpty())
        pythonCmd = QStringLiteral("python3");

    m_palacePythonOutput.clear();

    m_ui->editSimulationLog->clear();
    m_ui->editSimulationLog->insertPlainText(
        QString("Starting Palace Python preprocessing in WSL (%1)...\n").arg(distro));
    m_ui->editSimulationLog->insertPlainText(
        QString("[Using WSL Python: %1]\n").arg(pythonCmd));
    m_ui->editSimulationLog->insertPlainText(
        QString("[Initial Palace run directory guess: %1]\n").arg(runDirWin));

    QStringList argsPython;
    argsPython << "-d" << distro
               << "--"
               << "bash" << "-lc"
               << QString("cd \"%1\" && %2 \"%3\"")
                      .arg(modelDirLinux, pythonCmd, modelLinux);

    m_simProcess  = new QProcess(this);
    m_palacePhase = PalacePhase::PythonModel;

    connect(m_simProcess, &QProcess::readyReadStandardOutput, this, [=]() {
        const QByteArray data = m_simProcess->readAllStandardOutput();
        if (!data.isEmpty()) {
            const QString text = QString::fromUtf8(data);
            m_palacePythonOutput.append(text);
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
            m_ui->editSimulationLog->insertPlainText(text);
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
        }
    });

    connect(m_simProcess, &QProcess::readyReadStandardError, this, [=]() {
        const QByteArray data = m_simProcess->readAllStandardError();
        if (!data.isEmpty()) {
            const QString text = QString::fromUtf8(data);
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
            m_ui->editSimulationLog->insertPlainText(text);
            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
        }
    });

    connect(m_simProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [=](int exitCode, QProcess::ExitStatus) {
                if (m_palacePhase == PalacePhase::PythonModel) {
                    if (exitCode != 0) {
                        const QString msg =
                            QString("\n[Palace Python preprocessing finished with exit code %1]\n")
                                .arg(exitCode);
                        m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                        m_ui->editSimulationLog->insertPlainText(msg);
                        m_ui->editSimulationLog->moveCursor(QTextCursor::End);

                        m_simProcess->deleteLater();
                        m_simProcess  = nullptr;
                        m_palacePhase = PalacePhase::None;
                        return;
                    }

                    QString detectedRunDirWin;
                    {
                        QRegularExpression re(
                            R"(Simulation data directory:\s*([^\s]+))");
                        QRegularExpressionMatch m = re.match(m_palacePythonOutput);
                        if (m.hasMatch()) {
                            const QString simDirLinux = m.captured(1).trimmed();

                            auto wslToWin = [](const QString& p) -> QString {
                                if (p.startsWith("/mnt/") && p.size() > 6) {
                                    const QChar drive = p.at(5).toUpper();
                                    QString rest = p.mid(6); // after "/mnt/x"
                                    QString win = QString("%1:/%2").arg(drive).arg(rest);
                                    return win;
                                }
                                return p;
                            };

                            detectedRunDirWin = wslToWin(simDirLinux);

                            m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                            m_ui->editSimulationLog->insertPlainText(
                                QString("[Detected Palace simulation dir: %1]\n")
                                    .arg(detectedRunDirWin));
                            m_ui->editSimulationLog->moveCursor(QTextCursor::End);

                            if (!detectedRunDirWin.isEmpty()) {
                                m_simSettings["RunDir"] = detectedRunDirWin;
                                m_ui->txtRunDir->setText(detectedRunDirWin);
                            }
                        }
                    }

                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                    m_ui->editSimulationLog->insertPlainText(
                        "\n[Palace Python preprocessing finished successfully, searching for config...]\n");
                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);

                    QString searchDir = detectedRunDirWin.isEmpty()
                                            ? m_simSettings.value("RunDir").toString().trimmed()
                                            : detectedRunDirWin;

                    QDir dir(searchDir);
                    dir.setFilter(QDir::Files | QDir::Readable | QDir::NoSymLinks);
                    dir.setNameFilters(QStringList() << "*.json");
                    dir.setSorting(QDir::Time | QDir::Reversed);
                    const QFileInfoList files = dir.entryInfoList();

                    if (files.isEmpty()) {
                        error(QString("No Palace config (*.json) found in run directory: %1")
                                  .arg(searchDir),
                                  true);
                        m_simProcess->deleteLater();
                        m_simProcess  = nullptr;
                        m_palacePhase = PalacePhase::None;
                        return;
                    }

                    const QString configWin   = files.first().absoluteFilePath();
                    const QString configLinux = toWslPath(configWin);

                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                    m_ui->editSimulationLog->insertPlainText(
                        QString("[Using Palace config: %1]\n").arg(configWin));
                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);

                    const QString configDirLinux  = QFileInfo(configLinux).path();
                    const QString configBaseLinux = QFileInfo(configLinux).fileName();

                    QString palaceRoot2 = m_preferences.value("PALACE_INSTALL_PATH").toString().trimmed();
                    if (!palaceRoot2.startsWith('/'))
                        palaceRoot2 = toWslPath(palaceRoot2);
                    const QString palaceExeLinux2 = QDir(palaceRoot2).filePath("bin/palace");

                    QString cmd = QString(
                                      "cd \"%1\" && "
                                      "\"%2\" --launcher-args --oversubscribe "
                                      "-np $(/usr/bin/nproc 2>/dev/null || nproc 2>/dev/null || echo 1) "
                                      "\"%3\"")
                                      .arg(configDirLinux, palaceExeLinux2, configBaseLinux);

                    QStringList argsPalace;
                    argsPalace << "-d" << m_simSettings.value("WSL_DISTRO", "Ubuntu").toString().trimmed()
                               << "--"
                               << "bash" << "-lc"
                               << cmd;

                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                    m_ui->editSimulationLog->insertPlainText(
                        "\n[Starting Palace solver...]\n");
                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);

                    m_palacePhase = PalacePhase::PalaceSolver;
                    m_simProcess->start("wsl", argsPalace);
                    if (!m_simProcess->waitForStarted(3000)) {
                        error("Failed to start Palace solver under WSL.", false);
                        m_simProcess->deleteLater();
                        m_simProcess  = nullptr;
                        m_palacePhase = PalacePhase::None;
                    }
                    return;
                }

                if (m_palacePhase == PalacePhase::PalaceSolver) {
                    const QString msg =
                        QString("\n[Palace solver finished with exit code %1]\n").arg(exitCode);
                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);
                    m_ui->editSimulationLog->insertPlainText(msg);
                    m_ui->editSimulationLog->moveCursor(QTextCursor::End);

                    m_simProcess->deleteLater();
                    m_simProcess  = nullptr;
                    m_palacePhase = PalacePhase::None;
                }
            });

    m_simProcess->start("wsl", argsPython);
    if (!m_simProcess->waitForStarted(3000)) {
        error("Failed to start Palace Python preprocessing under WSL.", false);
        m_simProcess->deleteLater();
        m_simProcess  = nullptr;
        m_palacePhase = PalacePhase::None;
    }
}

/*!*******************************************************************************************************************
 * \brief Stops the running simulation process if active and cleans up process resources.
 **********************************************************************************************************************/
void MainWindow::on_btnStop_clicked()
{
    if (m_simProcess && m_simProcess->state() == QProcess::Running) {
        m_simProcess->kill();
        m_simProcess->waitForFinished();
        info("Simulation stopped by user.", false);
        m_simProcess->deleteLater();
        m_simProcess = nullptr;
    } else {
        info("No simulation is currently running.", false);
    }
}

/*!*******************************************************************************************************************
 * \brief Sets the GDS file path in the UI and updates related UI state.
 * \param filePath Full path to the GDS file to be shown in the text field.
 **********************************************************************************************************************/
void MainWindow::setGdsFile(const QString &filePath)
{
    m_ui->txtGdsFile->setText(filePath);
    updateGdsUserInfo();
    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Selects the given top cell name in the combo box if it exists.
 * \param cellName Name of the top cell to be selected in the UI combo box.
 **********************************************************************************************************************/
void MainWindow::setTopCell(const QString &cellName)
{
    int index = m_ui->cbxTopCell->findText(cellName);
    if (index >= 0)
        m_ui->cbxTopCell->setCurrentIndex(index);
}

/*!*******************************************************************************************************************
 * \brief Opens a JSON run file and populates UI settings if valid. Remembers last directory.
 **********************************************************************************************************************/
void MainWindow::on_actionOpen_triggered()
{
    QString defaultDir = QDir::homePath();
    if (m_sysSettings.contains("RunFileDir")) {
        const QString d = m_sysSettings.value("RunFileDir").toString();
        if (QDir(d).exists()) defaultDir = d;
    } else if (!m_runFile.isEmpty()) {
        defaultDir = QFileInfo(m_runFile).absolutePath();
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Run File"),
        defaultDir,
        tr("Run Files (*.json);;All Files (*)")
        );
    if (filePath.isEmpty())
        return;

    m_sysSettings["RunFileDir"] = QFileInfo(filePath).absolutePath();

    loadRunFile(filePath);
    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the in-memory mapping between GDS layer numbers and substrate layer names.
 *
 * Reads the substrate XML file currently set in the UI and fills \c m_gdsToSubName
 * (GDS number → layer name). The reverse map \c m_subNameToGds is derived from it.
 * If the substrate file does not exist, the maps are left empty.
 **********************************************************************************************************************/
void MainWindow::rebuildLayerMapping()
{
    m_gdsToSubName.clear();
    m_subNameToGds.clear();

    const QString subXml = m_ui->txtSubstrate->text();
    if (!QFileInfo(subXml).exists()) return;

    m_gdsToSubName = readSubstrateLayerMap(subXml);
    for (auto it = m_gdsToSubName.cbegin(); it != m_gdsToSubName.cend(); ++it)
        m_subNameToGds.insert(it.value(), it.key());
}

/*!*******************************************************************************************************************
 * \brief Enables/disables the "Use Substrate Layer Names" checkbox and applies mapping if needed.
 *
 * The checkbox is enabled only when both a valid GDS file and substrate XML file are present.
 * When enabling, the layer maps are rebuilt. If the checkbox is already checked, the port
 * table is converted to use substrate names immediately.
 **********************************************************************************************************************/
void MainWindow::updateSubLayerNamesCheckboxState()
{
    const bool haveGds = QFileInfo(m_ui->txtGdsFile->text()).exists();
    const bool haveSub = QFileInfo(m_ui->txtSubstrate->text()).exists();
    const bool enable  = haveGds && haveSub;

    m_ui->cbSubLayerNames->setEnabled(enable);

    if (!enable) {
        if (m_ui->cbSubLayerNames->isChecked())
            m_ui->cbSubLayerNames->setChecked(false);
        return;
    }

    rebuildLayerMapping();

    if (m_ui->cbSubLayerNames->isChecked())
        applySubLayerNamesToPorts(true);
}

/*!*******************************************************************************************************************
 * \brief Converts the port table’s layer combo boxes between numeric GDS layers and substrate names.
 *
 * For each row, the Source/From/To layer combo boxes are:
 *  1) Converted in-place to the target representation (\a toNames),
 *  2) Rebuilt with a deterministic, de-duplicated item list according to the mode,
 *  3) Restored to the corresponding (converted) current selection.
 *
 * This function also marks the window state as changed.
 *
 * \param toNames  True to show substrate layer names; false to show numeric GDS layer IDs.
 **********************************************************************************************************************/
void MainWindow::applySubLayerNamesToPorts(bool toNames)
{
    const int rows = m_ui->tblPorts->rowCount();

    for (int r = 0; r < rows; ++r) {
        auto* srcBox  = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(r, 3));
        auto* fromBox = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(r, 4));
        auto* toBox   = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(r, 5));
        if (!fromBox || !toBox) continue;

        auto convert = [&](QComboBox* box) {
            if (!box) return;
            const QString cur = box->currentText().trimmed();

            if (toNames) {
                bool ok=false; const int n = cur.toInt(&ok);
                if (ok && m_gdsToSubName.contains(n)) {
                    const QString nm = m_gdsToSubName.value(n);
                    addUnique(box, nm);
                    box->setCurrentText(nm);
                }
            } else {
                if (!strIsInt(cur) && m_subNameToGds.contains(cur)) {
                    const QString numStr = QString::number(m_subNameToGds.value(cur));
                    addUnique(box, numStr);
                    box->setCurrentText(numStr);
                }
            }
        };

        convert(srcBox);
        convert(fromBox);
        convert(toBox);

        rebuildComboWithMapping(srcBox,  m_gdsToSubName, m_subNameToGds, toNames);
        rebuildComboWithMapping(fromBox, m_gdsToSubName, m_subNameToGds, toNames);
        rebuildComboWithMapping(toBox,   m_gdsToSubName, m_subNameToGds, toNames);
    }

    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Slot: reacts to toggling of the "Use Substrate Layer Names" checkbox.
 *
 * Applies the requested conversion on the port table (names ↔ numbers).
 *
 * \param state  Checkbox state (Qt::Checked / Qt::Unchecked).
 **********************************************************************************************************************/
void MainWindow::on_cbSubLayerNames_stateChanged(int state)
{
    applySubLayerNamesToPorts(state == Qt::Checked);
}

/*!*******************************************************************************************************************
 * \brief Parses simulation_port(...) calls within script (multi-line safe) and extracts key arguments.
 *
 * Supported keyword arguments:
 *  - portnumber=<int>
 *  - voltage=<float>
 *  - port_Z0=<float>
 *  - source_layernum=<int>    or source_layername='<str>' / "<str>"
 *  - from_layername='<str>'   or "<str>"
 *  - to_layername  ='<str>'   or "<str>"
 *  - direction     ='<x|y|z>' or "<x|y|z>"
 *
 * The matcher tolerates arbitrary whitespace and newlines.
 *
 * \param script Full Python script text.
 * \return List of ParsedPort objects in order of appearance.
 **********************************************************************************************************************/
QVector<MainWindow::PortInfo> MainWindow::parsePortsFromScript(const QString& script)
{
    QVector<PortInfo> out;

    QRegularExpression callRe(
        R"(simulation_ports\s*\.\s*add_port\s*\(\s*simulation_setup\s*\.\s*simulation_port\s*\(\s*(.*?)\s*\)\s*\))",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::MultilineOption
    );

    auto it = callRe.globalMatch(script);
    while (it.hasNext()) {
        auto m = it.next();
        const QString args = m.captured(1);

        PortInfo p;

        auto rxInt = [](const QString& key){
            return QRegularExpression(
                QString(R"(%1\s*=\s*([+-]?\d+))").arg(QRegularExpression::escape(key)),
                QRegularExpression::MultilineOption
            );
        };
        auto rxNum = [](const QString& key){
            return QRegularExpression(
                QString(R"(%1\s*=\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))").arg(QRegularExpression::escape(key)),
                QRegularExpression::MultilineOption
            );
        };

        auto rxStr = [](const QString& key){
            return QRegularExpression(
                QString(R"REGEX(%1\s*=\s*(?:'([^']*)'|"([^"]*)"))REGEX")
                    .arg(QRegularExpression::escape(key)),
                QRegularExpression::MultilineOption
                );
        };

        { auto m2 = rxInt("portnumber").match(args); if (m2.hasMatch()) p.portnumber = m2.captured(1).toInt(); }
        { auto m2 = rxNum("voltage").match(args);    if (m2.hasMatch()) p.voltage    = m2.captured(1).toDouble(); }
        { auto m2 = rxNum("port_Z0").match(args);    if (m2.hasMatch()) p.z0         = m2.captured(1).toDouble(); }

        { auto m2 = rxInt("source_layernum").match(args);
          if (m2.hasMatch()) { p.sourceLayer = m2.captured(1); p.sourceIsNumber = true; } }

        if (p.sourceLayer.isEmpty()) {
            auto m2 = rxStr("source_layername").match(args);
            if (m2.hasMatch()) { p.sourceLayer = m2.captured(1).isEmpty() ? m2.captured(2) : m2.captured(1); p.sourceIsNumber = false; }
        }

        { auto m2 = rxStr("from_layername").match(args);
          if (m2.hasMatch()) p.fromLayer = m2.captured(1).isEmpty() ? m2.captured(2) : m2.captured(1); }

        { auto m2 = rxStr("to_layername").match(args);
          if (m2.hasMatch()) p.toLayer = m2.captured(1).isEmpty() ? m2.captured(2) : m2.captured(1); }

        { auto m2 = rxStr("direction").match(args);
          if (m2.hasMatch()) p.direction = m2.captured(1).isEmpty() ? m2.captured(2) : m2.captured(1); }

        if (p.portnumber > 0)
            out.push_back(p);
    }

    return out;
}

/*!*******************************************************************************************************************
 * \brief Appends parsed ports as rows to the Ports table, creating and initializing layer combo boxes.
 *
 * The function fills numeric GDS layers and substrate names similarly to on_btnAddPort(), then applies
 * current name/number display mode via rebuildComboWithMapping(). It also respects defaults if some
 * parsed fields are missing in the script.
 *
 * \param ports Parsed port list to append.
 **********************************************************************************************************************/
void MainWindow::appendParsedPortsToTable(const QVector<PortInfo>& ports)
{
    if (ports.isEmpty())
        return;

    QList<int> gdsNums; gdsNums.reserve(m_layers.size());
    for (const auto& layer : m_layers) gdsNums.push_back(layer.first);
    std::sort(gdsNums.begin(), gdsNums.end());

    QStringList subNames = m_subLayers;
    subNames.removeDuplicates();
    std::sort(subNames.begin(), subNames.end(),
              [](const QString& a, const QString& b){ return QString::localeAwareCompare(a, b) < 0; });

    const bool namesMode = m_ui->cbSubLayerNames->isChecked();

    for (const PortInfo& p : ports) {
        const int row = m_ui->tblPorts->rowCount();
        m_ui->tblPorts->insertRow(row);

        m_ui->tblPorts->setItem(row, 0, new QTableWidgetItem(QString::number(p.portnumber)));
        m_ui->tblPorts->setItem(row, 1, new QTableWidgetItem(QString::number(p.voltage, 'g', 12)));
        m_ui->tblPorts->setItem(row, 2, new QTableWidgetItem(QString::number(p.z0, 'g', 12)));

        auto* sourceLayerBox = new QComboBox();
        auto* fromLayerBox   = new QComboBox();
        auto* toLayerBox     = new QComboBox();
        auto* directionBox   = new QComboBox();

        for (int n : gdsNums) {
            const QString s = QString::number(n);
            sourceLayerBox->addItem(s);
            fromLayerBox->addItem(s);
            toLayerBox->addItem(s);
        }

        for (const QString& nm : subNames) {
            sourceLayerBox->addItem(nm);
            fromLayerBox->addItem(nm);
            toLayerBox->addItem(nm);
        }

        directionBox->addItems(QStringList() << "x" << "y" << "z");
        directionBox->setCurrentText(p.direction.isEmpty() ? "z" : p.direction);

        const QString srcVal = p.sourceLayer.isEmpty()
                                   ? (gdsNums.isEmpty() ? QString() : QString::number(gdsNums.first()))
                                   : p.sourceLayer;
        const QString fromVal = p.fromLayer;
        const QString toVal   = p.toLayer;

        sourceLayerBox->setCurrentText(srcVal);
        if (!fromVal.isEmpty()) fromLayerBox->setCurrentText(fromVal);
        if (!toVal.isEmpty())   toLayerBox->setCurrentText(toVal);

        m_ui->tblPorts->setCellWidget(row, 3, sourceLayerBox);
        m_ui->tblPorts->setCellWidget(row, 4, fromLayerBox);
        m_ui->tblPorts->setCellWidget(row, 5, toLayerBox);
        m_ui->tblPorts->setCellWidget(row, 6, directionBox);

        rebuildComboWithMapping(sourceLayerBox, m_gdsToSubName, m_subNameToGds, namesMode);
        rebuildComboWithMapping(fromLayerBox,   m_gdsToSubName, m_subNameToGds, namesMode);
        rebuildComboWithMapping(toLayerBox,     m_gdsToSubName, m_subNameToGds, namesMode);
    }
}

/*!*******************************************************************************************************************
 * \brief Imports port definitions from the Python editor into the Ports table.
 *
 * Reads the current script from the editor, parses simulation_port(...) calls, and appends the
 * results to the table only when it is empty. Rebuilds the GDS<->substrate layer mapping first.
 * If "Use Substrate Layer Names" is enabled and checked, converts layer combo boxes to names
 * after import.
 *
 * - Returns immediately if the editor is empty.
 * - Returns immediately if the Ports table already has rows.
 * - Calls rebuildLayerMapping() before parsing.
 * - Uses parsePortsFromScript(script) and appendParsedPortsToTable(parsed).
 * - Optionally applies applySubLayerNamesToPorts(true) depending on the checkbox.
 *
 * \sa rebuildLayerMapping(), parsePortsFromScript(), appendParsedPortsToTable(), applySubLayerNamesToPorts()
 **********************************************************************************************************************/
void MainWindow::importPortsFromEditor()
{
    const QString script = m_ui->editRunPythonScript->toPlainText();
    if (script.isEmpty()) return;
    if (m_ui->tblPorts->rowCount() > 0) return;

    rebuildLayerMapping();
    const auto parsed = parsePortsFromScript(script);
    if (!parsed.isEmpty()) {
        appendParsedPortsToTable(parsed);
        if (m_ui->cbSubLayerNames->isEnabled() && m_ui->cbSubLayerNames->isChecked())
            applySubLayerNamesToPorts(true);
    }
}

/*!*******************************************************************************************************************
 * \brief Generates and inserts the default Python simulation script into the editor.
 *
 * If the Python editor already contains code, the user is prompted whether to replace the
 * existing content. On confirmation, the editor is cleared and the default template is inserted.
 * The editor caret is moved to the beginning and the document is marked as modified.
 **********************************************************************************************************************/
void MainWindow::on_btnGenDefaultPython_clicked()
{
    const QString defaultScript = QString::fromUtf8(
        R"PY(import os
import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), 'modules')))

import modules.util_stackup_reader as stackup_reader
import modules.util_gds_reader as gds_reader
import modules.util_utilities as utilities
import modules.util_simulation_setup as simulation_setup
import modules.util_meshlines as util_meshlines

from openEMS import openEMS
import numpy as np


# Model comments
#
# This is a generic model running port excitation for all ports defined below,
# to get full [S] matrix data.
# Output is stored to Touchstone S-parameter file.
# No data plots are created by this script.


# ======================== workflow settings ================================

# preview model/mesh only?
# postprocess existing data without re-running simulation?
preview_only = False
postprocess_only = False

# ===================== input files and path settings =======================

gds_filename = "line_simple_viaport.gds"   # geometries
XML_filename = "SG13G2_nosub.xml"          # stackup

# preprocess GDSII for safe handling of cutouts/holes?
preprocess_gds = False

# merge via polygons with distance less than .. microns, set to 0 to disable via merging.
merge_polygon_size = 0


# get path for this simulation file
script_path = utilities.get_script_path(__file__)
# use script filename as model basename
model_basename = utilities.get_basename(__file__)
# set and create directory for simulation output
sim_path = utilities.create_sim_path (script_path,model_basename)
print('Simulation data directory: ', sim_path)
# change current path to model script path
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# ======================== simulation settings ================================

unit   = 1e-6  # geometry is in microns
margin = 50    # distance in microns from GDSII geometry boundary to simulation boundary

fstart =  0e9
fstop  = 110e9
numfreq = 401

refined_cellsize = 1  # mesh cell size in conductor region

# choices for boundary:
# 'PEC' : perfect electric conductor (default)
# 'PMC' : perfect magnetic conductor, useful for symmetries
# 'MUR' : simple MUR absorbing boundary conditions
# 'PML_8' : PML absorbing boundary conditions
Boundaries = ['PEC', 'PEC', 'PEC', 'PEC', 'PEC', 'PEC']

cells_per_wavelength = 20   # how many mesh cells per wavelength, must be 10 or more
energy_limit = -40          # end criteria for residual energy (dB)

# port configuration, port geometry is read from GDSII file on the specified layer
simulation_ports = simulation_setup.all_simulation_ports()

# in-plane port is specified with target_layername= and direction x or y
# via port is specified with from_layername= and to_layername= and direction z
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=1,
                                                           voltage=1,
                                                           port_Z0=50,
                                                           source_layernum=201,
                                                           from_layername='Metal1',
                                                           to_layername='TopMetal2',
                                                           direction='z'))

simulation_ports.add_port(simulation_setup.simulation_port(portnumber=2,
                                                           voltage=1,
                                                           port_Z0=50,
                                                           source_layernum=202,
                                                           from_layername='Metal1',
                                                           to_layername='TopMetal2',
                                                           direction='z'))

# ======================== simulation ================================

# get technology stackup data
materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate (XML_filename)
# get list of layers from technology
layernumbers = metals_list.getlayernumbers()
# we must also read the layers where we added ports, these are not included in technology layers
layernumbers.extend(simulation_ports.portlayers)

# read geometries from GDSII, only purpose 0
allpolygons = gds_reader.read_gds(gds_filename,
                                  layernumbers,
                                  purposelist=[0],
                                  metals_list=metals_list,
                                  preprocess=preprocess_gds,
                                  merge_polygon_size=merge_polygon_size)

# calculate maximum cellsize from wavelength in dielectric
wavelength_air = 3e8/fstop / unit
max_cellsize = (wavelength_air)/(np.sqrt(materials_list.eps_max)*cells_per_wavelength)

# define excitation and stop criteria and boundaries
FDTD = openEMS(EndCriteria=np.exp(energy_limit/10 * np.log(10)))
FDTD.SetGaussExcite( (fstart+fstop)/2, (fstop-fstart)/2 )
FDTD.SetBoundaryCond( Boundaries )


########### create model, run and post-process ###########

# run all port excitations, one after another

for port in simulation_ports.ports:
    simulation_setup.setupSimulation   ([port.portnumber],
                                        simulation_ports,
                                        FDTD,
                                        materials_list,
                                        dielectrics_list,
                                        metals_list,
                                        allpolygons,
                                        max_cellsize,
                                        refined_cellsize,
                                        margin,
                                        unit,
                                        xy_mesh_function=util_meshlines.create_xy_mesh_from_polygons)

    simulation_setup.runSimulation  ([port.portnumber],
                                        FDTD,
                                        sim_path,
                                        model_basename,
                                        preview_only,
                                        postprocess_only)


# Initialize an empty matrix for S-parameters
num_ports = simulation_ports.portcount
s_params = np.empty((num_ports, num_ports, numfreq), dtype=object)

# Define frequency resolution. Due to FFT from Empire time domain results,
# this is postprocessing and we can change it again at any time.
f = np.linspace(fstart,fstop,numfreq)

# Populate the S-parameter matrix with simulation results
for i in range(1, num_ports + 1):
    for j in range(1, num_ports + 1):
        s_params[i-1, j-1] = utilities.calculate_Sij(i, j, f, sim_path, simulation_ports)

# Write to Touchstone *.snp file
snp_name = os.path.join(sim_path, model_basename + '.s' + str(num_ports) + 'p')
utilities.write_snp(s_params, f, snp_name)

print('Created S-parameter output file at ', snp_name)
)PY");

    const bool hasExisting = !m_ui->editRunPythonScript->toPlainText().trimmed().isEmpty();
    if (hasExisting) {
        const auto ret = QMessageBox::question(
            this,
            tr("Replace Existing Script"),
            tr("The Python script editor already contains code.\n\n"
               "Do you want to replace it with the default template?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
            );
        if (ret != QMessageBox::Yes)
            return;
    }

    m_ui->editRunPythonScript->clear();
    m_ui->editRunPythonScript->setPlainText(defaultScript);
    m_ui->editRunPythonScript->moveCursor(QTextCursor::Start);

    m_ui->editRunPythonScript->document()->setModified(true);

    importPortsFromEditor();
    updateSubLayerNamesAutoCheck();

    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Connects a QComboBox within the Ports table to mark the simulation state as changed when edited.
 *
 * This helper function ensures that any modification of a port’s layer or direction selection
 * automatically updates the application state. It connects both the index-change and text-change
 * signals of the combo box to MainWindow::setStateChanged(), while respecting the
 * m_blockPortChanges guard to prevent unwanted triggers during table initialization.
 *
 * \param box Pointer to the QComboBox that should be monitored for user edits.
 **********************************************************************************************************************/
void MainWindow::hookPortCombo(QComboBox* box)
{
    if (!box) return;
    connect(box, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int){ if (!m_blockPortChanges) setStateChanged(); });
    connect(box, &QComboBox::editTextChanged, this,
            [this](const QString&){ if (!m_blockPortChanges) setStateChanged(); });
}


/*!*******************************************************************************************************************
 * \brief Handles selection changes in the "Simulation Tool" combo box.
 *
 * Persists the selected index into \c m_simSettings under "SIMULATION_TOOL_INDEX".
 * Also stores a stable key identifier ("openems"/"palace") as "SIMULATION_TOOL_KEY"
 * for robust restoration even if item order changes. Placeholder selections are ignored.
 *
 * \param index Newly selected index in \c cbxSimTool.
 **********************************************************************************************************************/
void MainWindow::on_cbxSimTool_currentIndexChanged(int index)
{
    if (index < 0 || !m_ui->cbxSimTool->isEnabled())
        return;

    const QString key  = m_ui->cbxSimTool->itemData(index).toString();
    if (key.isEmpty())
        return;

    m_preferences["SIMULATION_TOOL_INDEX"] = index;
    m_preferences["SIMULATION_TOOL_KEY"]   = key.toLower();;
}


/*!*******************************************************************************************************************
 * \brief Opens a Palace Python model file and parses its settings.
 *
 * Shows a file dialog, remembers the last used directory in \c m_preferences,
 * calls PalacePythonParser and (optionally) updates internal simulation settings.
 **********************************************************************************************************************/
void MainWindow::on_actionOpen_Python_Model_triggered()
{
    const QString lastDir  = m_preferences.value("PALACE_MODEL_DIR").toString();
    const QString startDir = lastDir.isEmpty() ? QDir::homePath() : lastDir;

    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open Palace Python model"),
        startDir,
        tr("Python files (*.py);;All files (*.*)"));

    if (fileName.isEmpty())
        return;

    m_ui->editSimulationLog->clear();

    const QFileInfo fi(fileName);
    m_preferences["PALACE_MODEL_DIR"]  = fi.absolutePath();
    m_preferences["PALACE_MODEL_FILE"] = fileName;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error(QString("Cannot open file %1").arg(fileName));
        return;
    }

    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    auto detectModelType = [](const QString& text) {
        if (text.contains("from openEMS import openEMS"))
            return QStringLiteral("openems");

        QRegularExpression re(R"(\w+\s*\[\s*['"][^'"]+['"]\s*\]\s*=)");
        if (re.match(text).hasMatch())
            return QStringLiteral("palace");

        return QStringLiteral("unknown");
    };

    const QString modelType = detectModelType(text);

    PythonParser::Result res = PythonParser::parseSettings(fileName);
    if (!res.ok)
    {
        error(tr("Failed to parse Palace model file:\n%1").arg(res.error));
        return;
    }

    QString simKey;
    if (modelType == QLatin1String("openems"))
        simKey = QStringLiteral("openems");
    else if (modelType == QLatin1String("palace"))
        simKey = QStringLiteral("palace");
    else
        simKey = QStringLiteral("palace");

    int idxSim = m_ui->cbxSimTool->findData(simKey);
    if (idxSim >= 0 && m_ui->cbxSimTool->isEnabled())
        m_ui->cbxSimTool->setCurrentIndex(idxSim);

    rebuildSimulationSettingsFromPalace(res.settings);

    const QDir modelDir(fi.absolutePath());

    if (!res.gdsFilename.isEmpty())
    {
        QString gdsPath = res.gdsFilename;
        if (QFileInfo(gdsPath).isRelative())
            gdsPath = modelDir.filePath(gdsPath);

        m_ui->txtGdsFile->setText(gdsPath);
        m_simSettings["GdsFile"] = gdsPath;
        m_sysSettings["GdsDir"]  = QFileInfo(gdsPath).absolutePath();
    }

    if (!res.xmlFilename.isEmpty())
    {
        QString subPath = res.xmlFilename;
        if (QFileInfo(subPath).isRelative())
            subPath = modelDir.filePath(subPath);

        m_ui->txtSubstrate->setText(subPath);
        m_simSettings["SubstrateFile"] = subPath;
        m_sysSettings["SubstrateDir"]  = QFileInfo(subPath).absolutePath();
    }

    updateSubLayerNamesCheckboxState();

    QFile scriptFile(fileName);
    if (scriptFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString script = QString::fromUtf8(scriptFile.readAll());
        scriptFile.close();

        m_ui->editRunPythonScript->setPlainText(script);
        m_ui->editRunPythonScript->document()->setModified(false);

        m_ui->txtRunPythonScript->setText(fileName);
        m_simSettings["RunPythonScript"] = fileName;

        m_ui->tblPorts->setRowCount(0);
        importPortsFromEditor();
        updateSubLayerNamesAutoCheck();
    }

    if (!res.simPath.isEmpty())
    {
        QString runDir = res.simPath;
        if (QFileInfo(runDir).isRelative())
            runDir = modelDir.filePath(runDir);

        QDir().mkpath(runDir);
        m_ui->txtRunDir->setText(runDir);
        m_simSettings["RunDir"] = runDir;
    }

    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the "Simulation Settings" property group using values parsed from a Palace Python model.
 *
 * This method is called when a Palace model file is opened and parsed successfully.
 * All existing OpenEMS-related properties within the "Simulation Settings" group are removed,
 * and new properties are created dynamically for each key–value pair found in the parsed data.
 *
 * The property type (bool, int, double, string) is inferred from the QVariant type of each value.
 * This allows arbitrary Palace configuration keys to be displayed and edited directly
 * in the QtPropertyBrowser without predefined structure.
 *
 * \param settings  Map of key–value pairs extracted from the Palace Python model.
 **********************************************************************************************************************/
void MainWindow::rebuildSimulationSettingsFromPalace(const QMap<QString, QVariant>& settings)
{
    if (!m_simSettingsGroup || !m_variantManager)
        return;

    {
        const auto children = m_simSettingsGroup->subProperties();
        for (QtProperty* child : children) {
            delete child;
        }
    }

    if (settings.contains(QLatin1String("Boundaries"))) {
        const QVariant v = settings.value(QLatin1String("Boundaries"));

        QStringList items;
        const QStringList sides{ "X-", "X+", "Y-", "Y+", "Z-", "Z+" };

        if (v.type() == QVariant::String) {
            QString expr = v.toString().trimmed();
            if (expr.startsWith('[') && expr.endsWith(']'))
                expr = expr.mid(1, expr.size() - 2);

            QRegularExpression itemRe(R"(['"]([^'"]+)['"])");
            auto itItems = itemRe.globalMatch(expr);
            while (itItems.hasNext()) {
                QRegularExpressionMatch m = itItems.next();
                items << m.captured(1).trimmed();
            }
        }
        else if (v.type() == QVariant::Map) {
            const QVariantMap m = v.toMap();
            for (const QString& s : sides)
                items << m.value(s, QStringLiteral("PEC")).toString();
        }

        if (items.size() == 6) {
            QtProperty* boundariesGroup = nullptr;
            const auto topProps = m_propertyBrowser->properties();
            for (QtProperty* top : topProps) {
                if (top->propertyName() == QLatin1String("Boundaries")) {
                    boundariesGroup = top;
                    break;
                }
            }

            if (boundariesGroup) {
                const auto subProps = boundariesGroup->subProperties();
                for (QtProperty* sub : subProps) {
                    const QString sideName = sub->propertyName();
                    int idx = sides.indexOf(sideName);
                    if (idx < 0 || idx >= items.size())
                        continue;

                    const QString valueStr = items.at(idx);
                    const QStringList enumNames =
                        m_variantManager->attributeValue(sub, "enumNames").toStringList();
                    int enumIndex = enumNames.indexOf(valueStr);
                    if (enumIndex >= 0)
                        m_variantManager->setValue(sub, enumIndex);
                }
            }

            QVariantMap bndMap;
            for (int i = 0; i < items.size() && i < sides.size(); ++i)
                bndMap[sides.at(i)] = items.at(i);
            m_simSettings["Boundaries"] = bndMap;
        }
    }

    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        const QString& key = it.key();
        const QVariant& val = it.value();

        if (key == QLatin1String("Boundaries"))
            continue;

        QVariant::Type t = val.type();
        int propType = QVariant::String;
        QVariant finalValue = val;

        switch (t) {
        case QVariant::Bool:
            propType   = QVariant::Bool;
            finalValue = val.toBool();
            break;
        case QVariant::Int:
        case QVariant::LongLong:
        case QVariant::UInt:
        case QVariant::ULongLong:
            propType   = QVariant::Int;
            finalValue = val.toInt();
            break;
        case QVariant::Double:
            propType   = QVariant::Double;
            finalValue = val.toDouble();
            break;
        default:
            propType   = QVariant::String;
            finalValue = val.toString();
            break;
        }

        QtVariantProperty* prop = m_variantManager->addProperty(propType, key);
        if (!prop)
            continue;

        if (propType == QVariant::Double) {
            prop->setAttribute(QLatin1String("decimals"), 12);
            prop->setAttribute(QLatin1String("minimum"),
                               -std::numeric_limits<double>::max());
            prop->setAttribute(QLatin1String("maximum"),
                               std::numeric_limits<double>::max());
            prop->setAttribute(QLatin1String("singleStep"), 0.0);
        }

        prop->setValue(finalValue);
        m_simSettingsGroup->addSubProperty(prop);
    }
}

/*!*******************************************************************************************************************
 * \brief Saves the current Python script, re-parses it and updates the simulation setup.
 *
 * Writes the contents of the embedded Python editor to the file referenced by
 * txtRunPythonScript, then runs PythonParser on that file and rebuilds the
 * simulation-related GUI state (simulation settings, GDS/substrate paths,
 * run directory and internal m_simSettings entries) from the parsed result.
 *
 * The editor document is marked unmodified on success so that further tab
 * changes do not trigger an unnecessary apply prompt.
 *
 * \return true on successful save and parse, false otherwise.
 **********************************************************************************************************************/
bool MainWindow::applyPythonScriptFromEditor()
{
    const QString filePath = m_ui->txtRunPythonScript->text().trimmed();
    if (filePath.isEmpty()) {
        error(tr("No Python script file specified."), false);
        return false;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error(tr("Failed to save Python script:\n%1").arg(f.errorString()), false);
        return false;
    }

    QTextStream out(&f);
    out << m_ui->editRunPythonScript->toPlainText();
    f.close();

    PythonParser::Result res = PythonParser::parseSettings(filePath);
    if (!res.ok) {
        error(tr("Failed to parse Python model file:\n%1").arg(res.error), false);
        return false;
    }

    rebuildSimulationSettingsFromPalace(res.settings);

    QFileInfo fi(filePath);
    const QDir modelDir(fi.absolutePath());

    if (!res.gdsFilename.isEmpty()) {
        QString gdsPath = res.gdsFilename;
        if (QFileInfo(gdsPath).isRelative())
            gdsPath = modelDir.filePath(gdsPath);

        m_ui->txtGdsFile->setText(gdsPath);
        m_simSettings["GdsFile"] = gdsPath;
        m_sysSettings["GdsDir"]  = QFileInfo(gdsPath).absolutePath();
    }

    if (!res.xmlFilename.isEmpty()) {
        QString subPath = res.xmlFilename;
        if (QFileInfo(subPath).isRelative())
            subPath = modelDir.filePath(subPath);

        m_ui->txtSubstrate->setText(subPath);
        m_simSettings["SubstrateFile"] = subPath;
        m_sysSettings["SubstrateDir"]  = QFileInfo(subPath).absolutePath();
    }

    if (!res.simPath.isEmpty()) {
        QString runDir = res.simPath;
        if (QFileInfo(runDir).isRelative())
            runDir = modelDir.filePath(runDir);

        QDir().mkpath(runDir);
        m_ui->txtRunDir->setText(runDir);
        m_simSettings["RunDir"] = runDir;
    }

    m_simSettings["RunPythonScript"] = filePath;
    m_ui->editRunPythonScript->document()->setModified(false);

    return true;
}

/*!*******************************************************************************************************************
 * \brief Automatically enables the "SubLayer Names" option when substrate and ports are available.
 *
 * This helper checks whether a substrate file is loaded and at least one port
 * is defined in the ports table. If both conditions are met, the checkbox
 * controlling the use of substrate layer names (cbSubLayerNames) is enabled
 * automatically. This improves workflow by ensuring correct layer-name mapping
 * without requiring manual user action.
 **********************************************************************************************************************/
void MainWindow::updateSubLayerNamesAutoCheck()
{
    const bool hasSubstrate = !m_ui->txtSubstrate->text().trimmed().isEmpty();
    const bool hasPorts     = (m_ui->tblPorts->rowCount() > 0);

    if (hasSubstrate && hasPorts)
        m_ui->cbSubLayerNames->setChecked(true);
}
