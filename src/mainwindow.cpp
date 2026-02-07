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

#include <QMenu>
#include <QFile>
#include <QDebug>
#include <QTimer>
#include <QAction>
#include <QProcess>
#include <QFileInfo>
#include <QSettings>
#include <QJsonArray>
#include <QScrollBar>
#include <QJsonValue>
#include <QFileDialog>
#include <QTextStream>
#include <QJsonObject>
#include <QMessageBox>
#include <QCloseEvent>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QStandardPaths>
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
#include "keywordseditor.h"

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

    setupTabMapping();
    showTab(m_tabMap.value("Main", 0));

    loadSettings();
    initRecentMenu();
    setupSettingsPanel();

    connect(m_ui->editRunPythonScript, &PythonEditor::sigFontSizeChanged,
            this, [=](qreal newSize){
                m_sysSettings["PYTHON_EDITOR_FONT_SIZE"] = newSize;
            });

    if (m_sysSettings.contains("PYTHON_EDITOR_FONT_SIZE")) {
        qreal size = m_sysSettings["PYTHON_EDITOR_FONT_SIZE"].toDouble();
        if (size > 4.0 && size < 80.0)
            m_ui->editRunPythonScript->setEditorFontSize(size);
    }

    updateSubLayerNamesCheckboxState();

    refreshSimToolOptions();

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_ui->editSimulationLog->setFont(mono);

    //hide python code button and text line
    m_ui->lblRunPythonScript->setVisible(false);
    m_ui->btnRunPythonScript->setVisible(false);
    m_ui->txtRunPythonScript->setVisible(false);

    setupWindowMenuDocks();

    refreshKeywordTipsForCurrentTool();

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
 * \brief Connects Window menu actions with dock widgets and keeps their visibility in sync.
 *
 * Binds checkable actions from the "Window" menu to their corresponding QDockWidget
 * instances (Run Control and Log). The action state reflects the current dock visibility,
 * and toggling the action shows or hides the dock. Closing a dock via its title bar
 * button also updates the associated menu action.
 *
 * This ensures that dock widgets can always be restored after being closed.
 **********************************************************************************************************************/
