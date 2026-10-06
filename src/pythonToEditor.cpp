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
#include <QDir>
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
#include "layoutfile.h"

#include <algorithm>
#include <cmath>

#include <QRegularExpression>

/*!*******************************************************************************************************************
 * \brief Checks whether a simulation setting represents a file path (GDS or XML).
 *
 * Determines whether the given setting should be treated as a file path rather than
 * a numeric or boolean simulation parameter. This is used to correctly serialize
 * file paths into Python string literals when updating Palace models.
 *
 * The check is based on:
 *  - Known canonical keys (e.g. "GdsFile", "SubstrateFile")
 *  - File extension heuristics (".gds", ".gdsii", ".xml")
 *
 * \param key  Setting key name.
 * \param v    Setting value.
 *
 * \return True if the setting represents a GDS or XML file path; otherwise false.
 **********************************************************************************************************************/
static bool isFilePathSetting(const QString& key, const QVariant& v)
{
    if (v.type() != QVariant::String)
        return false;

    const QString s = v.toString().trimmed();
    if (s.isEmpty())
        return false;

    if (key.compare("GdsFile", Qt::CaseInsensitive) == 0 ||
        key.compare("SubstrateFile", Qt::CaseInsensitive) == 0)
        return true;

    const QString lower = s.toLower();
    return lower.endsWith(".gds") || lower.endsWith(".gdsii") ||
           lower.endsWith(".xml");
}

/*!*******************************************************************************************************************
 * \brief Converts a native file system path into a quoted Python string literal.
 *
 * Escapes backslashes and single quotes so that the resulting string can be safely
 * embedded into a Python script as a file path. The returned value is always wrapped
 * in single quotes.
 *
 * This function does not perform any existence checks and operates purely on the
 * string representation of the path.
 *
 * \param path  Native file system path.
 *
 * \return Python-compatible quoted string representing \a path.
 **********************************************************************************************************************/
static QString toPythonQuotedPath(QString s)
{
    s = QDir::fromNativeSeparators(s);

    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");

    return QString("\"%1\"").arg(s);
}

/*!*******************************************************************************************************************
 * \brief Replaces a top-level Python assignment with a new value.
 *
 * Searches the script for a line of the form:
 * \code
 *   <key> = <value>   # optional comment
 * \endcode
 * and replaces only the value part with \a pyValue while preserving indentation
 * and an optional trailing comment.
 *
 * \param script  Python script text to be modified in-place.
 * \param key     Variable name to replace (left-hand side of assignment).
 * \param pyValue New Python literal/expression to put on the right-hand side.
 **********************************************************************************************************************/
static void replaceTopLevelVar(QString &script, const QString &key, const QString &pyValue)
{
    QRegularExpression reVar(
        QString(R"((?m)^([ \t]*%1\b[ \t]*=[ \t]*)([^#\r\n]*?)([ \t]*#.*)?$)")
            .arg(QRegularExpression::escape(key)));

    script.replace(reVar, QStringLiteral("\\1%1\\3").arg(pyValue));
}

/*!*******************************************************************************************************************
 * \brief Replaces any dict-style Python assignment for a given key with a new value.
 *
 * Searches the script for lines of the form:
 * \code
 *   <dict>['key'] = <value>   # optional comment
 *   <dict>["key"] = <value>   # optional comment
 * \endcode
 * and replaces only the value part with \a pyValue while preserving indentation
 * and an optional trailing comment.
 *
 * This helper is intentionally generic and does not restrict the dict variable name.
 *
 * \param script  Python script text to be modified in-place.
 * \param key     Dictionary key string to replace.
 * \param pyValue New Python literal/expression to put on the right-hand side.
 **********************************************************************************************************************/
