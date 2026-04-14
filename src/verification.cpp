#ifdef EMSTUDIO_TESTING

#include <QSignalBlocker>
#include <QString>
#include <QVector>
#include <QDebug>
#include <QDir>
#include <QProcess>

#include "mainwindow.h"
#include "ui_mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Initializes a default Palace Python model for test purposes without any UI dialogs.
 *
 * This helper is intended exclusively for automated tests. It performs a deterministic
 * initialization sequence equivalent to pressing "Generate Default Python" for the Palace
 * backend, but without any message boxes or file dialogs.
 *
 * The function:
 *  - Forces the simulation tool selection to "palace" (without validating installation paths).
 *  - Generates the default Palace Python script via createDefaultPalaceScript().
 *  - Inserts the generated script directly into the Python editor.
 *  - Clears and rebuilds the Ports table by parsing ports from the script.
 *
 * No filesystem writes are performed.
 *
 * \return True if the default Palace script was generated and inserted successfully,
 *         false if script generation failed or returned empty content.
 **********************************************************************************************************************/
bool MainWindow::testInitDefaultPalaceModel()
{
    // Ensure combo points to palace (no validation based on installed tools)
    const int idx = m_ui->cbxSimTool->findData("palace");
    if (idx >= 0) {
        QSignalBlocker b(m_ui->cbxSimTool);
        m_ui->cbxSimTool->setCurrentIndex(idx);
    }

    const QString script = createDefaultPalaceScript();
    if (script.trimmed().isEmpty())
        return false;

    testSetEditorText(script);
    m_ui->editRunPythonScript->document()->setModified(false);

    return true;
}

/*!*******************************************************************************************************************
 * \brief Parses simulation ports from the current Python editor content (test helper).
 *
 * This function extracts all simulation_port(...) definitions from the current Python editor
 * text using the same parsing logic as the production code, but without modifying any GUI state.
 *
 * Intended for use in unit and golden-file tests to verify:
 *  - number of ports
 *  - port indices
 *  - electrical parameters (Z0, voltage)
 *  - layer and direction mappings
 *
 * \return Vector of parsed PortInfo structures in order of appearance in the script.
 **********************************************************************************************************************/
QVector<MainWindow::PortInfo> MainWindow::testParsePortsFromEditor() const
{
    return const_cast<MainWindow*>(this)->parsePortsFromScript(m_ui->editRunPythonScript->toPlainText());
}

/*!*******************************************************************************************************************
 * \brief Generates a Python simulation script from the current GUI state without file I/O.
 *
 * This function provides a deterministic "GUI → Python script" conversion path intended
 * for automated testing. It applies the current simulation settings and port configuration
 * from the GUI to the Python script currently loaded in the editor.
 *
 * The generation process includes:
 *  - Applying simulation settings (frequency range, boundaries, etc.) to the script.
 *  - Rebuilding the port section from the Ports table and reinserting it into the script.
 *  - Updating GDS and substrate paths according to the selected simulation backend.
 *
 * No dialogs are shown and no files are written. The returned script is suitable for
 * golden-file comparison in regression tests.
 *
 * \param[out] outError Optional pointer receiving an error description if generation fails.
 * \return The fully generated Python script text, or an empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::testGenerateScriptFromGuiState(QString *outError)
{
    QString script = m_ui->editRunPythonScript->toPlainText();
    if (script.trimmed().isEmpty()) {
        if (outError) *outError = "Editor script is empty.";
        return {};
    }

    const QString simKeyLower = currentSimToolKey().toLower().trimmed();
    if (simKeyLower.isEmpty()) {
        if (outError) *outError = "Simulation tool key is empty.";
        return {};
    }

    applySimSettingsToScript(script, simKeyLower);

    const QString portCode = buildPortCodeFromGuiTable();
    replaceOrInsertPortSection(script, portCode);

    applyGdsAndXmlPaths(script, simKeyLower);

    return script;
}

/*!*******************************************************************************************************************
 * \brief Sets a simulation setting key/value pair for tests without going through the property browser.
 *
 * This helper is intended for automated tests to modify \c m_simSettings deterministically
 * (without UI interaction, signals, or dialogs). It marks the window state as changed so that
 * script regeneration paths behave the same way as for user edits.
 *
 * \param key Setting name/key to write into \c m_simSettings (e.g. "fstart", "fstop", "numfreq").
 * \param val Value to assign.
 **********************************************************************************************************************/