void MainWindow::setupWindowMenuDocks()
{
    auto bind = [](QAction* act, QDockWidget* dock)
    {
        if (!act || !dock) return;

        act->setChecked(dock->isVisible());
        QObject::connect(act, &QAction::toggled,
                         dock, &QDockWidget::setVisible);
        QObject::connect(dock, &QDockWidget::visibilityChanged,
                         act, &QAction::setChecked);
    };

    bind(m_ui->actionRun_Control, m_ui->dockRunControl);
    bind(m_ui->actionLog,         m_ui->dockLog);
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the "Simulation Tool" combo box (cbxSimTool) based on configured install paths.
 *
 * Reads PALACE_INSTALL_PATH from \c m_preferences, validates them with
 * pathLooksValid(), and repopulates \c cbxSimTool with the available tools ("OpenEMS", "Palace").
 * If none are valid, a placeholder item is shown and the combo is disabled. Emits an info() message
 * summarizing what is enabled.
 **********************************************************************************************************************/
void MainWindow::refreshSimToolOptions()
{
    QSignalBlocker blocker(m_ui->cbxSimTool);

    const QString openemsPath     = m_preferences.value("Python Path").toString();
    const QString palacePath      = m_preferences.value("PALACE_INSTALL_PATH").toString();
    const QString palaceScriptPath = m_preferences.value("PALACE_RUN_SCRIPT").toString();

    const bool hasOpenEMS = QFileInfo(openemsPath).isExecutable();

    const bool hasPalaceInstall = pathLooksValid(palacePath, "bin/palace");
    const bool hasPalaceScript  = fileLooksValid(palaceScriptPath);

    const bool hasPalace = hasPalaceInstall || hasPalaceScript;

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
        info("No valid simulation tools found. Set OpenEMS Paython path and/or PALACE_INSTALL_PATH / PALACE_SCRIPT_PATH in Preferences.");
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
 * \brief Validates that a given path points to an existing regular file.
 *
 * Checks that \p path is non-empty, exists on the local filesystem, and refers to a regular file.
 * This is intended for validating script or executable file paths that must be directly accessible
 * from the current process (e.g. launcher scripts).
 *
 * \param path The file path to validate.
 * \return \c true if the path exists and refers to a regular file, otherwise \c false.
 **********************************************************************************************************************/
bool MainWindow::fileLooksValid(const QString &path) const
{
    QFileInfo fi(path.trimmed());
    return fi.exists() && fi.isExecutable();
}


/*!*******************************************************************************************************************
 * \brief Converts a Windows path (e.g. "C:\foo\bar") to a WSL path ("/mnt/c/foo/bar").
 *        If the path already looks Linux-like, returns it unchanged.
 **********************************************************************************************************************/
QString MainWindow::toWslPath(const QString &winPath) const
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
 * \brief Converts a WSL-style Linux path to a native host path.
 *
 * On Windows, paths inside Python models may use WSL syntax such as
 * "/mnt/c/Users/...". This helper converts such paths back to Windows-native
 * form ("C:/Users/...") so the GUI and simulation settings operate on host
 * filesystem paths. On non-Windows systems, the input string is returned
 * unchanged.
 *
 * \param path The path string to convert.
 * \return A host-native filesystem path. On Windows, "/mnt/<drive>/<rest>"
 *         is mapped to "<DRIVE>:/<rest>". On other platforms, the original
 *         string is returned.
 **********************************************************************************************************************/
QString MainWindow::fromWslPath(const QString &path) const
{
#ifdef Q_OS_WIN
    QRegularExpression re(R"(^/mnt/([a-zA-Z])/(.*)$)");
    auto m = re.match(path.trimmed());
    if (!m.hasMatch())
        return path; // not a WSL path, return as-is

    const QString drive = m.captured(1).toUpper();
    QString rest = m.captured(2);

    QString winPath = drive + ":\\" + rest;
    return QDir::toNativeSeparators(winPath);
#else
    Q_UNUSED(path);
    return path;
#endif
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
            tr("The python script has been modified. Do you want to save your changes?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
            );

        if (reply == QMessageBox::Yes) {
            if (!applyPythonScriptFromEditor()) {
                event->ignore();
                return;
            }
            setStateSaved();
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
                return;

            saveSettings();
            setStateSaved();
            m_ui->editRunPythonScript->document()->setModified(false);
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
            msg.addButton(tr("&Ignore"), QMessageBox::RejectRole);
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

    /*auto addDouble = [&](const QString& name, double value) {
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
    addDouble("refined_cellsize", 1.0); */

    const QString simTool = m_preferences.value("SIMULATION_TOOL_KEY", QLatin1String("OpenEMS")).toString();

    QStringList boundaryOptions;

    if (simTool.compare(QLatin1String("OpenEMS"), Qt::CaseInsensitive) == 0) {
        boundaryOptions.clear();
        boundaryOptions
            << QStringLiteral("PEC")
            << QStringLiteral("PMC")
            << QStringLiteral("MUR")
            << QStringLiteral("PML_8");
    } else if (simTool.compare(QLatin1String("Palace"), Qt::CaseInsensitive) == 0) {
        boundaryOptions.clear();
        boundaryOptions
            << QStringLiteral("PEC")
            << QStringLiteral("PMC")
            << QStringLiteral("Absorbing")
            << QStringLiteral("Impedance")
            << QStringLiteral("Conductivity");
    } else {
        boundaryOptions.clear();
        boundaryOptions
            << QStringLiteral("PEC")
            << QStringLiteral("PMC");
    }

    QtVariantProperty *boundariesGroup =
        m_variantManager->addProperty(QtVariantPropertyManager::groupTypeId(), QLatin1String("Boundaries"));

    QStringList boundaryNames    = { QStringLiteral("X-"), QStringLiteral("X+"),
                                 QStringLiteral("Y-"), QStringLiteral("Y+"),
                                 QStringLiteral("Z-"), QStringLiteral("Z+") };
    QStringList boundaryDefaults = { QStringLiteral("PEC"), QStringLiteral("PEC"),
                                    QStringLiteral("PEC"), QStringLiteral("PEC"),
                                    QStringLiteral("PEC"), QStringLiteral("PEC") };

    for (int i = 0; i < boundaryNames.size(); ++i) {
        QtVariantProperty *bndProp =
            m_variantManager->addProperty(QtVariantPropertyManager::enumTypeId(), boundaryNames.at(i));
        bndProp->setAttribute(QStringLiteral("enumNames"), boundaryOptions);
        bndProp->setValue(boundaryOptions.indexOf(boundaryDefaults.at(i)));
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
 * for the current Python model. The chosen path is stored in
 * txtRunPythonScript and PALACE_MODEL_FILE, and then the model is saved
 * via applyPythonScriptFromEditor().
 **********************************************************************************************************************/
void MainWindow::on_actionSave_As_triggered()
{
    if (!ensurePythonScriptPathBySaveAs(true))
        return;

    on_actionSave_triggered();
}

/*!*******************************************************************************************************************
 * \brief Triggered when the "Save" action is invoked.
 *
 * Behaviour depends on where Ctrl+S was pressed:
 *
 * - If the focus is inside the Python editor (editRunPythonScript or any of its children),
 *   the Python script is considered the source of truth:
 *      Python editor  -> parsed into m_simSettings / GUI.
 *
 * - If the focus is outside the Python editor (GUI widgets, tables, line edits, etc.),
 *   the GUI is considered the source of truth for the script:
 *      GUI state      -> regenerated Python script (if state changed and editor is not modified)
 *                     -> parsed back into m_simSettings.
 *
 * In both cases, after a successful applyPythonScriptFromEditor(), settings are saved and
 * the window is marked as "saved" (no '*').
 **********************************************************************************************************************/
void MainWindow::on_actionSave_triggered()
{
    bool pythonEditorActive = false;
    if (QWidget *fw = QApplication::focusWidget()) {
        pythonEditorActive =
            (fw == m_ui->editRunPythonScript) ||
            m_ui->editRunPythonScript->isAncestorOf(fw);
    }

    if (!pythonEditorActive) {
        const QString scriptPath = m_ui->txtRunPythonScript->text().trimmed();
        if (!scriptPath.isEmpty() && QFileInfo(scriptPath).exists()) {
            loadPythonScriptToEditor(scriptPath);
        }
    }

    if (!applyPythonScriptFromEditor())
        return;

    saveSettings();
    setStateSaved();

    info("All changes saved successfully.", true);
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

        m_ui->tblPorts->setRowCount(0);

        rebuildLayerMapping();

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

        const bool namesMode = m_ui->cbSubLayerNames->isChecked();

        const QVariant portsVar = m_simSettings["Ports"];
        if (portsVar.canConvert<QVariantList>()) {
            const QVariantList portsList = portsVar.toList();

            auto setCurrentSafe = [](QComboBox* box, const QString& value){
                if (!box || value.isEmpty()) return;
                QSignalBlocker blocker(box);
                if (box->findText(value) < 0)
                    box->addItem(value);
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

                sourceLayerBox->addItem(QString());
                fromLayerBox->addItem(QString());
                toLayerBox->addItem(QString());

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

                directionBox->addItems(QStringList() << "x" << "y" << "z" << "-x" << "-y" << "-z");

                const QString src  = portMap.value("Source Layer").toString().trimmed();
                const QString from = portMap.value("From Layer").toString().trimmed();
                const QString to   = portMap.value("To Layer").toString().trimmed();
                QString dir        = portMap.value("Direction").toString().trimmed();
                if (dir.isEmpty()) dir = "z";

                dir = dir.toLower();

                setCurrentSafe(sourceLayerBox, src);
                setCurrentSafe(fromLayerBox,   from);
                setCurrentSafe(toLayerBox,     to);
                {
                    QSignalBlocker b(directionBox);
                    directionBox->setCurrentText(dir);
                }

                // Place widgets
                m_ui->tblPorts->setCellWidget(row, 3, sourceLayerBox);
                m_ui->tblPorts->setCellWidget(row, 4, fromLayerBox);
                m_ui->tblPorts->setCellWidget(row, 5, toLayerBox);
                m_ui->tblPorts->setCellWidget(row, 6, directionBox);

                // Rebuild combos with mapping
                rebuildComboWithMapping(sourceLayerBox, m_gdsToSubName, m_subNameToGds, namesMode);
                rebuildComboWithMapping(fromLayerBox,   m_gdsToSubName, m_subNameToGds, namesMode);
                rebuildComboWithMapping(toLayerBox,     m_gdsToSubName, m_subNameToGds, namesMode);

                // Hooks
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

    sourceLayerBox->addItem(QString());
    fromLayerBox->addItem(QString());
    toLayerBox->addItem(QString());

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

    directionBox->addItems(QStringList() << "x" << "y" << "z" << "-x" << "-y" << "-z");
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

    const QFileInfo fi(arg1);
    if (fi.exists()) {
        m_simSettings["SubstrateFile"] = arg1;
        m_sysSettings["SubstrateDir"]  = fi.absolutePath();

        m_subLayers = readSubstrateLayers(arg1);

        drawSubstrate(arg1);
    }

    setStateChanged();
    updateSubLayerNamesCheckboxState();
}


/*!*******************************************************************************************************************
 * \brief Marks the simulation state as changed, updates script title and reloads Python script if file exists.
 **********************************************************************************************************************/
void MainWindow::setStateChanged()
{
    QFileInfo fi(m_ui->txtRunPythonScript->text());
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
    QFileInfo fi(m_ui->txtRunPythonScript->text());
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
    const QString comboKey = currentSimToolKey();

    const QString scriptText = m_ui->editRunPythonScript->toPlainText();

    QString modelType = QStringLiteral("unknown");
    if (!scriptText.trimmed().isEmpty()) {
        auto detectModelType = [](const QString& text) -> QString {
            if (text.contains(QStringLiteral("from openEMS import openEMS")))
                return QStringLiteral("openems");

            QRegularExpression re(R"(\w+\s*\[\s*['"][^'"]+['"]\s*\]\s*=)");
            if (re.match(text).hasMatch())
                return QStringLiteral("palace");

            return QStringLiteral("unknown");
        };

        modelType = detectModelType(scriptText);
    }

    QString key;
    if (modelType == QLatin1String("openems") ||
        modelType == QLatin1String("palace"))
    {
        key = modelType;
    } else {
        key = comboKey;
    }

    if (key.isEmpty()) {
        error("No simulation tool selected/configured.");
        return;
    }

    m_ui->txtLog->clear();

    if (key == QLatin1String("openems")) {
        runOpenEMS();
    } else if (key == QLatin1String("palace")) {
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
 * \brief Stops the running simulation process if active and cleans up process resources.
 **********************************************************************************************************************/
void MainWindow::on_btnStop_clicked()
{
    if (!m_simProcess || m_simProcess->state() != QProcess::Running) {
        info("No simulation is currently running.", false);
        return;
    }

    m_palacePhase = PalacePhase::None;

    info("Stopping simulation...", false);

    m_simProcess->terminate();

    QProcess *p = m_simProcess;
    QTimer::singleShot(1500, this, [p]() {
        if (p && p->state() != QProcess::NotRunning)
            p->kill();
    });
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
 * \brief Sets the GDS file path in the UI and updates related UI state.
 * \param filePath Full path to the GDS file to be shown in the text field.
 **********************************************************************************************************************/
void MainWindow::setSubstrateFile(const QString &filePath)
{
    m_ui->txtSubstrate->setText(filePath);
    updateSubLayerNamesCheckboxState();
    setStateChanged();
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

        if (p.fromLayer.isEmpty() && p.toLayer.isEmpty()) {
            auto m2 = rxStr("target_layername").match(args);
            if (m2.hasMatch()) {
                const QString t = m2.captured(1).isEmpty() ? m2.captured(2) : m2.captured(1);
                p.toLayer = t;
            }
        }

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

        sourceLayerBox->addItem(QString());
        fromLayerBox->addItem(QString());
        toLayerBox->addItem(QString());

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

        directionBox->addItems(QStringList() << "x" << "y" << "z" << "-x" << "-y" << "-z");

        QString dir = p.direction.toLower();
        directionBox->setCurrentText(p.direction.isEmpty() ? "z" : dir);

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
    const QString simKey = currentSimToolKey().toLower();

    QString defaultScript;
    if (simKey == QLatin1String("openems")) {
        defaultScript = createDefaultOpenemsScript();
    } else if (simKey == QLatin1String("palace")) {
        defaultScript = createDefaultPalaceScript();
    } else {
        // Fallback: you can choose one, or warn.
        defaultScript = createDefaultOpenemsScript();
    }

    if (defaultScript.isEmpty())
        return;

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
 * \brief Reads a UTF-8 encoded text file into a string.
 *
 * Opens the specified file in text mode, reads its full contents using UTF-8 encoding,
 * and returns the data via the output parameter. In case of failure, an error message
 * is reported and the function returns \c false.
 *
 * \param fileName Path to the text file to be read.
 * \param outText  Output string receiving the file contents.
 *
 * \return \c true on success, \c false if the file could not be opened or read.
 **********************************************************************************************************************/
bool MainWindow::readTextFileUtf8(const QString &fileName, QString &outText)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error(QString("Cannot open file:\n%1\n\nReason: %2").arg(fileName, file.errorString()));
        return false;
    }

    QTextStream ts(&file);
    ts.setCodec("UTF-8");
    outText = ts.readAll();
    return true;
}

/*!*******************************************************************************************************************
 * \brief Resolves the absolute path to a Python model template file.
 *
 * Resolution order:
 *  1) Preferences["MODEL_TEMPLATES_DIR"] (if set and contains the requested file)
 *  2) Application directory: "<app>/scripts/<templateFile>"
 *
 * \param templateFile File name under scripts/ (e.g. "palace_model.py", "openems_model.py").
 * \return Absolute path to the template file (may point to a non-existing file; caller should check/read).
 **********************************************************************************************************************/
QString MainWindow::resolveModelTemplatePath(const QString &templateFile) const
{
    const QString prefDir = m_preferences.value("MODEL_TEMPLATES_DIR").toString().trimmed();
    if (!prefDir.isEmpty()) {
        const QString p = QDir(prefDir).filePath(templateFile);
        if (QFileInfo(p).exists() && QFileInfo(p).isFile()) {
            return p;
        }
    }

    const QString appLoc = QCoreApplication::applicationDirPath();

    return QDir(appLoc).filePath(QStringLiteral("scripts/%1").arg(templateFile));
}

/*!*******************************************************************************************************************
 * \brief Creates the default Palace/Gmsh Python simulation script.
 *
 * Loads the Palace Python model template from the application scripts directory,
 * substitutes project-specific parameters (GDS file, substrate XML file, top cell name),
 * and parses the resulting script to rebuild simulation settings in the UI.
 *
 * \return Fully expanded Python simulation script, or an empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::createDefaultPalaceScript()
{
    QString gdsFile = m_ui->txtGdsFile->text().trimmed();
    if (!gdsFile.isEmpty())
        gdsFile = toWslPath(QDir::fromNativeSeparators(gdsFile));

    QString xmlFile = m_ui->txtSubstrate->text().trimmed();
    if (!xmlFile.isEmpty())
        xmlFile = toWslPath(QDir::fromNativeSeparators(xmlFile));

    // Top cell (gds_cellname) support
    QString topCell = m_ui->cbxTopCell->currentText().trimmed();

    auto pyEscape = [](QString s) -> QString {
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        return s;
    };

    const QString templatePath = resolveModelTemplatePath(QStringLiteral("palace_model.py"));

    QString templateText;
    if (!readTextFileUtf8(templatePath, templateText)) {
        return QString();
    }

    const QString script = QString::fromUtf8("%1")
                               .arg(templateText)
                               .arg(gdsFile, xmlFile, pyEscape(topCell));

    PythonParser::Result parseResult = PythonParser::parseSettingsFromText(script);
    if (parseResult.ok) {
        m_curPythonData = parseResult;
        const auto tips = mergeTipsPreferModel(parseResult.settingTips, m_keywordTips);
        rebuildSimulationSettingsFromPalace(parseResult.settings, tips, parseResult.topLevel);
    }

    return script;
}

/*!*******************************************************************************************************************
 * \brief Creates the default OpenEMS Python simulation script.
 *
 * Loads the OpenEMS Python model template from the application scripts directory,
 * substitutes project-specific parameters (GDS file, substrate XML file, top cell name),
 * and parses the resulting script to rebuild simulation settings in the UI.
 *
 * \return Fully expanded Python simulation script, or an empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::createDefaultOpenemsScript()
{
    QString gdsFile = m_ui->txtGdsFile->text().trimmed();
    if (!gdsFile.isEmpty())
        gdsFile = QDir::fromNativeSeparators(gdsFile);

    QString xmlFile = m_ui->txtSubstrate->text().trimmed();
    if (!xmlFile.isEmpty())
        xmlFile = QDir::fromNativeSeparators(xmlFile);

    QString topCell = m_ui->cbxTopCell->currentText().trimmed();

    auto pyEscape = [](QString s) -> QString {
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        return s;
    };

    const QString templatePath = resolveModelTemplatePath(QStringLiteral("openems_model.py"));

    QString templateText;
    if (!readTextFileUtf8(templatePath, templateText))
        return QString();

    const QString script = QString::fromUtf8("%1")
                               .arg(templateText)
                               .arg(gdsFile, xmlFile, pyEscape(topCell));

    PythonParser::Result parseResult = PythonParser::parseSettingsFromText(script);
    if (parseResult.ok) {
        m_curPythonData = parseResult;
        const auto tips = mergeTipsPreferModel(parseResult.settingTips, m_keywordTips);
        rebuildSimulationSettingsFromPalace(parseResult.settings, tips, parseResult.topLevel);
    }

    return script;
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
    m_preferences["SIMULATION_TOOL_KEY"]   = key.toLower();

    refreshKeywordTipsForCurrentTool();
}

/*!*******************************************************************************************************************
 * \brief Opens a Palace/OpenEMS Python model via a file dialog.
 *
 * Displays a file selection dialog, remembers the last used directory in
 * \c m_preferences under "PALACE_MODEL_DIR", and if the user selects a file,
 * forwards the file path to loadPythonModel() for actual parsing and loading.
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

    loadPythonModel(fileName);
    addRecentPythonModel(fileName);
    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Loads a Palace/OpenEMS Python model from the given file path.
 *
 * Reads the Python script, detects the model type, parses Palace settings,
 * updates simulation parameters, GDS and substrate paths, port table,
 * run directory, and loads the script into the Python editor.
 *
 * This function performs the full model import without opening any dialogs.
 * Used by the Open Python Model action, recent files, and startup restore.
 *
 * \param fileName Absolute path to the Python model file (.py) to load.
 **********************************************************************************************************************/
void MainWindow::loadPythonModel(const QString &fileName)
{
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
    if (!res.ok) {
        error(tr("Failed to parse Palace model file:\n%1").arg(res.error));
        return;
    }

    m_curPythonData = res;

    QString simKey;
    if (modelType == QLatin1String("openems"))
        simKey = QStringLiteral("openems");
    else if (modelType == QLatin1String("palace"))
        simKey = QStringLiteral("palace");
    else
        simKey = QStringLiteral("palace");

    const int idxSim = m_ui->cbxSimTool->findData(simKey);
    if (idxSim >= 0 && m_ui->cbxSimTool->isEnabled())
        m_ui->cbxSimTool->setCurrentIndex(idxSim);

    const auto tips = mergeTipsPreferModel(res.settingTips, m_keywordTips);
    rebuildSimulationSettingsFromPalace(res.settings, tips, res.topLevel);

    const QDir modelDir(fi.absolutePath());

    m_modelGdsKey.clear();
    m_modelXmlKey.clear();

    if (!res.getCellName().isEmpty())
    {
        const QString cellName = res.getCellName();

        const int idx = m_ui->cbxTopCell->findText(cellName);
        if (idx >= 0) {
            m_ui->cbxTopCell->setCurrentIndex(idx);
            m_simSettings[QStringLiteral("gds_cellname")] = cellName;
        }
    }


    if (!res.gdsFilename.isEmpty())
    {
        QString gdsPath = fromWslPath(res.gdsFilename);
        if (QFileInfo(gdsPath).isRelative())
            gdsPath = modelDir.filePath(gdsPath);

        m_ui->txtGdsFile->setText(gdsPath);

        m_modelGdsKey = !res.gdsSettingKey.isEmpty() ? res.gdsSettingKey
                        : !res.gdsLegacyVar.isEmpty()  ? res.gdsLegacyVar
                                                      : QStringLiteral("GdsFile");

        m_simSettings[m_modelGdsKey] = gdsPath;
        m_simSettings[QStringLiteral("GdsFile")] = gdsPath; // keep canonical for existing code
        m_sysSettings["GdsDir"] = QFileInfo(gdsPath).absolutePath();
    }

    if (!res.xmlFilename.isEmpty())
    {
        QString subPath = fromWslPath(res.xmlFilename);
        if (QFileInfo(subPath).isRelative())
            subPath = modelDir.filePath(subPath);

        m_ui->txtSubstrate->setText(subPath);

        m_modelXmlKey = !res.xmlSettingKey.isEmpty() ? res.xmlSettingKey
                        : !res.xmlLegacyVar.isEmpty()  ? res.xmlLegacyVar
                                                      : QStringLiteral("SubstrateFile");

        m_simSettings[m_modelXmlKey] = subPath;
        m_simSettings[QStringLiteral("SubstrateFile")] = subPath; // keep canonical for existing code
        m_sysSettings["SubstrateDir"] = QFileInfo(subPath).absolutePath();
    }

    updateSubLayerNamesCheckboxState();

    {
        QSignalBlocker blocker(m_ui->editRunPythonScript);
        m_ui->editRunPythonScript->setPlainText(text);
        m_ui->editRunPythonScript->document()->setModified(false);
    }

    m_ui->txtRunPythonScript->setText(fileName);
    m_simSettings["RunPythonScript"] = fileName;

    m_ui->tblPorts->setRowCount(0);
    importPortsFromEditor();
    updateSubLayerNamesAutoCheck();

    if (!res.simPath.isEmpty())
    {
        QString runDir = res.simPath;
        if (QFileInfo(runDir).isRelative())
            runDir = modelDir.filePath(runDir);

        m_simSettings["RunDir"] = fi.absolutePath();
    }

    setStateSaved();
}

/*!*******************************************************************************************************************
 * \brief Ensures that a valid Python model file path exists, invoking "Save As" if necessary.
 *
 * If \a forceDialog is false and a script path is already set, the function returns immediately.
 * Otherwise it prepares a suggested directory and file name, runs a Save As dialog, validates the
 * selected folder against simulation-specific requirements, and on success stores the path in UI
 * and preferences.
 *
 * \param forceDialog If true, always opens the Save As dialog even if a path is already present.
 * \return True if a valid Python script path is available after the call; false if the user cancels.
 **********************************************************************************************************************/
bool MainWindow::ensurePythonScriptPathBySaveAs(bool forceDialog)
{
    const QString currentPath = currentPythonScriptPath().trimmed();
    if (!forceDialog && !currentPath.isEmpty())
        return true;

    QString startDir;
    QString suggestedName;
    initSaveAsSuggestion(currentPath, startDir, suggestedName);

    const QString simKey = currentSimToolKey().toLower();

    while (true) {
        QString chosenPath = showPythonModelSaveAsDialog(startDir, suggestedName);
        if (chosenPath.isEmpty())
            return false;

        chosenPath = ensurePySuffix(chosenPath);

        QString missingFolder;
        if (!validateRequiredFolderForSim(QFileInfo(chosenPath).absolutePath(), simKey, &missingFolder)) {
            const RequiredFolderDecision decision =
                askMissingFolderDecision(simKey, missingFolder);

            if (decision == RequiredFolderDecision::ChooseAnotherDir) {
                startDir = QFileInfo(chosenPath).absolutePath();
                suggestedName = QFileInfo(chosenPath).fileName();
                continue;
            }
            if (decision == RequiredFolderDecision::Cancel)
                return false;
            // SaveAnyway -> continue saving
        }

        commitChosenPythonModelPath(chosenPath);
        return true;
    }
}

/*!*******************************************************************************************************************
 * \brief Returns the current Python script path from the UI (trimmed).
 * \return Current script path string (may be empty).
 **********************************************************************************************************************/
QString MainWindow::currentPythonScriptPath() const
{
    return m_ui->txtRunPythonScript ? m_ui->txtRunPythonScript->text().trimmed() : QString();
}

/*!*******************************************************************************************************************
 * \brief Initializes the starting directory and suggested file name for "Save Python Model As".
 *
 * The function chooses a sensible folder and filename suggestion based on:
 *  - current script path (if present),
 *  - last saved model path from preferences,
 *  - GDS file location (if present),
 *  - top cell name (if available),
 *  - otherwise falls back to the user home directory and "model.py".
 *
 * \param currentPath    Current script path from UI (may be empty).
 * \param[out] startDir  Output directory used as dialog стартовая папка.
 * \param[out] suggestedName Output file name suggestion (e.g. "<TopCell>.py" or "model.py").
 **********************************************************************************************************************/
void MainWindow::initSaveAsSuggestion(const QString &currentPath,
                                      QString &startDir,
                                      QString &suggestedName) const
{
    const QString prefPath = m_preferences.value("PALACE_MODEL_FILE").toString().trimmed();

    if (!currentPath.isEmpty()) {
        const QFileInfo cfi(currentPath);
        startDir = cfi.absolutePath();
        suggestedName = cfi.fileName();
        return;
    }

    // Prefer directory from GDS if possible, otherwise from prefPath, otherwise home.
    startDir = bestDefaultModelDirectory(prefPath);

    const QString topCell = bestTopCellName();
    if (!topCell.isEmpty()) {
        suggestedName = topCell + QStringLiteral(".py");
        return;
    }

    if (!prefPath.isEmpty()) {
        suggestedName = QFileInfo(prefPath).fileName();
        if (suggestedName.isEmpty())
            suggestedName = QStringLiteral("model.py");
        return;
    }

    suggestedName = QStringLiteral("model.py");
}

/*!*******************************************************************************************************************
 * \brief Chooses the best default directory for saving a Python model.
 *
 * Prefers the directory of an existing GDS file (if set), otherwise falls back to the directory of
 * \a prefPath (if non-empty), and finally to the user's home directory.
 *
 * \param prefPath Preference-stored model file path (may be empty).
 * \return Directory path to use as Save As dialog start directory.
 **********************************************************************************************************************/
QString MainWindow::bestDefaultModelDirectory(const QString &prefPath) const
{
    QString gdsPath;
    if (m_ui->txtGdsFile)
        gdsPath = m_ui->txtGdsFile->text().trimmed();

    if (!gdsPath.isEmpty() && QFileInfo::exists(gdsPath))
        return QFileInfo(gdsPath).absolutePath();

    if (!prefPath.isEmpty())
        return QFileInfo(prefPath).absolutePath();

    return QDir::homePath();
}

/*!*******************************************************************************************************************
 * \brief Returns the best available top cell name for file name suggestion.
 *
 * Uses m_simSettings["TopCell"] if set; otherwise falls back to the UI top cell combo box.
 *
 * \return Top cell name or empty string if none is available.
 **********************************************************************************************************************/
QString MainWindow::bestTopCellName() const
{
    QString topCell = m_simSettings.value("TopCell").toString().trimmed();
    if (topCell.isEmpty() && m_ui->cbxTopCell)
        topCell = m_ui->cbxTopCell->currentText().trimmed();
    return topCell;
}

/*!*******************************************************************************************************************
 * \brief Shows the "Save Python Model As" dialog.
 *
 * Builds the default dialog path from \a startDir and \a suggestedName and opens QFileDialog::getSaveFileName().
 *
 * \param startDir Starting directory for the dialog.
 * \param suggestedName Suggested file name to prefill.
 * \return Selected path or empty string if the user cancels.
 **********************************************************************************************************************/
QString MainWindow::showPythonModelSaveAsDialog(const QString &startDir,
                                                const QString &suggestedName) const
{
    const QString defaultPath = QDir(startDir).filePath(suggestedName);

    return QFileDialog::getSaveFileName(
        const_cast<MainWindow*>(this),
        tr("Save Python Model As"),
        defaultPath,
        tr("Python Files (*.py);;All Files (*)"));
}

/*!*******************************************************************************************************************
 * \brief Ensures that the given path has a ".py" suffix.
 *
 * If the user provided a file name without extension, ".py" is appended.
 *
 * \param path Chosen file path.
 * \return Path with ".py" extension enforced.
 **********************************************************************************************************************/
QString MainWindow::ensurePySuffix(QString path) const
{
    QFileInfo fi(path);
    if (fi.suffix().isEmpty())
        path += QStringLiteral(".py");
    return path;
}

/*!*******************************************************************************************************************
 * \brief Returns the required folder name for a given simulation backend key.
 *
 * OpenEMS expects the model to be located in a folder containing "modules".
 * Palace expects the model to be located in a folder containing "gds2palace".
 *
 * \param simKeyLower Simulation key in lower case ("openems" / "palace").
 * \return Required folder name or empty string if no requirement applies.
 **********************************************************************************************************************/
QString MainWindow::requiredFolderForSim(const QString &simKeyLower) const
{
    if (simKeyLower == QLatin1String("openems"))
        return QStringLiteral("modules");
    if (simKeyLower == QLatin1String("palace"))
        return QStringLiteral("gds2palace");
    return QString();
}

/*!*******************************************************************************************************************
 * \brief Validates that a directory contains simulation-specific required subfolder (if any).
 *
 * \param dirPath       Directory chosen by the user for saving the model.
 * \param simKeyLower   Simulation key in lower case ("openems"/"palace").
 * \param[out] missingName Receives the required folder name if missing.
 *
 * \return True if the directory is acceptable; false if a required folder is missing.
 **********************************************************************************************************************/
bool MainWindow::validateRequiredFolderForSim(const QString &dirPath,
                                              const QString &simKeyLower,
                                              QString *missingName) const
{
    const QString need = requiredFolderForSim(simKeyLower);
    if (need.isEmpty())
        return true;

    const bool ok = QDir(QDir(dirPath).filePath(need)).exists();
    if (!ok && missingName)
        *missingName = need;
    return ok;
}

/*!*******************************************************************************************************************
 * \brief Asks the user what to do if the selected directory misses required simulation modules folder.
 *
 * \param simKeyLower Simulation key in lower case ("openems"/"palace").
 * \param missingFolder Required folder name that is missing in the selected directory.
 * \return User decision: choose another directory, save anyway, or cancel.
 **********************************************************************************************************************/
MainWindow::RequiredFolderDecision
MainWindow::askMissingFolderDecision(const QString &simKeyLower,
                                     const QString &missingFolder) const
{
    const QString simName =
        (simKeyLower == QLatin1String("openems")) ? QStringLiteral("OpenEMS") :
            (simKeyLower == QLatin1String("palace"))  ? QStringLiteral("Palace")  :
            simKeyLower;

    QMessageBox msg(const_cast<MainWindow*>(this));
    msg.setIcon(QMessageBox::Warning);
    msg.setWindowTitle(tr("Missing simulation modules"));
    msg.setText(tr("The selected folder does not contain the required '%1' directory for %2.")
                    .arg(missingFolder, simName));
    msg.setInformativeText(tr("Choose another folder, or save here anyway."));

    QPushButton *btnChoose = msg.addButton(tr("Choose another directory"), QMessageBox::AcceptRole);
    QPushButton *btnSave   = msg.addButton(tr("Save here anyway"), QMessageBox::DestructiveRole);
    QPushButton *btnCancel = msg.addButton(QMessageBox::Cancel);

    msg.exec();

    if (msg.clickedButton() == btnChoose) return RequiredFolderDecision::ChooseAnotherDir;
    if (msg.clickedButton() == btnSave)   return RequiredFolderDecision::SaveAnyway;
    Q_UNUSED(btnCancel);
    return RequiredFolderDecision::Cancel;
}

/*!*******************************************************************************************************************
 * \brief Stores the chosen Python model path into UI and preferences and updates UI palette.
 *
 * \param path Absolute file path to the selected Python model.
 **********************************************************************************************************************/
void MainWindow::commitChosenPythonModelPath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);

    m_ui->txtRunPythonScript->setText(native);
    m_preferences["PALACE_MODEL_FILE"] = path;

    setLineEditPalette(m_ui->txtRunPythonScript, path);
}

/*!*******************************************************************************************************************
 * \brief Returns the list of recently opened Python model files.
 *
 * Retrieves the stored list of Python model file paths from the preferences /
 * settings storage used by the application.
 *
 * \return List of file paths ordered from most recent to least recent.
 **********************************************************************************************************************/
QStringList MainWindow::recentPythonModels() const
{
    const QVariant v = m_preferences.value("RECENT_PYTHON_MODELS");
    if (v.canConvert<QStringList>())
        return v.toStringList();
    if (v.type() == QVariant::String)
        return QStringList{ v.toString() };
    return {};
}

/*!*******************************************************************************************************************
 * \brief Stores the list of recently opened Python model files.
 *
 * Writes \p list into the preferences / settings storage used by the
 * application. The list is expected to be ordered from most recent to least
 * recent.
 *
 * \param list List of Python model file paths.
 **********************************************************************************************************************/
void MainWindow::setRecentPythonModels(const QStringList& list)
{
    m_preferences["RECENT_PYTHON_MODELS"] = list;
}

/*!*******************************************************************************************************************
 * \brief Initializes the "Recent" menu for Python model files.
 *
 * Locates (or creates) the "Recent" submenu under File, creates up to five
 * placeholder actions, connects them to the open-recent handler and populates
 * the menu based on the stored list of recently opened Python model files.
 **********************************************************************************************************************/
void MainWindow::initRecentMenu()
{
    QAction* recentAct = m_ui->actionRecent;
    if (!recentAct)
        return;

    if (!m_menuRecent) {
        m_menuRecent = new QMenu(this);
        m_menuRecent->setObjectName("menuRecent");
        recentAct->setMenu(m_menuRecent);
    } else {
        m_menuRecent->clear();
        recentAct->setMenu(m_menuRecent);
    }

    m_recentModelActions.clear();

    for (int i = 0; i < kMaxRecentPythonModels; ++i) {
        QAction* a = new QAction(this);
        a->setVisible(false);
        connect(a, &QAction::triggered, this, &MainWindow::onOpenRecentPythonModel);
        m_menuRecent->addAction(a);
        m_recentModelActions.push_back(a);
    }

    QAction* clearAct = m_menuRecent->addAction(tr("Clear"));
    connect(clearAct, &QAction::triggered, this, [this]() {
        setRecentPythonModels({});
        updateRecentMenu();
        saveSettings();
    });

    updateRecentMenu();
}

/*!*******************************************************************************************************************
 * \brief Updates the "Recent" menu entries for Python model files.
 *
 * Reads the stored list of recently opened Python model files, sanitizes it
 * (keeps only unique existing *.py files) and updates the corresponding menu
 * actions (text, tooltip and action data). The menu is disabled when the list
 * is empty.
 **********************************************************************************************************************/
void MainWindow::updateRecentMenu()
{
    if (!m_menuRecent)
        return;

    QStringList files = recentPythonModels();

    QStringList cleaned;
    cleaned.reserve(files.size());

    for (const QString& p : files) {
        const QString path = QDir::fromNativeSeparators(p.trimmed());
        if (path.isEmpty())
            continue;
        if (!path.endsWith(".py", Qt::CaseInsensitive))
            continue;
        if (!QFileInfo::exists(path))
            continue;
        if (!cleaned.contains(path))
            cleaned << path;
        if (cleaned.size() >= kMaxRecentPythonModels)
            break;
    }

    if (cleaned != files) {
        setRecentPythonModels(cleaned);
    }

    const int n = cleaned.size();
    for (int i = 0; i < m_recentModelActions.size(); ++i) {
        QAction* a = m_recentModelActions[i];
        if (i < n) {
            const QString filePath = cleaned.at(i);
            const QString shown = QDir::toNativeSeparators(filePath);

            a->setText(QString("&%1  %2").arg(i + 1).arg(shown));
            a->setToolTip(shown);
            a->setData(filePath);
            a->setVisible(true);
        } else {
            a->setVisible(false);
        }
    }

    m_menuRecent->setEnabled(n > 0);
}

/*!*******************************************************************************************************************
 * \brief Adds a Python model file to the recent-files list.
 *
 * Prepends \p filePath to the recent list, removes duplicates, enforces the
 * maximum number of entries and refreshes the "Recent" menu. Non-Python files
 * are ignored.
 *
 * \param filePath Absolute path to the Python model file (*.py).
 **********************************************************************************************************************/
void MainWindow::addRecentPythonModel(const QString& filePath)
{
    QString path = QDir::fromNativeSeparators(filePath.trimmed());
    if (path.isEmpty())
        return;

    if (!path.endsWith(".py", Qt::CaseInsensitive))
        return;

    QStringList files = recentPythonModels();
    files.removeAll(path);
    files.prepend(path);

    while (files.size() > kMaxRecentPythonModels)
        files.removeLast();

    setRecentPythonModels(files);
    updateRecentMenu();
}

/*!*******************************************************************************************************************
 * \brief Opens a Python model selected from the "Recent" menu.
 *
 * Triggered by one of the recent-file actions. Loads the model referenced by
 * the triggering action, updates the recent list order and removes stale
 * entries if the file no longer exists.
 **********************************************************************************************************************/
void MainWindow::onOpenRecentPythonModel()
{
    QAction* a = qobject_cast<QAction*>(sender());
    if (!a)
        return;

    const QString filePath = a->data().toString();
    if (filePath.isEmpty())
        return;

    if (!QFileInfo::exists(filePath)) {
        QStringList files = recentPythonModels();
        files.removeAll(filePath);
        setRecentPythonModels(files);
        updateRecentMenu();
        saveSettings();
        error(tr("File not found: %1").arg(QDir::toNativeSeparators(filePath)));
        return;
    }

    loadPythonModel(filePath);
    addRecentPythonModel(filePath);
    saveSettings();
}

/*!*******************************************************************************************************************
 * \brief Tries to auto-load a recent Python script matching the currently selected top cell.
 *
 * This is intended for KLayout integration workflow:
 * - KLayout starts EMStudio with -gdsfile and -topcell
 * - after setGdsFile()/setTopCell() we try to find a recent Python script named "<topcell>.py"
 *   (by comparing QFileInfo(path).completeBaseName() with the top cell name)
 * - if found, the script is loaded into the editor and applied to GUI settings
 *
 * The function does nothing if:
 * - top cell is empty
 * - no recent python list exists / is empty
 * - no matching script is found
 **********************************************************************************************************************/
void MainWindow::tryAutoLoadRecentPythonForTopCell()
{
    if (!m_ui || !m_ui->cbxTopCell)
        return;

    const QString top = m_ui->cbxTopCell->currentText().trimmed();
    if (top.isEmpty())
        return;

    const QStringList recentPy = m_preferences.value(QStringLiteral("RecentPythonScripts")).toStringList();
    if (recentPy.isEmpty())
        return;

    QString bestMatch;

    for (const QString &p : recentPy)
    {
        if (p.isEmpty())
            continue;

        QFileInfo fi(p);
        if (!fi.exists() || !fi.isFile())
            continue;

        if (fi.completeBaseName().compare(top, Qt::CaseInsensitive) == 0 &&
            fi.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0)
        {
            bestMatch = fi.absoluteFilePath();
            break;
        }
    }

    if (bestMatch.isEmpty())
        return;

    loadPythonScriptToEditor(bestMatch);

    applyPythonScriptFromEditor();
}
