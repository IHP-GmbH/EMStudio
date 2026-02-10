#ifdef EMSTUDIO_TESTING

#include <QSignalBlocker>
#include <QString>
#include <QVector>
#include <QDebug>

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

#endif

