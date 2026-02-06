#ifdef EMSTUDIO_TESTING

#include <QSignalBlocker>
#include <QString>
#include <QVector>

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

    QSignalBlocker be(m_ui->editRunPythonScript);
    m_ui->editRunPythonScript->setPlainText(script);
    m_ui->editRunPythonScript->document()->setModified(false);

    m_ui->tblPorts->setRowCount(0);
    importPortsFromEditor();

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

QString MainWindow::testEditorText() const
{
    return m_ui && m_ui->editRunPythonScript ? m_ui->editRunPythonScript->toPlainText() : QString();
}

void MainWindow::testSetEditorText(const QString& s)
{
    if (!m_ui || !m_ui->editRunPythonScript)
        return;

    QSignalBlocker b(m_ui->editRunPythonScript);
    m_ui->editRunPythonScript->setPlainText(s);
    m_ui->editRunPythonScript->document()->setModified(true);
}
#endif