void MainWindow::testSetSimSetting(const QString& key, const QVariant& val)
{
    m_simSettings[key] = val;
    setStateChanged();
}

/*!*******************************************************************************************************************
 * \brief Returns the current Python editor content for test inspection.
 *
 * This helper provides read-only access to the Python editor text without
 * triggering any UI updates or state changes. It is intended for use in
 * unit and golden-file tests to compare the generated or modified script
 * against expected reference content.
 *
 * \return Full Python script text currently present in the editor,
 *         or an empty string if the editor is not initialized.
 **********************************************************************************************************************/
QString MainWindow::testEditorText() const
{
    return m_ui && m_ui->editRunPythonScript ? m_ui->editRunPythonScript->toPlainText() : QString();
}

/*!*******************************************************************************************************************
 * \brief Replaces the Python editor content for tests without user interaction.
 *
 * This helper sets the complete text of the Python editor in a deterministic
 * way, blocking editor signals to avoid unintended side effects. The document
 * is marked as modified to emulate user edits and ensure that regeneration
 * paths behave consistently in automated tests.
 *
 * No dialogs are shown and no files are written.
 *
 * \param s Full Python script text to insert into the editor.
 **********************************************************************************************************************/
void MainWindow::testSetEditorText(const QString& s)
{
    if (!m_ui || !m_ui->editRunPythonScript)
        return;

    QSignalBlocker b(m_ui->editRunPythonScript);
    m_ui->editRunPythonScript->setPlainText(s);
    m_ui->editRunPythonScript->document()->setModified(true);
}

/*!*******************************************************************************************************************
 * \brief Selects the simulation backend by its stable key ("openems"/"palace") for unit tests.
 *
 * This helper emulates the user selection in the Simulation Tool combo box without opening any dialogs.
 * It sets the corresponding item in \c cbxSimTool by matching the item data (stable tool key),
 * triggers the same update path as the UI slot (persisting \c SIMULATION_TOOL_KEY / INDEX and
 * refreshing keyword tips), and returns whether the key was found and applied.
 *
 * \param simToolKey Stable tool key to select (e.g. "openems" or "palace"), case-insensitive.
 * \param outErr Optional error string for diagnostics.
 * \return True if the tool key existed in the combo box and was selected; otherwise false.
 **********************************************************************************************************************/
