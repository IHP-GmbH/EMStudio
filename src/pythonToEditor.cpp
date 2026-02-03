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
 * \brief Replaces a Palace-style dict assignment with a new value.
 *
 * Searches for lines of the form:
 * \code
 *   settings['key'] = <value>   # optional comment
 * \endcode
 * where the dict variable name is a single identifier, and replaces only the
 * value part with \a pyValue while preserving indentation and an optional comment.
 *
 * \param script  Python script text to be modified in-place.
 * \param key     Dictionary key string to replace.
 * \param pyValue New Python literal/expression to put on the right-hand side.
 **********************************************************************************************************************/
static void replacePalaceDictVar(QString &script, const QString &key, const QString &pyValue)
{
    QRegularExpression re(
        QString(R"(^(\s*\w+\s*\[\s*['"]%1['"]\s*\]\s*=\s*)([^#\n]*?)(\s*#.*)?$)")
            .arg(QRegularExpression::escape(key)),
        QRegularExpression::MultilineOption);

    script.replace(re, QStringLiteral("\\1%1\\3").arg(pyValue));
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

    ensurePortsTableInitializedFromScript(script);

    updateSubLayerNamesAutoCheck();

    const QString portCode = buildPortCodeFromGuiTable();
    if (!portCode.isEmpty())
        replaceOrInsertPortSection(script, portCode);

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
    } else if (simKeyLower == QLatin1String("palace")) {
        applyPalaceSettings(script);
    }
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
    Q_UNUSED(simKeyLower);

    if (keyIsExcludedForEm(key))
        return;

    auto itMode = m_curPythonData.writeMode.constFind(key);
    if (itMode == m_curPythonData.writeMode.constEnd()) {
        info(QString("Python write: skip key '%1' (not found in script)").arg(key), false);
        return;
    }

    const auto mode = itMode.value();

    QString pyValue;
    if (isFilePathSetting(key, val)) {
        pyValue = toPythonQuotedPath(val.toString());
    } else {
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
           key == QLatin1String("RunPythonScript");
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
    const QString simKeyLower = QStringLiteral("palace");

    for (auto it = m_simSettings.constBegin(); it != m_simSettings.constEnd(); ++it) {
        const QString  &key = it.key();
        const QVariant &val = it.value();

        applyOneSettingToScript(script, key, val, simKeyLower);
    }

    applyBoundaries(script, /*alsoTopLevelAssignment=*/false);
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

    // Dict-style: <something>['Boundaries'] = ...
    QRegularExpression reSettings(
        R"(^\s*(\w+)\s*\[\s*['"]Boundaries['"]\s*\]\s*=\s*.*$)",
        QRegularExpression::MultilineOption);

    script.replace(reSettings, QString("\\1['Boundaries'] = %1").arg(bndPython));

    if (alsoTopLevelAssignment) {
        // Top-level: Boundaries = ...
        QRegularExpression reBnd("^Boundaries\\s*=.*$", QRegularExpression::MultilineOption);
        script.replace(reBnd, QString("Boundaries = %1").arg(bndPython));
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
QString MainWindow::makeScriptPathForPython(QString nativePath, const QString &simKeyLower) const
{
#ifdef Q_OS_WIN
    if (simKeyLower == QLatin1String("palace")) {
        if (!QStandardPaths::findExecutable(QStringLiteral("wsl")).isEmpty())
            return toWslPath(nativePath);
    }
#else
    Q_UNUSED(simKeyLower);
#endif
    return nativePath;
}

/*!*******************************************************************************************************************
 * \brief Updates GDS and substrate XML file path variables inside the script.
 *
 * Replaces \c gds_filename and \c XML_filename assignments with values taken from \c m_simSettings
 * (keys: \c GdsFile, \c SubstrateFile). Paths may be converted to WSL form depending on platform/tool.
 *
 * \param script      Python script text to be modified in-place.
 * \param simKeyLower Current simulation tool key in lower-case (e.g. "openems", "palace").
 **********************************************************************************************************************/
void MainWindow::applyGdsAndXmlPaths(QString &script, const QString &simKeyLower)
{
    if (m_simSettings.contains("GdsFile")) {
        QString gdsPath = makeScriptPathForPython(m_simSettings.value("GdsFile").toString(), simKeyLower);

        QRegularExpression re("^gds_filename\\s*=.*$", QRegularExpression::MultilineOption);
        script.replace(re, QStringLiteral("gds_filename = \"%1\"").arg(gdsPath));
    }

    const QString topCell = m_ui->cbxTopCell->currentText().trimmed();
    if (!topCell.isEmpty()) {
        QRegularExpression re(R"(^\s*gds_cellname\s*=.*$)",  QRegularExpression::MultilineOption);
        script.replace(re, QStringLiteral("gds_cellname = \"%1\"").arg(topCell));
    }

    if (m_simSettings.contains("SubstrateFile")) {
        QString xmlPath = makeScriptPathForPython(m_simSettings.value("SubstrateFile").toString(), simKeyLower);

        QRegularExpression re("^XML_filename\\s*=.*$",
                              QRegularExpression::MultilineOption);
        script.replace(re, QStringLiteral("XML_filename = \"%1\"").arg(xmlPath));
    }
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
        return t.startsWith(QStringLiteral("simulation_ports.add_port"));
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

        // Scan forward while lines belong to the port block
        while (scan < script.size()) {
            int lineEnd = script.indexOf('\n', scan);
            if (lineEnd < 0)
                lineEnd = script.size();

            const QString line = script.mid(scan, lineEnd - scan);
            const QString t = line.trimmed();

            if (t.isEmpty() || t.startsWith('#') || isAddPortLine(t)) {
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
void MainWindow::replaceOrInsertPortSection(QString &script, const QString &portCode)
{
    const auto blocks = findPortBlocks(script);

    if (!blocks.isEmpty()) {
        // Delete from the end to keep indices valid
        for (int i = blocks.size() - 1; i >= 1; --i) {
            const int s = blocks[i].first;
            const int e = blocks[i].second;
            script.remove(s, e - s);
        }

        // Replace the first block
        const int s0 = blocks[0].first;
        const int e0 = blocks[0].second;
        script.replace(s0, e0 - s0, portCode);
        return;
    }

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