static void replaceAnyDictVar(QString &script, const QString &key, const QString &pyValue)
{
    QRegularExpression reDict(
        QString(R"(^(\s*(\w+)\s*\[\s*['"]%1['"]\s*\]\s*=\s*)([^#\n]*?)(\s*#.*)?$)")
            .arg(QRegularExpression::escape(key)),
        QRegularExpression::MultilineOption);

    script.replace(reDict, QStringLiteral("\\1%1\\4").arg(pyValue));
}

/*!*******************************************************************************************************************
 * \brief Converts a text cell of the Simulation Settings grid into a Python right-hand side.
 *
 * A value that was a quoted string literal in the script is written back quoted.
 * Anything else (lists such as \c [10e9], references such as \c settings['fstop'],
 * \c None) is a raw expression and written as typed. For \c fdump a bare value
 * such as \c 10e9 is wrapped into a list and an empty cell means \c [].
 *
 * \param key    Setting key.
 * \param text   Cell text.
 * \param quoted True if the script had a quoted string literal for \a key.
 * \param out    Receives the Python expression.
 * \return False if nothing should be written (empty raw expression).
 **********************************************************************************************************************/
static bool textSettingToPython(const QString &key, const QString &text, bool quoted, QString *out)
{
    if (quoted) {
        QString v = text;
        v.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        v.replace(QLatin1Char('\''), QStringLiteral("\\'"));
        *out = QLatin1Char('\'') + v + QLatin1Char('\'');
        return true;
    }

    QString expr = text.simplified(); // one line, the script edit is line-based
    if (key.compare(QLatin1String("fdump"), Qt::CaseInsensitive) == 0) {
        if (expr.isEmpty())
            expr = QStringLiteral("[]");
        else if (!expr.startsWith(QLatin1Char('[')))
            expr = QLatin1Char('[') + expr + QLatin1Char(']');
    }
    if (expr.isEmpty())
        return false;
    *out = expr;
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

/*!*******************************************************************************************************************
 * \brief Loads the Python simulation script into the editor and updates parameters according to current settings.
 *
 * Loads the script from \a filePath if the editor has no unsaved modifications. Otherwise, the current editor
 * content is used. The script is then updated to reflect the values stored in \c m_simSettings and the port
 * configuration in the GUI, and finally written back to the editor while preserving cursor and scroll position.
 *
 * \param filePath Path to the Python script file.
 **********************************************************************************************************************/
void MainWindow::loadPythonScriptToEditor(const QString &filePath)
{
    QString script = loadOrReusePythonScriptText(filePath);
    if (script.isEmpty() && !m_ui->editRunPythonScript->document()->isModified())
        return; // file read failed, error already shown

    const QString simKeyLower = currentSimToolKey().toLower();

    applySimSettingsToScript(script, simKeyLower);
    applyGdsAndXmlPaths(script, simKeyLower);
    applyVariableOverridesToScript(script);

    if (isElmerThermalKey(simKeyLower)) {
        ensureThermalTableInitializedFromScript(script);
        const QString thermalCode = buildThermalCodeFromGuiTable();
        if (!thermalCode.isEmpty())
            replaceOrInsertThermalSection(script, thermalCode);
    } else {
        ensurePortsTableInitializedFromScript(script);
        updateSubLayerNamesAutoCheck();
        const QString portCode = buildPortCodeFromGuiTable();
        if (!portCode.isEmpty())
            replaceOrInsertPortSection(script, portCode);
    }

    setEditorScriptPreservingState(script);
}

/*!*******************************************************************************************************************
 * \brief Loads a Python script either from the editor (if modified) or from disk.
 *
 * If the editor document has unsaved modifications, returns the current editor text.
 * Otherwise reads the script from \a filePath using UTF-8 via \c readTextFileUtf8().
 *
 * \param filePath Path to the Python script file.
 *
 * \return Script text to work with. Returns an empty string if reading from disk fails.
 **********************************************************************************************************************/
QString MainWindow::loadOrReusePythonScriptText(const QString &filePath)
{
    if (m_ui->editRunPythonScript->document()->isModified())
        return m_ui->editRunPythonScript->toPlainText();

    QString text;
    if (!readTextFileUtf8(filePath, text))
        return QString();
    return text;
}

/*!*******************************************************************************************************************
 * \brief Applies simulation settings replacements to the script depending on the active simulation engine.
 *
 * Selects the appropriate replacement strategy based on \a simKeyLower.
 *
 * \param script      Python script text to be modified in-place.
 * \param simKeyLower Current simulation tool key in lower-case (e.g. "openems", "palace").
 **********************************************************************************************************************/
void MainWindow::applySimSettingsToScript(QString &script, const QString &simKeyLower)
{
    if (simKeyLower == QLatin1String("openems")) {
        applyOpenEmsSettings(script);
    } else if (simKeyLower == QLatin1String("palace") || isElmerFamilyKey(simKeyLower)) {
        applyPalaceSettings(script);
    }
    
    forceStartSimulationOff(script);
}

/*!*******************************************************************************************************************
 * \brief Forces \c start_simulation to False (top-level and settings['start_simulation']).
 *
 * Imported Volker-style scripts sometimes set this True, which would start Palace/Elmer from
 * Python itself instead of EMStudio's Run button. Templates already use False.
 **********************************************************************************************************************/
void MainWindow::forceStartSimulationOff(QString &script) const
{
    if (script.isEmpty())
        return;

    static const QRegularExpression reTop(
        QStringLiteral(R"((?m)^([ \t]*start_simulation[ \t]*=[ \t]*)(\S+)([ \t]*(?:#.*)?)?$)"));
    script.replace(reTop, QStringLiteral("\\1False\\3"));

    static const QRegularExpression reDict(
        QStringLiteral(R"((\w+\s*\[\s*['"]start_simulation['"]\s*\]\s*=\s*)(\S+))"));
    script.replace(reDict, QStringLiteral("\\1False"));
}

/*!*******************************************************************************************************************
 * \brief Converts a QVariant into a Python literal string suitable for embedding into a script.
 *
 * Supported types:
 * - Double   -> formatted with \c 'g' precision 12
 * - Integer  -> decimal
 * - Bool     -> \c True / \c False
 *
 * \param v           Input QVariant.
 * \param outLiteral  Output string receiving the Python literal.
 *
 * \return \c true if conversion succeeded, \c false if the type is unsupported.
 **********************************************************************************************************************/
bool MainWindow::variantToPythonLiteral(const QVariant &v, QString *outLiteral)
{
    if (!outLiteral)
        return false;

    if (v.type() == QVariant::Double) {
        *outLiteral = QString::number(v.toDouble(), 'g', 12);
        return true;
    }

    if (v.type() == QVariant::Int) {
        *outLiteral = QString::number(v.toInt());
        return true;
    }

    if (v.type() == QVariant::LongLong ||
        v.type() == QVariant::UInt ||
        v.type() == QVariant::ULongLong) {
        *outLiteral = QString::number(v.toLongLong());
        return true;
    }

    if (v.type() == QVariant::Bool) {
        *outLiteral = v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
        return true;
    }

    return false;
}

/*!*******************************************************************************************************************
 * \brief Applies OpenEMS-related settings updates to the script.
 *
 * Replaces selected keys from \c m_simSettings both for top-level assignments (\c key = value)
 * and dict-style assignments (\c settings['key'] = value). Also updates the boundaries section.
 *
 * \param script Python script text to be modified in-place.
 **********************************************************************************************************************/
void MainWindow::applyOpenEmsSettings(QString &script)
{
    const QString simKeyLower = QStringLiteral("openems");

    for (auto it = m_simSettings.constBegin(); it != m_simSettings.constEnd(); ++it) {
        applyOneSettingToScript(script, it.key(), it.value(), simKeyLower);
    }

    applyBoundaries(script, /*alsoTopLevelAssignment=*/true);
}

/*!*******************************************************************************************************************
 * \brief Applies a single simulation setting to the Python script using the parser-defined write mode.
 *
 * Updates exactly one setting identified by \a key in the given Python script according to the
 * write policy inferred by \c PythonParser:
 * - \c TopLevel   → replaces a top-level assignment (\c key = value)
 * - \c DictAssign → replaces a dictionary-style assignment (\c someDict['key'] = value)
 *
 * The function:
 * - Skips keys excluded by \c keyIsExcludedForEm()
 * - Skips keys that were not found during parsing (not present in \c writeMode),
 *   emitting an informational message in this case
 * - Converts values to appropriate Python literals (numeric, boolean, or quoted paths)
 *
 * The actual replacement strategy is fully driven by parsed script structure and
 * does not depend on the active simulation backend.
 *
 * \param script       Python script text to be modified in-place.
 * \param key          Simulation setting key to apply.
 * \param val          Value to serialize and write into the script.
 * \param simKeyLower  Current simulation tool key (reserved for future use).
 **********************************************************************************************************************/
void MainWindow::applyOneSettingToScript(QString &script,
                                         const QString &key,
                                         const QVariant &val,
                                         const QString &simKeyLower)
{
    if (keyIsExcludedForEm(key))
        return;

    auto itMode = m_curPythonData.writeMode.constFind(key);
    if (itMode == m_curPythonData.writeMode.constEnd()) {
        //info(QString("Python write: skip key '%1' (not found in script)").arg(key), false);
        return;
    }

    const auto mode = itMode.value();

    Q_UNUSED(simKeyLower);
    const QVariant inScript = m_curPythonData.settings.contains(key)
            ? m_curPythonData.settings.value(key)
            : m_curPythonData.topLevel.value(key);

    QString pyValue;
    // fdump is always a list of frequencies. The Elmer EM grid shows it as a checkbox (a bool), which
    // becomes a non-empty list reusing an already-solved frequency (dumps every solved frequency; same
    // codegen as setupEM). Converted whatever the tool is now: after a switch from Elmer EM the bool
    // may still be in the grid, and gds2palace crashes on fdump = True / False.
    if (key.compare(QLatin1String("fdump"), Qt::CaseInsensitive) == 0
        && val.type() == QVariant::Bool) {
        const QString current = inScript.toString().trimmed();
        const bool scriptEnabled = !current.isEmpty() && current != QLatin1String("[]")
                && current != QLatin1String("None")
                && current.compare(QLatin1String("False"), Qt::CaseInsensitive) != 0;
        if (inScript.isValid() && inScript.type() != QVariant::Bool && scriptEnabled == val.toBool())
            return;   // the script's own list already says the same
        if (val.toBool()) {
            if (m_simSettings.contains(QStringLiteral("fstop")))
                pyValue = QStringLiteral("[settings['fstop']]");
            else
                pyValue = QStringLiteral("[settings['fpoint'][0]]");
        } else {
            pyValue = QStringLiteral("[]");
        }
    } else if (isFilePathSetting(key, val)) {
        pyValue = toPythonQuotedPath(val.toString());
    } else if (val.type() == QVariant::String) {
        // Text cells (lists, expressions, string literals) used to be skipped,
        // so edits were lost when Save re-read the script. Unchanged cells are
        // left alone to keep the script's own formatting.
        if (val.toString().trimmed() == inScript.toString().trimmed())
            return;
        if (!textSettingToPython(key, val.toString(),
                                 m_curPythonData.quotedStrings.contains(key), &pyValue))
            return;
    } else {
        // Numbers and True/False: unchanged values keep the script's own spelling (1e9 stays
        // 1e9, not 1000000000), so Save doesn't rewrite lines the user didn't touch.
        if (inScript.isValid()) {
            // A checkbox never replaces a list, expression or string (e.g. a grid row whose editor
            // type no longer matches the script).
            if (val.type() == QVariant::Bool && inScript.type() != QVariant::Bool) {
                info(tr("Not written: %1 is not True/False in the script (%2), but the grid holds a "
                        "checkbox value.").arg(key, inScript.toString()), false);
                return;
            }
            if (val.type() == QVariant::Bool || inScript.type() == QVariant::Bool) {
                if (val.type() == inScript.type() && val.toBool() == inScript.toBool())
                    return;
            } else {
                bool okA = false;
                bool okB = false;
                const double a = val.toDouble(&okA);
                const double b = inScript.toDouble(&okB);
                if (okA && okB && (a == b || std::abs(a - b) <= 1e-12 * std::max(std::abs(a), std::abs(b))))
                    return;
            }
        }
        if (!variantToPythonLiteral(val, &pyValue))
            return;
    }

    switch (mode) {
    case PythonParser::SettingWriteMode::TopLevel:
        replaceTopLevelVar(script, key, pyValue);
        break;

    case PythonParser::SettingWriteMode::DictAssign:
        replaceAnyDictVar(script, key, pyValue);
        break;

    case PythonParser::SettingWriteMode::Unknown:
        break;
    }
}

/*!*******************************************************************************************************************
 * \brief Checks whether a given setting key should be skipped for Palace parameter replacement.
 *
 * Palace scripts are updated only for scalar/boolean/integer-like values. Complex entries like ports,
 * boundaries and file paths are handled separately.
 *
 * \param key Setting key name.
 *
 * \return \c true if the key must be excluded from generic Palace replacements.
 **********************************************************************************************************************/
bool MainWindow::keyIsExcludedForEm(const QString &key)
{
    return key == QLatin1String("Boundaries") ||
           key == QLatin1String("Ports") ||
           key == QLatin1String("RunDir") ||
           key == QLatin1String("RunPythonScript") ||
           key == QLatin1String("GdsFile") ||
           key == QLatin1String("SubstrateFile") ||
           key == QLatin1String("variable_overrides");  // applyVariableOverridesToScript
}

/*!*******************************************************************************************************************
 * \brief Applies Palace-related settings updates to the script.
 *
 * Iterates over \c m_simSettings and updates dict-style assignments
 * (\c someDict['key'] = value) for supported scalar types, skipping keys handled separately.
 * Also updates boundaries if present.
 *
 * \param script Python script text to be modified in-place.
 **********************************************************************************************************************/
void MainWindow::applyPalaceSettings(QString &script)
{
    const QString simKeyLower = currentSimToolKey().toLower();
    if (simKeyLower.isEmpty())
        return;

    for (auto it = m_simSettings.constBegin(); it != m_simSettings.constEnd(); ++it) {
        const QString  &key = it.key();
        const QVariant &val = it.value();

        applyOneSettingToScript(script, key, val, simKeyLower);
    }

    applyBoundaries(script, /*alsoTopLevelAssignment=*/false);

    if (isElmerThermalKey(simKeyLower))
        applyElmerThermalWorkflowToScript(script);
    else if (isElmerEmKey(simKeyLower) || simKeyLower == QLatin1String("elmer"))
        applyElmerWorkflowToScript(script);
    else if (simKeyLower == QLatin1String("palace"))
        applyPalaceWorkflowToScript(script);
}

/*!*******************************************************************************************************************
 * \brief Patches a gds2palace model script for Palace solver output (run_sim, settings['elmer']=False).
 **********************************************************************************************************************/
void MainWindow::applyPalaceWorkflowToScript(QString &script)
{
    QRegularExpression reElmerKey(R"(\w+\s*\[\s*['"]elmer['"]\s*\]\s*=)");
    if (reElmerKey.match(script).hasMatch())
        replaceAnyDictVar(script, QStringLiteral("elmer"), QStringLiteral("False"));

    script.replace(
        QRegularExpression(R"(utilities\.create_elmer_run_script\s*\(\s*sim_path\s*,\s*settings\s*\))"),
        QStringLiteral("utilities.create_run_script(sim_path)"));

    script.replace(
        QRegularExpression(R"(run_command\s*=\s*\[\s*['"]\./run_elmer['"]\s*\])"),
        QStringLiteral("run_command = ['./run_sim']"));

    QRegularExpression reIterKey(R"(\w+\s*\[\s*['"]iterative['"]\s*\]\s*=)");
    if (reIterKey.match(script).hasMatch())
        replaceAnyDictVar(script, QStringLiteral("iterative"), QStringLiteral("False"));
}

/*!*******************************************************************************************************************
 * \brief Writes current GUI simulation settings into the Python editor buffer.
 **********************************************************************************************************************/
void MainWindow::syncGuiSettingsToPythonEditor()
{
    QString script = m_ui->editRunPythonScript->toPlainText();
    const QString simKey = currentSimToolKey().toLower();

    if (script.trimmed().isEmpty() || simKey.isEmpty())
        return;

    applySimSettingsToScript(script, simKey);
    applyGdsAndXmlPaths(script, simKey);
    applyVariableOverridesToScript(script);
    applyBoundaries(script, simKey == QLatin1String("openems"));

    // Ports / thermal must follow the GUI table on Save (Ctrl+S), otherwise
    // applyPythonScriptFromEditor() reloads stale direction/layers from the script.
    if (isElmerThermalKey(simKey)) {
        const QString thermalCode = buildThermalCodeFromGuiTable();
        if (!thermalCode.isEmpty())
            replaceOrInsertThermalSection(script, thermalCode);
    } else {
        const QString portCode = buildPortCodeFromGuiTable();
        if (!portCode.isEmpty())
            replaceOrInsertPortSection(script, portCode);
    }

    setEditorScriptPreservingState(script);
}

/*!*******************************************************************************************************************
 * \brief Writes GDS / substrate paths from the Main-tab line edits into the Python editor.
 *
 * Used when the user browses (or otherwise changes) paths so the model script stays
 * consistent with the layout viewer — including gds2palace \c settings['GdsFile'].
 **********************************************************************************************************************/
void MainWindow::syncGuiPathsToPythonEditor()
{
    if (!m_ui || !m_ui->editRunPythonScript)
        return;

    QString script = m_ui->editRunPythonScript->toPlainText();
    if (script.trimmed().isEmpty())
        return;

    if (m_ui->txtGdsFile) {
        const QString gds = m_ui->txtGdsFile->text().trimmed();
        if (!gds.isEmpty()) {
            m_simSettings[QStringLiteral("GdsFile")] = gds;
            if (!m_modelGdsKey.isEmpty())
                m_simSettings[m_modelGdsKey] = gds;
        }
    }
    if (m_ui->txtSubstrate) {
        const QString xml = m_ui->txtSubstrate->text().trimmed();
        if (!xml.isEmpty()) {
            m_simSettings[QStringLiteral("SubstrateFile")] = xml;
            if (!m_modelXmlKey.isEmpty())
                m_simSettings[m_modelXmlKey] = xml;
        }
    }

    applyGdsAndXmlPaths(script, currentSimToolKey().toLower());
    setEditorScriptPreservingState(script);
}

/*!*******************************************************************************************************************
 * \brief Patches a gds2palace model script for Elmer solver output (run_elmer, settings['elmer']).
 **********************************************************************************************************************/
void MainWindow::applyElmerWorkflowToScript(QString &script)
{
    QRegularExpression reElmerKey(R"(\w+\s*\[\s*['"]elmer['"]\s*\]\s*=)");
    if (reElmerKey.match(script).hasMatch()) {
        replaceAnyDictVar(script, QStringLiteral("elmer"), QStringLiteral("True"));
    } else {
        QRegularExpression reCreate(
            R"(config_name,\s*data_dir\s*=\s*simulation_setup\.create_(?:palace|elmer)\s*\()");
        const QRegularExpressionMatch m = reCreate.match(script);
        if (m.hasMatch())
            script.insert(m.capturedStart(), QStringLiteral("settings['elmer'] = True\n"));
    }

    QRegularExpression reIterKey(R"(\w+\s*\[\s*['"]iterative['"]\s*\]\s*=)");
    if (reIterKey.match(script).hasMatch()) {
        replaceAnyDictVar(script, QStringLiteral("iterative"), QStringLiteral("True"));
    } else {
        QRegularExpression reElmerLine(R"(settings\s*\[\s*['"]elmer['"]\s*\]\s*=\s*True)");
        const QRegularExpressionMatch m = reElmerLine.match(script);
        if (m.hasMatch())
            script.insert(m.capturedEnd(), QStringLiteral("\nsettings['iterative'] = True"));
    }

    script.replace(
        QRegularExpression(R"(utilities\.create_run_script\s*\(\s*sim_path\s*\))"),
        QStringLiteral("utilities.create_elmer_run_script(sim_path, settings)"));

    script.replace(
        QRegularExpression(R"(run_command\s*=\s*\[\s*['"]\./run_sim['"]\s*\])"),
        QStringLiteral("run_command = ['./run_elmer']"));
}


/*!*******************************************************************************************************************
 * \brief Updates the "Boundaries" assignment in the script from \c m_simSettings.
 *
 * Builds a Python list of six boundary entries in the order: X-, X+, Y-, Y+, Z-, Z+.
 * Updates dict-style boundaries assignment. Optionally updates the top-level \c Boundaries
 * variable assignment when \a alsoTopLevelAssignment is \c true.
 *
 * \param script                Python script text to be modified in-place.
 * \param alsoTopLevelAssignment If \c true, also replaces \c Boundaries = ... assignments.
 **********************************************************************************************************************/
void MainWindow::applyBoundaries(QString &script, bool alsoTopLevelAssignment)
{
    if (!m_simSettings.contains("Boundaries") && !alsoTopLevelAssignment)
        return;

    QStringList bndKeys = {"X-", "X+", "Y-", "Y+", "Z-", "Z+"};
    QStringList bndValues;

    QVariantMap bndMap;
    if (m_simSettings.contains("Boundaries"))
        bndMap = m_simSettings["Boundaries"].toMap();

    for (const QString &key : bndKeys)
        bndValues << bndMap.value(key, "PEC").toString();

    const QString bndPython = QString("['%1']").arg(bndValues.join("', '"));

    // <lhs> = <value>  # comment  -> keep indentation (e.g. inside a sweep loop) and the comment;
    // an unchanged list (same six values) keeps its own spelling.
    auto rewrite = [&](const QString &lhsPattern) {
        const QRegularExpression re(
            QStringLiteral(R"((?m)^([ \t]*%1[ \t]*=[ \t]*)([^#\n]*?)([ \t]*#[^\n]*)?$)").arg(lhsPattern));
        QString out;
        int last = 0;
        QRegularExpressionMatchIterator it = re.globalMatch(script);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QStringList current;
            QRegularExpressionMatchIterator items =
                QRegularExpression(QStringLiteral(R"(['"]([^'"]*)['"])")).globalMatch(m.captured(2));
            while (items.hasNext())
                current << items.next().captured(1).trimmed();
            out += script.mid(last, m.capturedStart() - last);
            out += (current == bndValues) ? m.captured(0)
                                          : m.captured(1) + bndPython + m.captured(3);
            last = m.capturedEnd();
        }
        out += script.mid(last);
        script = out;
    };

    // Dict-style: <something>['Boundaries'] = ...
    rewrite(QStringLiteral(R"(\w+\s*\[\s*['"]Boundaries['"]\s*\])"));

    if (alsoTopLevelAssignment) {
        // Plain variable: Boundaries = ...
        rewrite(QStringLiteral("Boundaries"));
    }
}

/*!*******************************************************************************************************************
 * \brief Converts a native file path to the path form expected inside the Python script.
 *
 * On Windows, when using Palace with WSL available, converts paths to WSL format.
 * On non-Windows platforms, returns the input as-is.
 *
 * \param nativePath  Native OS path.
 * \param simKeyLower Current simulation tool key in lower-case (e.g. "openems", "palace").
 *
 * \return Converted script-friendly path string.
 **********************************************************************************************************************/
#include "wslHelper.h"

QString MainWindow::makeScriptPathForPython(QString nativePath, const QString &simKeyLower) const
{
#ifdef Q_OS_WIN
    if (simKeyLower == QLatin1String("palace") || isElmerFamilyKey(simKeyLower)) {
        if (isWslAvailable())
            return toWslPath(nativePath);
    }
#else
    Q_UNUSED(simKeyLower);
#endif
    // Python string literals: prefer forward slashes (works on Windows too).
    return QDir::fromNativeSeparators(nativePath);
}

/*!*******************************************************************************************************************
 * \brief Writes the selected top cell into the variable the script's read_gds() call uses.
 *
 * - read_gds(..., cellname=<var>): updates the top-level \c <var> = "..." (inserted before
 *   \c gds_filename if the script never defines it). \c gds_cellname lines are rewritten
 *   whole (golden style); other variables keep their trailing comment.
 * - read_gds(..., cellname=settings['key']): updates (or adds) that dict entry.
 * - read_gds(..., cellname="literal"): updates the literal.
 * - read_gds(...) without cellname: writes nothing; the script simulates the GDS top cell
 *   (logged when another cell is selected). No unused cell variable is invented.
 * - No read_gds call found: only existing \c gds_cellname / \c settings['(gds_)cellname'] lines
 *   are updated.
 *
 * \param script  Python script text, modified in place.
 * \param topCell Cell selected in the Top Cell combo box.
 **********************************************************************************************************************/
void MainWindow::applyTopCellToScript(QString &script, const QString &topCell)
{
    auto quoted = [&]() { return QStringLiteral("\"%1\"").arg(topCell); };

    // Only true top-level string assignments; never the call kwarg "cellname=gds_cellname".
    auto replaceTopLevelGdsCellname = [&]() -> bool {
        const QRegularExpression reVar(
            QStringLiteral("(?m)^([ \\t]*)gds_cellname[ \\t]*=[ \\t]*(?:\"[^\"]*\"|'[^']*')[ \\t]*(?:#.*)?$"));
        if (!script.contains(reVar))
            return false;
        // Whole line (golden style), keeping its indentation.
        script.replace(reVar, QStringLiteral("\\1gds_cellname = %1").arg(quoted()));
        return true;
    };
    auto replaceDictCell = [&](const QString &keyPattern) -> bool {
        const QRegularExpression reAssign(
            QStringLiteral(R"((?m)^([ \t]*\w+\s*\[\s*['"]%1['"]\s*\]\s*=\s*)([^\n#]+?)([ \t]*(?:#[^\n]*)?)$)")
                .arg(keyPattern));
        if (!script.contains(reAssign))
            return false;
        script.replace(reAssign, QStringLiteral("\\1%1\\3").arg(quoted()));
        return true;
    };
    // New top-level line before gds_filename (templates keep their cell next to the GDS path).
    auto insertTopLevel = [&](const QString &var) {
        const QString line = QStringLiteral("%1 = %2\n").arg(var, quoted());
        const QRegularExpressionMatch m =
            QRegularExpression(QStringLiteral(R"((?m)^[ \t]*gds_filename\s*=.*$)")).match(script);
        if (m.hasMatch()) {
            script.insert(m.capturedStart(), line);
            return;
        }
        const int call = script.indexOf(QRegularExpression(QStringLiteral(R"(\bread_gds\s*\()")));
        const int lineStart = call < 0 ? 0 : script.lastIndexOf(QLatin1Char('\n'), call) + 1;
        script.insert(lineStart, line);
    };

    const PythonParser::ReadGdsCellRef ref = PythonParser::readGdsCellRef(script);
    if (!ref.found) {
        replaceTopLevelGdsCellname();
        replaceDictCell(QStringLiteral("(?:gds_)?cellname"));
        return;
    }

    if (!ref.variable.isEmpty()) {
        if (ref.variable == QLatin1String("gds_cellname")) {
            if (!replaceTopLevelGdsCellname())
                insertTopLevel(ref.variable);
            return;
        }
        const QRegularExpression reVar(
            QStringLiteral(R"((?m)^([ \t]*%1[ \t]*=[ \t]*)(?:"[^"\n]*"|'[^'\n]*'|None)([^\n]*)$)")
                .arg(QRegularExpression::escape(ref.variable)));
        if (script.contains(reVar)) {
            script.replace(reVar, QStringLiteral("\\1%1\\2").arg(quoted()));
            return;
        }
        // Defined some other way (e.g. from settings): leave it; undefined: add it.
        const QRegularExpression reDefined(
            QStringLiteral(R"((?m)^[ \t]*%1[ \t]*=)").arg(QRegularExpression::escape(ref.variable)));
        if (!script.contains(reDefined))
            insertTopLevel(ref.variable);
        return;
    }

    if (!ref.settingsKey.isEmpty()) {
        if (replaceDictCell(QRegularExpression::escape(ref.settingsKey)))
            return;
        const QRegularExpressionMatch init = QRegularExpression(
                    QStringLiteral(R"((?m)^([ \t]*settings\s*=\s*\{\s*\}[ \t]*)$)")).match(script);
        if (init.hasMatch())
            script.insert(init.capturedEnd(),
                          QStringLiteral("\nsettings['%1'] = %2").arg(ref.settingsKey, quoted()));
        return;
    }

    if (ref.hasLiteral) {
        script.replace(ref.literalStart, ref.literalLength, quoted());
        return;
    }

    // read_gds(...) has no cellname argument: the script always loads the GDS top cell.
    if (!m_gdsTopCell.isEmpty() && topCell != m_gdsTopCell) {
        info(tr("The model's read_gds() call has no cellname argument, so it simulates the GDS "
                "top cell '%1', not the selected '%2'.").arg(m_gdsTopCell, topCell), false);
    }
}

/*!*******************************************************************************************************************
 * \brief Updates GDS and substrate XML file path variables inside the script.
 *
 * Replaces top-level \c gds_filename / \c XML_filename and gds2palace-style
 * \c settings['GdsFile'] / \c settings['SubstrateFile'] (plus model-specific keys).
 * Paths may be converted to WSL form depending on platform/tool.
 *
 * \param script      Python script text to be modified in-place.
 * \param simKeyLower Current simulation tool key in lower-case (e.g. "openems", "palace").
 **********************************************************************************************************************/
void MainWindow::applyGdsAndXmlPaths(QString &script, const QString &simKeyLower)
{
    // Does a path written in the script (maybe relative to the model, or a WSL path) name the same
    // file as the GUI's path? Then the line stays as written (relative paths keep the model movable).
    const QString modelDir = currentPythonScriptPath().isEmpty()
            ? QString() : QFileInfo(currentPythonScriptPath()).absolutePath();
    auto sameFile = [&](const QString &scriptValue, const QString &guiPath) -> bool {
        QString p = fromWslPath(scriptValue.trimmed());
        if (p.isEmpty() || guiPath.trimmed().isEmpty())
            return false;
        if (QFileInfo(p).isRelative()) {
            if (modelDir.isEmpty())
                return false;
            p = QDir(modelDir).filePath(p);
        }
        const QFileInfo a(p);
        const QFileInfo b(guiPath);
        if (a.exists() && b.exists())
            return a.canonicalFilePath() == b.canonicalFilePath();
        return QDir::cleanPath(a.absoluteFilePath()) == QDir::cleanPath(b.absoluteFilePath());
    };
    // <lhs> = "path"  # comment : value replaced (comment kept) unless it is the same file.
    auto replacePath = [&](const QString &lhsPattern, const QString &guiPath, const QString &value) {
        const QRegularExpression re(
            QStringLiteral(R"((?m)^([ \t]*%1[ \t]*=[ \t]*)([^#\n]*?)([ \t]*#[^\n]*)?$)").arg(lhsPattern));
        QString out;
        int last = 0;
        QRegularExpressionMatchIterator it = re.globalMatch(script);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QRegularExpressionMatch quoted =
                QRegularExpression(QStringLiteral(R"(^(['"])(.*)\1$)")).match(m.captured(2).trimmed());
            out += script.mid(last, m.capturedStart() - last);
            out += (quoted.hasMatch() && sameFile(quoted.captured(2), guiPath))
                    ? m.captured(0)
                    : m.captured(1) + QStringLiteral("\"%1\"").arg(value) + m.captured(3);
            last = m.capturedEnd();
        }
        out += script.mid(last);
        script = out;
    };
    auto replaceDictStringAssign = [&](const QString &key, const QString &guiPath, const QString &value) {
        if (key.isEmpty())
            return;
        replacePath(QStringLiteral(R"(\w+\s*\[\s*['"]%1['"]\s*\])").arg(QRegularExpression::escape(key)),
                    guiPath, value);
    };

    if (m_simSettings.contains("GdsFile")) {
        const QString gdsGui = m_simSettings.value("GdsFile").toString();
        const bool layoutIsRoom = isRoomLayoutPath(gdsGui);

        // ROOM in the Layout File field → script keeps ./<layoutStem>.gds beside the model
        // (same as RunDir when it is the model folder); layout_room remembers the ROOM source.
        QString gdsPath;
        QString gdsSameAs = gdsGui;
        if (layoutIsRoom) {
            const QString companion = companionLayoutGdsBesideModel();
            if (!companion.isEmpty()) {
                gdsSameAs = companion;
                // ./name.gds — cwd / RunDir is typically the model directory.
                gdsPath = QStringLiteral("./") + QFileInfo(companion).fileName();
            } else {
                gdsPath = makeScriptPathForPython(gdsGui, simKeyLower);
            }
        } else {
            gdsPath = makeScriptPathForPython(gdsGui, simKeyLower);
        }

        replacePath(QStringLiteral("gds_filename"), gdsSameAs, gdsPath);

        // gds2palace / Elmer: settings['GdsFile'] (and model-specific key if different).
        QStringList gdsKeys{QStringLiteral("GdsFile")};
        if (!m_modelGdsKey.isEmpty())
            gdsKeys << m_modelGdsKey;
        gdsKeys.removeDuplicates();
        for (const QString &k : gdsKeys)
            replaceDictStringAssign(k, gdsSameAs, gdsPath);

        auto ensureLayoutRoomAssign = [&](const QString &value) {
            const QRegularExpression reExisting(
                QStringLiteral(R"((?m)^[ \t]*layout_room[ \t]*=)"));
            if (reExisting.match(script).hasMatch()) {
                // sameFile against ROOM path when present; otherwise force rewrite to "".
                replacePath(QStringLiteral("layout_room"),
                            layoutIsRoom ? gdsGui : QStringLiteral("__clear_layout_room__"),
                            value);
                return;
            }
            if (value.isEmpty())
                return;
            const QRegularExpression reGds(
                QStringLiteral(R"((?m)^[ \t]*gds_filename[ \t]*=[ \t]*[^\n]*$)"));
            const QRegularExpressionMatch m = reGds.match(script);
            const QString line = QStringLiteral("layout_room = \"%1\"\n").arg(value);
            if (m.hasMatch())
                script.insert(m.capturedStart(), line);
            else
                script.prepend(line);
        };

        if (layoutIsRoom) {
            QString roomForScript = QDir::fromNativeSeparators(QFileInfo(gdsGui).absoluteFilePath());
            if (!modelDir.isEmpty()) {
                const QString rel = QDir::fromNativeSeparators(
                    QDir(modelDir).relativeFilePath(QFileInfo(gdsGui).absoluteFilePath()));
                if (!rel.isEmpty())
                    roomForScript = rel;
            }
            roomForScript.replace(QLatin1Char('\\'), QLatin1Char('/'));
            ensureLayoutRoomAssign(roomForScript);
        } else {
            ensureLayoutRoomAssign(QString());
        }
    }

    const QString topCell = m_ui->cbxTopCell->currentText().trimmed();
    if (!topCell.isEmpty())
        applyTopCellToScript(script, topCell);

    if (m_simSettings.contains("SubstrateFile")) {
        const QString xmlGui = m_simSettings.value("SubstrateFile").toString();
        QString xmlPath = makeScriptPathForPython(xmlGui, simKeyLower);

        replacePath(QStringLiteral("XML_filename"), xmlGui, xmlPath);

        QStringList xmlKeys{QStringLiteral("SubstrateFile")};
        if (!m_modelXmlKey.isEmpty())
            xmlKeys << m_modelXmlKey;
        xmlKeys.removeDuplicates();
        for (const QString &k : xmlKeys)
            replaceDictStringAssign(k, xmlGui, xmlPath);
    }
}

/*!*******************************************************************************************************************
 * \brief Returns the index of the first \a sep in \a text outside brackets and string literals, or -1.
 **********************************************************************************************************************/
static int indexOfTopLevel(const QString &text, QChar sep)
{
    int depth = 0;
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            if (c == QLatin1Char('\\'))
                ++i;
            else if (c == quote)
                quote = QChar();
        } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            quote = c;
        } else if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) {
            --depth;
        } else if (c == sep && depth == 0) {
            return i;
        }
    }
    return -1;
}

/*!*******************************************************************************************************************
 * \brief Splits \a text at each \a sep outside brackets and string literals (e.g. a dict body into its entries).
 **********************************************************************************************************************/
static QStringList splitTopLevel(QString text, QChar sep)
{
    QStringList parts;
    for (int i = indexOfTopLevel(text, sep); i >= 0; i = indexOfTopLevel(text, sep)) {
        parts << text.left(i);
        text.remove(0, i + 1);
    }
    parts << text;
    return parts;
}

/*!*******************************************************************************************************************
 * \brief Finds the dict that holds the stackup Variable overrides in a model script.
 *
 * Follows the \c variable_overrides= argument of \c stackup_reader.read_substrate(...): a top-level
 * variable, a \c settings['key'] entry or an inline dict. Without that argument, a top-level
 * \c variable_overrides = {...} (older EMStudio scripts) is used.
 *
 * \param script   Script text.
 * \param start    Set to the offset of the dict literal ('{'), or -1 if there is none.
 * \param length   Set to the dict literal's length incl. braces.
 * \param argument Set to what read_substrate passes (see PythonParser::callArgumentRef).
 **********************************************************************************************************************/
static void findOverridesDict(const QString &script, int *start, int *length,
                              PythonParser::CallArgRef *argument)
{
    *start = -1;
    *length = 0;
    *argument = PythonParser::callArgumentRef(script, QStringLiteral("read_substrate"),
                                              QStringLiteral("variable_overrides"));
    const PythonParser::CallArgRef &ref = *argument;
    if (ref.isDictLiteral) {
        *start = ref.literalStart;
        *length = ref.literalLength;
        return;
    }

    QString lhs;  // regex for the assignment target
    if (!ref.settingsKey.isEmpty())
        lhs = QStringLiteral(R"(\w+\s*\[\s*['"]%1['"]\s*\])").arg(QRegularExpression::escape(ref.settingsKey));
    else if (!ref.variable.isEmpty())
        lhs = QRegularExpression::escape(ref.variable);
    else if (!ref.hasArgument)
        lhs = QStringLiteral("variable_overrides");
    else
        return;  // passed as something EMStudio can't follow

    const QRegularExpression reAssign(QStringLiteral(R"((?m)^[ \t]*%1[ \t]*=[ \t]*\{)").arg(lhs));
    const QRegularExpressionMatch m = reAssign.match(script);
    if (!m.hasMatch())
        return;
    const int open = m.capturedEnd() - 1;
    // Balanced braces: the dict may span several lines.
    int depth = 0;
    QChar quote;
    for (int i = open; i < script.size(); ++i) {
        const QChar c = script.at(i);
        if (!quote.isNull()) {
            if (c == QLatin1Char('\\'))
                ++i;
            else if (c == quote)
                quote = QChar();
        } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            quote = c;
        } else if (c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char('}') && --depth == 0) {
            *start = open;
            *length = i + 1 - open;
            return;
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Writes the stackup Variable overrides from the Substrate tab into the model script.
 *
 * The dict that read_substrate(..., variable_overrides=...) uses is rewritten in place (top-level
 * variable, settings['key'] or inline dict). A script that passes no overrides gets a top-level
 * \c variable_overrides dict and the argument only when the table has entries.
 **********************************************************************************************************************/
void MainWindow::applyVariableOverridesToScript(QString &script)
{
    storeStackupOverridesFromTable();
    const QHash<QString, QVariant> overrides = currentStackupOverrides();

    // Dict literal, keys sorted so unchanged tables give unchanged text.
    QStringList keys = overrides.keys();
    keys.sort();
    QStringList entries;
    for (const QString &key : keys) {
        const QString val = overrides.value(key).toString().trimmed();
        bool okNum = false;
        val.toDouble(&okNum);
        if (okNum && !val.contains(QLatin1Char('\'')) && !val.contains(QLatin1Char('"')))
            entries << QStringLiteral("'%1': %2").arg(key, val);
        else if (m_stackupOverrideExpressions.value(key) == val)
            // Unquoted in the script (a name or expression, e.g. a loop variable): keep it as code.
            entries << QStringLiteral("'%1': %2").arg(key, val);
        else
            entries << QStringLiteral("'%1': '%2'")
                           .arg(key, QString(val).replace(QLatin1Char('\''), QStringLiteral("\\'")));
    }
    const QString dictLit = entries.isEmpty() ? QStringLiteral("{}")
                                              : QStringLiteral("{%1}").arg(entries.join(QStringLiteral(", ")));

    int dictStart = -1;
    int dictLength = 0;
    PythonParser::CallArgRef ref;
    findOverridesDict(script, &dictStart, &dictLength, &ref);

    if (ref.hasArgument) {
        if (dictStart >= 0) {
            if (script.mid(dictStart, dictLength) != dictLit)
                script.replace(dictStart, dictLength, dictLit);
            return;
        }
        if (!ref.settingsKey.isEmpty() || !ref.variable.isEmpty()) {
            // Referenced but not defined as a dict literal: define it before the read_substrate line,
            // unless it is assigned some other way (then it isn't ours to rewrite).
            const QString lhs = !ref.settingsKey.isEmpty()
                    ? QStringLiteral(R"(\w+\s*\[\s*['"]%1['"]\s*\])").arg(QRegularExpression::escape(ref.settingsKey))
                    : QRegularExpression::escape(ref.variable);
            if (script.contains(QRegularExpression(QStringLiteral(R"((?m)^[ \t]*%1[ \t]*=)").arg(lhs))))
                return;
            const QVector<PythonParser::CallSite> calls =
                PythonParser::findCalls(script, QStringLiteral("read_substrate"));
            if (calls.isEmpty())
                return;
            const int lineStart = script.lastIndexOf(QLatin1Char('\n'), calls.first().start) + 1;
            const QString target = !ref.settingsKey.isEmpty()
                    ? QStringLiteral("settings['%1']").arg(ref.settingsKey) : ref.variable;
            script.insert(lineStart, QStringLiteral("%1 = %2\n").arg(target, dictLit));
        }
        return;
    }

    // read_substrate passes no overrides: only add them when there are some.
    if (dictStart >= 0) {
        if (script.mid(dictStart, dictLength) != dictLit)
            script.replace(dictStart, dictLength, dictLit);
    }
    if (entries.isEmpty())
        return;
    if (dictStart < 0) {
        const QString dictLine = QStringLiteral("variable_overrides = %1").arg(dictLit);
        const QRegularExpressionMatch m =
            QRegularExpression(QStringLiteral(R"((?m)^[ \t]*XML_filename\s*=.*$)")).match(script);
        if (m.hasMatch())
            script.insert(m.capturedEnd(), QStringLiteral("\n") + dictLine);
        else {
            const QVector<PythonParser::CallSite> calls =
                PythonParser::findCalls(script, QStringLiteral("read_substrate"));
            const int lineStart = calls.isEmpty()
                    ? 0 : script.lastIndexOf(QLatin1Char('\n'), calls.first().start) + 1;
            script.insert(lineStart, dictLine + QStringLiteral("\n"));
        }
    }
    const QVector<PythonParser::CallSite> calls =
        PythonParser::findCalls(script, QStringLiteral("read_substrate"));
    if (calls.isEmpty())
        return;
    const PythonParser::CallSite &call = calls.first();
    const QString args = call.args.trimmed();
    const QString added = args.isEmpty() ? QStringLiteral("XML_filename, variable_overrides=variable_overrides")
                                         : QStringLiteral(", variable_overrides=variable_overrides");
    // Insert before the closing ')' (after any trailing whitespace of the last argument).
    int pos = call.argsEnd;
    while (pos > call.argsStart && script.at(pos - 1).isSpace())
        --pos;
    script.insert(pos, added);
}

/*!*******************************************************************************************************************
 * \brief Reads the stackup Variable overrides of a model script into m_simSettings.
 *
 * Uses the dict that read_substrate(..., variable_overrides=...) passes (see findOverridesDict).
 **********************************************************************************************************************/
void MainWindow::loadVariableOverridesFromScript(const QString &script)
{
    QVariantMap map;
    m_stackupOverrideExpressions.clear();
    int dictStart = -1;
    int dictLength = 0;
    PythonParser::CallArgRef ref;
    findOverridesDict(script, &dictStart, &dictLength, &ref);
    if (dictStart >= 0) {
        const QString body = script.mid(dictStart + 1, dictLength - 2);
        const QRegularExpression reKey(QStringLiteral(R"(^\s*(['"])([^'"]+)\1\s*$)"));
        for (const QString &entry : splitTopLevel(body, QLatin1Char(','))) {
            const int colon = indexOfTopLevel(entry, QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QRegularExpressionMatch k = reKey.match(entry.left(colon));
            if (!k.hasMatch())
                continue;
            QString val = entry.mid(colon + 1).trimmed();
            if (val.size() >= 2
                && ((val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\'')))
                    || (val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')))))
                val = val.mid(1, val.size() - 2);
            else
                m_stackupOverrideExpressions.insert(k.captured(2), val);
            map.insert(k.captured(2), val);
        }
    }
    m_simSettings[QStringLiteral("StackupVariableOverrides")] = map;
}

/*!*******************************************************************************************************************
 * \brief Ensures that the ports table is initialized, using ports parsed from the script when needed.
 *
 * If the ports table is empty, rebuilds the layer mapping and tries to parse ports from the
 * given \a script text. Parsed ports are appended to the table if any are found.
 *
 * \param script Python script text used as the source for port parsing.
 **********************************************************************************************************************/
void MainWindow::ensurePortsTableInitializedFromScript(const QString &script)
{
    if (m_ui->tblPorts->rowCount() != 0)
        return;

    rebuildLayerMapping();

    const auto parsed = parsePortsFromScript(script);
    if (!parsed.isEmpty())
        appendParsedPortsToTable(parsed);
}

/*!*******************************************************************************************************************
 * \brief Builds a Python code block defining all simulation ports from the GUI ports table.
 *
 * Generates code that creates \c simulation_ports using \c simulation_setup.all_simulation_ports()
 * and adds each port via \c simulation_ports.add_port(simulation_setup.simulation_port(...)).
 * Layer numbers may be mapped to substrate layer names using \c m_gdsToSubName when available.
 *
 * \return Python source code for the ports section, or an empty string if no ports are defined.
 **********************************************************************************************************************/
QString MainWindow::buildPortCodeFromGuiTable() const
{
    if (m_ui->tblPorts->rowCount() == 0)
        return QString();

    auto toLayerName = [&](const QString& s) -> QString {
        bool ok = false;
        const int n = s.toInt(&ok);
        if (ok && m_gdsToSubName.contains(n))
            return m_gdsToSubName.value(n);
        return s;
    };

    auto pyQuote = [](QString s) -> QString {
        s.replace('\\', "\\\\");
        s.replace('\'', "\\'");
        return "'" + s + "'";
    };

    QString portCode;
    portCode += "simulation_ports = simulation_setup.all_simulation_ports()\n";

    for (int row = 0; row < m_ui->tblPorts->rowCount(); ++row) {
        auto* itemNum  = m_ui->tblPorts->item(row, 0);
        auto* itemVolt = m_ui->tblPorts->item(row, 1);
        auto* itemZ0   = m_ui->tblPorts->item(row, 2);

        const QString num  = itemNum  ? itemNum->text().trimmed()  : QString();
        const QString volt = itemVolt ? itemVolt->text().trimmed() : QString();
        const QString z0   = itemZ0   ? itemZ0->text().trimmed()   : QString();

        auto* srcBox  = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 3));
        auto* fromBox = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 4));
        auto* toBox   = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 5));
        auto* dirBox  = qobject_cast<QComboBox*>(m_ui->tblPorts->cellWidget(row, 6));

        const QString srcVal  = srcBox  ? srcBox->currentText().trimmed()  : QString();
        const QString fromVal = fromBox ? fromBox->currentText().trimmed() : QString();
        const QString toVal   = toBox   ? toBox->currentText().trimmed()   : QString();
        QString       dirVal  = dirBox  ? dirBox->currentText().trimmed()  : QString();

        if (dirVal.isEmpty())
            dirVal = QStringLiteral("z");

        QStringList argsList;

        if (!num.isEmpty())
            argsList << QStringLiteral("portnumber=%1").arg(num);
        if (!volt.isEmpty())
            argsList << QStringLiteral("voltage=%1").arg(volt);
        if (!z0.isEmpty())
            argsList << QStringLiteral("port_Z0=%1").arg(z0);

        if (!srcVal.isEmpty()) {
            bool srcIsInt = false;
            const int srcNum = srcVal.toInt(&srcIsInt);
            if (srcIsInt)
                argsList << QStringLiteral("source_layernum=%1").arg(srcNum);
            else
                argsList << QStringLiteral("source_layername=%1").arg(pyQuote(srcVal));
        }

        const QString fromName = toLayerName(fromVal);
        const QString toName   = toLayerName(toVal);

        if (!fromName.isEmpty() && !toName.isEmpty()) {
            argsList << QStringLiteral("from_layername=%1").arg(pyQuote(fromName));
            argsList << QStringLiteral("to_layername=%1").arg(pyQuote(toName));
        } else if (!fromName.isEmpty()) {
            argsList << QStringLiteral("target_layername=%1").arg(pyQuote(fromName));
        } else if (!toName.isEmpty()) {
            argsList << QStringLiteral("target_layername=%1").arg(pyQuote(toName));
        }

        argsList << QStringLiteral("direction=%1").arg(pyQuote(dirVal));

        const QString argsJoined = argsList.join(QStringLiteral(", "));
        portCode += QStringLiteral(
                        "simulation_ports.add_port("
                        "simulation_setup.simulation_port(%1))\n")
                        .arg(argsJoined);
    }

    return portCode;
}

/*!*******************************************************************************************************************
 * \brief Finds all existing "ports sections" inside a Python script.
 *
 * A ports section is defined as a block starting with:
 * \code
 *   simulation_ports = simulation_setup.all_simulation_ports()
 * \endcode
 * followed by any number of empty lines and/or lines starting with
 * \c simulation_ports.add_port(...).
 *
 * \param script Python script text to scan.
 *
 * \return Vector of (start, end) index pairs for each detected block.
 **********************************************************************************************************************/
QVector<QPair<int,int>> MainWindow::findPortBlocks(const QString &script)
{
    auto isAddPortLine = [](const QString& t) -> bool {
        return t.startsWith(QStringLiteral("simulation_ports.add_port"))
            || t.startsWith(QStringLiteral("simulation_ports.add_port("));
    };

    // Count () so Volker-style multiline add_port(...simulation_port(...)) stays in the block.
    auto parenDelta = [](const QString &line) -> int {
        int d = 0;
        for (const QChar c : line) {
            if (c == QLatin1Char('('))
                ++d;
            else if (c == QLatin1Char(')'))
                --d;
        }
        return d;
    };

    QVector<QPair<int,int>> blocks;

    QRegularExpression startRe(
        R"((?m)^[ \t]*simulation_ports\s*=\s*simulation_setup\.all_simulation_ports\(\)\s*(?:#.*)?\r?\n?)");

    int searchPos = 0;
    while (true) {
        QRegularExpressionMatch m = startRe.match(script, searchPos);
        if (!m.hasMatch())
            break;

        const int blockStart = m.capturedStart();
        int scan = m.capturedEnd();
        int parenDepth = 0;

        // Scan forward while lines belong to the port block (incl. wrapped add_port args).
        while (scan < script.size()) {
            int lineEnd = script.indexOf('\n', scan);
            if (lineEnd < 0)
                lineEnd = script.size();

            const QString line = script.mid(scan, lineEnd - scan);
            const QString t = line.trimmed();

            if (parenDepth > 0) {
                // Continuation of a multiline add_port(...) — keep until balanced.
                parenDepth += parenDelta(line);
                if (parenDepth < 0)
                    parenDepth = 0;
                scan = (lineEnd < script.size()) ? (lineEnd + 1) : lineEnd;
                continue;
            }

            if (t.isEmpty() || t.startsWith(QLatin1Char('#'))) {
                scan = (lineEnd < script.size()) ? (lineEnd + 1) : lineEnd;
                continue;
            }

            if (isAddPortLine(t)) {
                parenDepth += parenDelta(line);
                if (parenDepth < 0)
                    parenDepth = 0;
                scan = (lineEnd < script.size()) ? (lineEnd + 1) : lineEnd;
                continue;
            }
            break;
        }

        blocks.push_back({blockStart, scan});
        searchPos = scan;
    }

    return blocks;
}

/*!*******************************************************************************************************************
 * \brief Replaces the first ports section in the script and removes any subsequent duplicate sections.
 *
 * If at least one ports section exists, all but the first are removed and the first one is replaced
 * with \a portCode. If no ports section exists, \a portCode is injected before a "simulation ==="
 * marker if present, otherwise appended at the end of the script.
 *
 * \param script   Python script text to be modified in-place.
 * \param portCode New ports section Python code.
 **********************************************************************************************************************/
void MainWindow::replaceOrInsertPortSection(QString &script, const QString &portCodeIn)
{
    const auto blocks = findPortBlocks(script);
    if (blocks.isEmpty() && portCodeIn.trimmed().isEmpty())
        return;  // nothing to write, nothing to replace
    // No ports: keep the (empty) port list defined, later code uses simulation_ports.
    const QString portCode = portCodeIn.trimmed().isEmpty()
            ? QStringLiteral("simulation_ports = simulation_setup.all_simulation_ports()\n") : portCodeIn;

    // Same ports as in the script: leave the user's formatting, comments and layout alone.
    if (blocks.size() == 1 && portsEqual(parsePortsFromScript(script), parsePortsFromScript(portCode)))
        return;

    if (!blocks.isEmpty()) {
        // Delete from the end to keep indices valid
        for (int i = blocks.size() - 1; i >= 1; --i) {
            const int s = blocks[i].first;
            const int e = blocks[i].second;
            script.remove(s, e - s);
        }

        // Replace the first block. It may be indented (e.g. inside a sweep loop): the new code
        // gets the same indentation. Comment lines inside the block are kept; blank and comment
        // lines after the last add_port(...) are not part of it.
        const int s0 = blocks[0].first;
        int e0 = blocks[0].second;
        const QString indent = QRegularExpression(QStringLiteral(R"(^[ \t]*)"))
                                   .match(script.mid(s0, e0 - s0)).captured(0);
        QStringList blockLines = script.mid(s0, e0 - s0).split(QLatin1Char('\n'));
        while (!blockLines.isEmpty()
               && (blockLines.last().trimmed().isEmpty() || blockLines.last().trimmed().startsWith(QLatin1Char('#')))) {
            e0 -= blockLines.last().size() + 1;
            blockLines.removeLast();
        }
        e0 = qMax(s0, qMin(e0 + 1, blocks[0].second));  // keep the newline of the last statement
        QStringList comments;
        for (const QString &line : blockLines)
            if (line.trimmed().startsWith(QLatin1Char('#')))
                comments << line.trimmed();

        QStringList newLines = portCode.split(QLatin1Char('\n'));
        if (!newLines.isEmpty() && newLines.last().isEmpty())
            newLines.removeLast();
        if (!comments.isEmpty() && !newLines.isEmpty())
            for (int i = comments.size() - 1; i >= 0; --i)
                newLines.insert(1, comments.at(i));  // after "simulation_ports = ..."
        for (QString &line : newLines)
            if (!line.isEmpty())
                line.prepend(indent);
        script.replace(s0, e0 - s0, newLines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
    } else {
        // No section found -> insert before "simulation ===" marker if present, else append
        QRegularExpression simMarker(
            R"(#[^\n]*simulation\s*={3,})",
            QRegularExpression::MultilineOption);
        QRegularExpressionMatch simMatch = simMarker.match(script);

        const QString injected = QStringLiteral("\n\n") + portCode + QStringLiteral("\n");

        if (simMatch.hasMatch()) {
            const int insertPos = simMatch.capturedStart();
            script.insert(insertPos, injected);
        } else {
            script.append(injected);
        }
    }

    // Older builds truncated multiline add_port blocks and left orphaned calls behind.
    // After writing one clean section, drop any add_port(...) still outside it.
    const auto kept = findPortBlocks(script);
    if (kept.isEmpty())
        return;

    const int keepStart = kept[0].first;
    const int keepEnd = kept[0].second;

    QRegularExpression orphanRe(
        R"(simulation_ports\s*\.\s*add_port\s*\(\s*simulation_setup\s*\.\s*simulation_port\s*\(\s*.*?\s*\)\s*\))",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::MultilineOption);

    QVector<QPair<int, int>> orphans;
    auto it = orphanRe.globalMatch(script);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedStart() < keepStart || m.capturedEnd() > keepEnd)
            orphans.push_back({m.capturedStart(), m.capturedEnd()});
    }
    for (int i = orphans.size() - 1; i >= 0; --i) {
        const int s = orphans[i].first;
        int e = orphans[i].second;
        // Eat following newline so we do not leave blank-gap debris.
        if (e < script.size() && (script.at(e) == QLatin1Char('\n')
                                  || script.at(e) == QLatin1Char('\r'))) {
            ++e;
            if (e < script.size() && script.at(e - 1) == QLatin1Char('\r')
                && script.at(e) == QLatin1Char('\n'))
                ++e;
        }
        script.remove(s, e - s);
    }
}

/*!*******************************************************************************************************************
 * \brief True if two port lists describe the same ports (same order, same values).
 *
 * \param a First list.
 * \param b Second list.
 * \return Equality of number, voltage, Z0, source, from / to (target) layer and direction.
 **********************************************************************************************************************/
bool MainWindow::portsEqual(const QVector<PortInfo> &a, const QVector<PortInfo> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        const PortInfo &x = a.at(i);
        const PortInfo &y = b.at(i);
        if (x.portnumber != y.portnumber || !qFuzzyCompare(x.voltage + 1.0, y.voltage + 1.0)
            || !qFuzzyCompare(x.z0, y.z0) || x.sourceLayer.trimmed() != y.sourceLayer.trimmed()
            || x.fromLayer.trimmed() != y.fromLayer.trimmed() || x.toLayer.trimmed() != y.toLayer.trimmed()
            || x.direction.trimmed().compare(y.direction.trimmed(), Qt::CaseInsensitive) != 0)
            return false;
    }
    return true;
}

/*!*******************************************************************************************************************
 * \brief Writes the modified script to the editor while preserving cursor selection and scroll position.
 *
 * Captures current cursor/selection and scrollbar values, sets the editor text with undo support,
 * clears the modified flag, then restores selection and scrollbars within valid bounds.
 *
 * \param script Final Python script text to set in the editor.
 **********************************************************************************************************************/
void MainWindow::setEditorScriptPreservingState(const QString &script)
{
    // Save editor state
    QTextCursor oldCursor = m_ui->editRunPythonScript->textCursor();
    int oldPos    = oldCursor.position();
    int oldAnchor = oldCursor.anchor();

    QScrollBar *vScroll = m_ui->editRunPythonScript->verticalScrollBar();
    QScrollBar *hScroll = m_ui->editRunPythonScript->horizontalScrollBar();
    int oldV = vScroll ? vScroll->value() : 0;
    int oldH = hScroll ? hScroll->value() : 0;

    // Apply new text without triggering extra signals
    QSignalBlocker blocker(m_ui->editRunPythonScript);
    m_ui->editRunPythonScript->setPlainTextUndoable(script);
    m_ui->editRunPythonScript->document()->setModified(false);

    // Restore selection/cursor
    QTextDocument *doc = m_ui->editRunPythonScript->document();
    const int len = doc->characterCount();
    if (len > 0) {
        oldPos    = qBound(0, oldPos,    len - 1);
        oldAnchor = qBound(0, oldAnchor, len - 1);

        QTextCursor newCursor(doc);
        newCursor.setPosition(oldAnchor);
        newCursor.setPosition(oldPos, QTextCursor::KeepAnchor);
        m_ui->editRunPythonScript->setTextCursor(newCursor);
    }

    // Restore scrollbars
    if (vScroll)
        vScroll->setValue(qMin(oldV, vScroll->maximum()));
    if (hScroll)
        hScroll->setValue(qMin(oldH, hScroll->maximum()));
}

/*!*******************************************************************************************************************
 * \brief Name of the settings dict the model assigns at top level (\c X['key'] = ... at column 0).
 *
 * \param script Script text.
 * A model with only \c settings = {} (no entries yet) counts as well.
 *
 * \return Dict name, or empty for models that use loose variables only (Add setting is disabled).
 **********************************************************************************************************************/
QString MainWindow::settingsDictName(const QString &script) const
{
    static const QRegularExpression re(QStringLiteral(R"((?m)^([A-Za-z_]\w*)\[\s*['"]\w+['"]\s*\]\s*=(?!=))"));
    const QString name = re.match(script).captured(1);
    if (!name.isEmpty())
        return name;
    static const QRegularExpression reEmpty(QStringLiteral(R"((?m)^settings\s*=\s*\{\s*\})"));
    return reEmpty.match(script).hasMatch() ? QStringLiteral("settings") : QString();
}

/*!*******************************************************************************************************************
 * \brief Top-level \c dict['key'] = ... statements (column 0), in script order.
 *
 * \param script Script text.
 * \param dict   Settings dict name.
 * \return One entry per statement: key and the statement's [start, end) offsets.
 **********************************************************************************************************************/
QVector<MainWindow::SettingStatement> MainWindow::topLevelSettingStatements(const QString &script,
                                                                           const QString &dict) const
{
    QVector<SettingStatement> out;
    if (dict.isEmpty())
        return out;
    const QRegularExpression re(QStringLiteral(R"((?m)^%1\[\s*(['"])(\w+)\1\s*\]\s*=(?!=))")
                                    .arg(QRegularExpression::escape(dict)));
    QRegularExpressionMatchIterator it = re.globalMatch(script);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out.push_back({m.captured(2), m.capturedStart(), PythonParser::statementEnd(script, m.capturedStart())});
    }
    return out;
}

/*!*******************************************************************************************************************
 * \brief Sort key of a setting in the grid: (topic index, row in the keyword file).
 *
 * \param key Setting key (loose variables are mapped through settingKeyword()).
 * \return Sort key; unknown keys sort last.
 **********************************************************************************************************************/
QPair<int, int> MainWindow::settingSortKey(const QString &key) const
{
    const QString keyword = settingKeyword(key);
    QStringList topics;
    for (int i = 0; i < m_keywordTable.size(); ++i) {
        const KeywordEntry &e = m_keywordTable.at(i);
        const QString topic = e.topic.isEmpty() ? QStringLiteral("Other") : e.topic;
        if (!topics.contains(topic))
            topics << topic;
        if (e.keyword == keyword && !e.topic.isEmpty())
            return qMakePair(int(topics.indexOf(topic)), i);
    }
    return qMakePair(INT_MAX, INT_MAX);
}

/*!*******************************************************************************************************************
 * \brief Inserts \c dict['key'] = value where it fits by topic.
 *
 * After the present key that comes last before it in keyword-file order (topic, then row), else
 * before the first present key, else after \c dict = {}. Keys the keyword file doesn't know go
 * after the last top-level settings line. Only column-0 statements are anchors.
 *
 * \param script  Script text, changed in place.
 * \param key     New key (not yet in the script).
 * \param pyValue Python literal.
 * \return False when the model has no settings dict.
 **********************************************************************************************************************/
bool MainWindow::insertSettingIntoScript(QString &script, const QString &key, const QString &pyValue) const
{
    const QString dict = settingsDictName(script);
    if (dict.isEmpty())
        return false;
    const QVector<SettingStatement> stmts = topLevelSettingStatements(script, dict);
    const QPair<int, int> newKey = settingSortKey(key);
    const bool known = newKey.first != INT_MAX;

    int pos = -1;
    if (known) {
        const SettingStatement *before = nullptr;  // largest sort key below the new one
        const SettingStatement *after = nullptr;   // smallest sort key above the new one
        for (const SettingStatement &s : stmts) {
            const QPair<int, int> k = settingSortKey(s.key);
            if (k.first == INT_MAX)
                continue;
            if (k < newKey) {
                if (!before || !(k < settingSortKey(before->key)))
                    before = &s;
            } else if (!after || k < settingSortKey(after->key)) {
                after = &s;
            }
        }
        if (before)
            pos = before->end;
        else if (after)
            pos = after->start;
    }
    if (pos < 0 && !known && !stmts.isEmpty())
        pos = stmts.last().end;
    if (pos < 0) {
        const QRegularExpressionMatch init = QRegularExpression(
            QStringLiteral(R"((?m)^%1\s*=\s*\{\s*\}[^\n]*$)").arg(QRegularExpression::escape(dict))).match(script);
        if (init.hasMatch())
            pos = PythonParser::statementEnd(script, init.capturedStart());
        else if (!stmts.isEmpty())
            pos = stmts.last().end;
        else
            return false;
    }

    QString line = QStringLiteral("%1['%2'] = %3\n").arg(dict, key, pyValue);
    if (pos > 0 && script.at(pos - 1) != QLatin1Char('\n'))
        line.prepend(QLatin1Char('\n'));
    script.insert(pos, line);
    return true;
}

/*!*******************************************************************************************************************
 * \brief Whether a setting can be removed from the script, and why not.
 *
 * Only a key assigned exactly once, at top level, that the workflow doesn't require.
 *
 * \param script Script text.
 * \param key    Setting key.
 * \param why    Optional reason when it can't.
 * \return True if removeSettingFromScript() would remove it.
 **********************************************************************************************************************/
bool MainWindow::canRemoveSetting(const QString &script, const QString &key, QString *why) const
{
    auto fail = [why](const QString &reason) {
        if (why)
            *why = reason;
        return false;
    };
    const QString keyword = settingKeyword(key);
    for (const KeywordEntry &e : m_keywordTable)
        if (e.keyword == keyword && e.required)
            return fail(tr("Required by the workflow."));
    const QString dict = settingsDictName(script);
    if (dict.isEmpty())
        return fail(tr("The model has no settings dict."));
    const QRegularExpression any(QStringLiteral(R"((?m)^[ \t]*\w+\[\s*['"]%1['"]\s*\]\s*=(?!=))")
                                     .arg(QRegularExpression::escape(key)));
    int count = 0;
    QRegularExpressionMatchIterator it = any.globalMatch(script);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    int topLevel = 0;
    for (const SettingStatement &s : topLevelSettingStatements(script, dict))
        if (s.key == key)
            ++topLevel;
    if (topLevel != 1 || count != 1)
        return fail(tr("Assigned more than once or inside a block; edit it on the Python tab."));
    return true;
}

/*!*******************************************************************************************************************
 * \brief Removes the single top-level assignment of \a key (see canRemoveSetting()).
 *
 * \param script Script text, changed in place.
 * \param key    Setting key.
 * \return True if removed.
 **********************************************************************************************************************/
bool MainWindow::removeSettingFromScript(QString &script, const QString &key) const
{
    if (!canRemoveSetting(script, key, nullptr))
        return false;
    for (const SettingStatement &s : topLevelSettingStatements(script, settingsDictName(script))) {
        if (s.key == key) {
            script.remove(s.start, s.end - s.start);
            return true;
        }
    }
    return false;
}

/*!*******************************************************************************************************************
 * \brief Writes \a pyValue into the existing assignment of \a key (dict entry or top-level variable).
 *
 * \param script  Script text, changed in place.
 * \param key     Setting key known to the last parse (writeMode).
 * \param pyValue Python literal.
 * \return False if the key has no known assignment.
 **********************************************************************************************************************/
bool MainWindow::writeSettingValueToScript(QString &script, const QString &key, const QString &pyValue) const
{
    switch (m_curPythonData.writeMode.value(key, PythonParser::SettingWriteMode::Unknown)) {
    case PythonParser::SettingWriteMode::DictAssign:
        replaceAnyDictVar(script, key, pyValue);
        return true;
    case PythonParser::SettingWriteMode::TopLevel:
        replaceTopLevelVar(script, key, pyValue);
        return true;
    default:
        return false;
    }
}