bool MainWindow::testSetSimToolKey(const QString& simToolKey, QString* outErr)
{
    if (outErr) *outErr = QString();

    if (!m_ui || !m_ui->cbxSimTool) {
        if (outErr) *outErr = QStringLiteral("cbxSimTool is not initialized.");
        return false;
    }

    if (!m_ui->cbxSimTool->isEnabled()) {
        if (outErr) *outErr = QStringLiteral("cbxSimTool is disabled (no simulation tool configured?).");
        return false;
    }

    const QString key = simToolKey.trimmed().toLower();
    if (key.isEmpty()) {
        if (outErr) *outErr = QStringLiteral("Requested sim tool key is empty.");
        return false;
    }

    if (m_ui->cbxSimTool->count() == 0) {
        refreshSimToolOptions();
    }

    const int idx = m_ui->cbxSimTool->findData(key);
    if (idx < 0) {
        if (outErr) {
            QStringList avail;
            for (int i = 0; i < m_ui->cbxSimTool->count(); ++i) {
                avail << m_ui->cbxSimTool->itemData(i).toString();
            }
            *outErr = QString("Simulation tool key '%1' not found in combo. Available: [%2]")
                          .arg(key, avail.join(", "));
        }
        return false;
    }

    {
        QSignalBlocker blocker(m_ui->cbxSimTool);
        m_ui->cbxSimTool->setCurrentIndex(idx);
    }

    on_cbxSimTool_currentIndexChanged(idx);

    const QString applied = currentSimToolKey();
    if (applied != key) {
        if (outErr) *outErr = QString("Failed to apply tool key. Expected '%1', got '%2'.").arg(key, applied);
        return false;
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Sets a preference key/value pair for tests without UI interaction.
 *
 * This helper writes directly into the internal preferences storage used by
 * MainWindow, bypassing dialogs, validators, and persistent settings.
 * It is intended exclusively for automated tests to simulate configured
 * external tools (e.g. Palace/OpenEMS) in CI environments.
 *
 * \param key Preference key to set (e.g. "PALACE_INSTALL_PATH").
 * \param value Preference value to assign.
 **********************************************************************************************************************/
void MainWindow::testSetPreference(const QString& key, const QVariant& value)
{
    m_preferences[key] = value;
}

/*!*******************************************************************************************************************
 * \brief Refreshes the Simulation Tool combo box for test environments.
 *
 * This helper re-evaluates available simulation backends based on the current
 * preferences state and updates the Simulation Tool combo box accordingly.
 * Unlike the production path, this function is intended to be callable
 * explicitly from unit tests after test preferences have been injected.
 *
 * It allows CI tests to enable and select simulation backends (e.g. Palace)
 * without requiring real external tool installations.
 **********************************************************************************************************************/
void MainWindow::refreshSimToolOptionsForTests()
{
    refreshSimToolOptions();
}

/*!*******************************************************************************************************************
 * \brief Initializes a default OpenEMS Python model for test purposes without any UI dialogs.
 *
 * Forces the simulation tool selection to "openems", generates the default OpenEMS
 * Python script, inserts it into the editor and imports ports into the table.
 *
 * \return True if the default OpenEMS script was generated successfully, false otherwise.
 **********************************************************************************************************************/
bool MainWindow::testInitDefaultOpenemsModel()
{
    const int idx = m_ui->cbxSimTool->findData("openems");
    if (idx >= 0) {
        QSignalBlocker b(m_ui->cbxSimTool);
        m_ui->cbxSimTool->setCurrentIndex(idx);
    }

    const QString script = createDefaultOpenemsScript();
    if (script.trimmed().isEmpty())
        return false;

    testSetEditorText(script);
    m_ui->editRunPythonScript->document()->setModified(false);

    m_ui->tblPorts->setRowCount(0);
    importPortsFromEditor();

    return true;
}

/*!*******************************************************************************************************************
 * \brief Returns the current number of rows in the Ports table.
 *
 * \return Row count of tblPorts, or -1 if the table is not available.
 **********************************************************************************************************************/
int MainWindow::testPortsRowCount() const
{
    return (m_ui && m_ui->tblPorts) ? m_ui->tblPorts->rowCount() : -1;
}

/*!*******************************************************************************************************************
 * \brief Programmatically toggles the "Use Substrate Layer Names" checkbox for tests.
 *
 * Updates the checkbox state and explicitly invokes the corresponding slot
 * to ensure the same behavior as in normal UI interaction.
 *
 * \param on True to enable substrate layer names, false to switch to numeric GDS layers.
 **********************************************************************************************************************/
void MainWindow::testSetSubLayerNamesChecked(bool on)
{
    if (!m_ui || !m_ui->cbSubLayerNames)
        return;

    updateSubLayerNamesCheckboxState();

    {
        QSignalBlocker b(m_ui->cbSubLayerNames);
        m_ui->cbSubLayerNames->setChecked(on);
    }

    on_cbSubLayerNames_stateChanged(on ? Qt::Checked : Qt::Unchecked);
}

/*!*******************************************************************************************************************
 * \brief Writes the current editor text into a temporary Python file for process-based tests.
 *
 * \param fileNameHint Optional file name hint.
 * \return Absolute path to the created file, or empty string on failure.
 **********************************************************************************************************************/
QString MainWindow::testWriteEditorToTempPyFile(const QString& fileNameHint) const
{
    const QString baseDir =
        QDir::temp().filePath(QStringLiteral("emstudio_test_runopenems"));
    QDir().mkpath(baseDir);

    const QString fileName = fileNameHint.isEmpty()
                                 ? QStringLiteral("test_script.py")
                                 : fileNameHint;

    const QString path = QDir(baseDir).filePath(fileName);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};

    f.write(testEditorText().toUtf8());
    f.close();

    return path;
}

/*!*******************************************************************************************************************
 * \brief Returns the current simulation log text for test inspection.
 *
 * \return Full text content of the simulation log editor.
 **********************************************************************************************************************/
QString MainWindow::testSimulationLogText() const
{
    return (m_ui && m_ui->editSimulationLog)
    ? m_ui->editSimulationLog->toPlainText()
    : QString();
}

/*!*******************************************************************************************************************
 * \brief Returns whether a simulation process is currently running.
 *
 * \return True if m_simProcess exists and is running, false otherwise.
 **********************************************************************************************************************/
bool MainWindow::testIsSimulationRunning() const
{
    return m_simProcess && m_simProcess->state() == QProcess::Running;
}

/*!*******************************************************************************************************************
 * \brief Sets the Python script path for OpenEMS runs in test mode.
 *
 * This helper assigns the Python model path both to the GUI line edit
 * (txtRunPythonScript) and to the internal simulation settings map.
 * It avoids triggering any UI signals and ensures that headless runs
 * (e.g. runOpenEMS(false)) do not open a "Save As" dialog during tests.
 *
 * Intended for automated tests that prepare a temporary Python script
 * and want to run it through the normal OpenEMS execution path.
 *
 * \param path Absolute path to the Python script file to use for the run.
 **********************************************************************************************************************/
void MainWindow::testSetRunPythonScriptPath(const QString& path)
{
    if (!m_ui || !m_ui->txtRunPythonScript)
        return;

    {
        QSignalBlocker b(m_ui->txtRunPythonScript);
        m_ui->txtRunPythonScript->setText(path);
    }

    m_simSettings["RunPythonScript"] = path;
}

/*!*******************************************************************************************************************
 * \brief Starts an OpenEMS simulation from tests without requiring user interaction.
 *
 * This helper forwards the call directly to runOpenEMS(), allowing unit
 * and golden tests to execute the normal simulation launch path without
 * accessing private methods.
 *
 * It is typically used together with:
 *  - testSetRunPythonScriptPath()
 *  - testWriteEditorToTempPyFile()
 *  - a Python stub executable configured via "Python Path"
 *
 * This allows CI tests to verify process startup, logging behaviour,
 * and proper termination of the simulation process without requiring
 * a real OpenEMS installation.
 *
 * \param interactive If true, the normal interactive workflow is used.
 *                    If false, the simulation runs in headless mode
 *                    (used by automated tests).
 **********************************************************************************************************************/
void MainWindow::testRunOpenems(bool interactive)
{
    runOpenEMS(interactive);
}

/*!*******************************************************************************************************************
 * \brief Starts Palace simulation from tests without exposing private production API.
 *
 * \param interactive If true, runs in interactive mode; otherwise in headless mode.
 **********************************************************************************************************************/
void MainWindow::testRunPalace(bool interactive)
{
    runPalace(interactive);
}

/*!*******************************************************************************************************************
 * \brief Starts headless backend dispatch from tests without exposing private production API.
 *
 * \param simKeyLower Backend key to dispatch ("palace", "openems", or other).
 **********************************************************************************************************************/
void MainWindow::testRunHeadless(const QString& simKeyLower)
{
    runHeadless(simKeyLower);
}

/*!*******************************************************************************************************************
 * \brief Returns whether MainWindow is currently in headless mode.
 *
 * \return True if headless mode is enabled, false otherwise.
 **********************************************************************************************************************/
bool MainWindow::testIsHeadless() const
{
    return m_headless;
}

/*!*******************************************************************************************************************
 * \brief Exposes toLinuxPathPortable() for automated tests.
 *
 * \param path Input path.
 * \param distro WSL distribution name.
 * \param timeoutMs Timeout in milliseconds.
 * \return Converted Linux path.
 **********************************************************************************************************************/
QString MainWindow::testToLinuxPathPortable(const QString& path,
                                            const QString& distro,
                                            int timeoutMs) const
{
    return toLinuxPathPortable(path, distro, timeoutMs);
}

/*!*******************************************************************************************************************
 * \brief Exposes pathExistsPortable() for automated tests.
 *
 * \param path Input path.
 * \param distro WSL distribution name.
 * \param timeoutMs Timeout in milliseconds.
 * \return True if the path exists.
 **********************************************************************************************************************/
bool MainWindow::testPathExistsPortable(const QString& path,
                                        const QString& distro,
                                        int timeoutMs) const
{
    return pathExistsPortable(path, distro, timeoutMs);
}

/*!*******************************************************************************************************************
 * \brief Imports ports from the current editor text in test mode.
 **********************************************************************************************************************/
void MainWindow::testImportPortsFromEditor()
{
    importPortsFromEditor();
}

/*!*******************************************************************************************************************
 * \brief Returns plain table item text from the Ports table.
 *
 * \param row Row index.
 * \param col Column index.
 * \return Item text or empty string if the cell has no QTableWidgetItem.
 **********************************************************************************************************************/
QString MainWindow::testPortCellText(int row, int col) const
{
    if (!m_ui || !m_ui->tblPorts)
        return QString();

    QTableWidgetItem* item = m_ui->tblPorts->item(row, col);
    return item ? item->text() : QString();
}

/*!*******************************************************************************************************************
 * \brief Returns current combo-box text from the Ports table.
 *
 * \param row Row index.
 * \param col Column index.
 * \return Current combo text or empty string if the cell does not contain a QComboBox.
 **********************************************************************************************************************/
QString MainWindow::testPortComboText(int row, int col) const
{
    if (!m_ui || !m_ui->tblPorts)
        return QString();

    QComboBox* box = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, col));
    return box ? box->currentText() : QString();
}

/*!*******************************************************************************************************************
 * \brief Calls on_btnAddPort_clicked() in test mode.
 **********************************************************************************************************************/
void MainWindow::testClickAddPort()
{
    on_btnAddPort_clicked();
}

/*!*******************************************************************************************************************
 * \brief Calls on_btnReomovePort_clicked() in test mode.
 **********************************************************************************************************************/
void MainWindow::testClickRemoveCurrentPort()
{
    on_btnReomovePort_clicked();
}

/*!*******************************************************************************************************************
 * \brief Calls on_btnRemovePorts_clicked() in test mode.
 **********************************************************************************************************************/
void MainWindow::testRemoveAllPorts()
{
    on_btnRemovePorts_clicked();
}

/*!*******************************************************************************************************************
 * \brief Sets the current selected row in the Ports table.
 *
 * \param row Row index to select.
 **********************************************************************************************************************/
void MainWindow::testSetCurrentPortRow(int row)
{
    if (!m_ui || !m_ui->tblPorts)
        return;

    m_ui->tblPorts->setCurrentCell(row, 0);
}

/*!*******************************************************************************************************************
 * \brief Returns the currently selected simulation tool key for tests.
 *
 * \return Stable tool key such as "openems" or "palace".
 **********************************************************************************************************************/
QString MainWindow::testCurrentSimToolKey() const
{
    return currentSimToolKey();
}

/*!*******************************************************************************************************************
 * \brief Triggers the normal Save action in test mode.
 **********************************************************************************************************************/
void MainWindow::testTriggerSave()
{
    on_actionSave_triggered();
}

/*!*******************************************************************************************************************
 * \brief Sets the Run Python Script line edit path for tests without extra UI interaction.
 *
 * \param path Absolute file path to place into txtRunPythonScript.
 **********************************************************************************************************************/
void MainWindow::testSetRunPythonScriptLinePath(const QString& path)
{
    if (!m_ui || !m_ui->txtRunPythonScript)
        return;

    QSignalBlocker b(m_ui->txtRunPythonScript);
    m_ui->txtRunPythonScript->setText(path);
}

/*!*******************************************************************************************************************
 * \brief Builds Palace run context in test mode and exposes selected fields.
 *
 * \param[out] outError Human-readable error text on failure.
 * \return True on success, false otherwise.
 **********************************************************************************************************************/
bool MainWindow::testBuildPalaceRunContext(QString* outError,
                                           QString* outSimKeyLower,
                                           QString* outModelWin,
                                           QString* outLauncherWin,
                                           int* outRunMode,
                                           QString* outBaseName,
                                           QString* outRunDirGuessWin,
                                           QString* outPalaceRoot,
                                           QString* outDistro,
                                           QString* outPythonCmd,
                                           QString* outPalaceExeLinux,
                                           QString* outModelDirLinux,
                                           QString* outModelLinux)
{
    if (outError)
        *outError = QString();

    PalaceRunContext ctx;
    QString err;
    const bool ok = buildPalaceRunContext(ctx, err);

    if (!ok) {
        if (outError)
            *outError = err;
        return false;
    }

    if (outSimKeyLower)    *outSimKeyLower    = ctx.simKeyLower;
    if (outModelWin)       *outModelWin       = ctx.modelWin;
    if (outLauncherWin)    *outLauncherWin    = ctx.launcherWin;
    if (outRunMode)        *outRunMode        = ctx.runMode;
    if (outBaseName)       *outBaseName       = ctx.baseName;
    if (outRunDirGuessWin) *outRunDirGuessWin = ctx.runDirGuessWin;
    if (outPalaceRoot)     *outPalaceRoot     = ctx.palaceRoot;
    if (outDistro)         *outDistro         = ctx.distro;
    if (outPythonCmd)      *outPythonCmd      = ctx.pythonCmd;
    if (outPalaceExeLinux) *outPalaceExeLinux = ctx.palaceExeLinux;
    if (outModelDirLinux)  *outModelDirLinux  = ctx.modelDirLinux;
    if (outModelLinux)     *outModelLinux     = ctx.modelLinux;

    return true;
}

/*!*******************************************************************************************************************
 * \brief Sets simulation log text directly for tests.
 *
 * \param text Full log text to place into editSimulationLog.
 **********************************************************************************************************************/
void MainWindow::testSetSimulationLogText(const QString& text)
{
    if (!m_ui || !m_ui->editSimulationLog)
        return;

    QSignalBlocker b(m_ui->editSimulationLog);
    m_ui->editSimulationLog->setPlainText(text);
}

/*!*******************************************************************************************************************
 * \brief Exposes detectRunDirFromLog() for tests.
 **********************************************************************************************************************/
QString MainWindow::testDetectRunDirFromLog() const
{
    return detectRunDirFromLog();
}

/*!*******************************************************************************************************************
 * \brief Exposes guessDefaultPalaceRunDir() for tests.
 **********************************************************************************************************************/
QString MainWindow::testGuessDefaultPalaceRunDir(const QString& modelFile,
                                                 const QString& baseName) const
{
    return guessDefaultPalaceRunDir(modelFile, baseName);
}

/*!*******************************************************************************************************************
 * \brief Exposes chooseSearchDir() for tests.
 **********************************************************************************************************************/
QString MainWindow::testChooseSearchDir(const QString& detectedRunDir,
                                        const QString& defaultRunDir) const
{
    return chooseSearchDir(detectedRunDir, defaultRunDir);
}

/*!*******************************************************************************************************************
 * \brief Exposes findPalaceConfigJson() for tests.
 **********************************************************************************************************************/
QString MainWindow::testFindPalaceConfigJson(const QString& runDir) const
{
    return findPalaceConfigJson(runDir);
}

#endif

