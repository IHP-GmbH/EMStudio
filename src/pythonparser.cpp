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

#include "pythonparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QRegularExpression>

namespace
{

/*!*******************************************************************************************************************
 * \brief Finds a setting key in a map using case-insensitive comparison.
 *
 * Searches through \a settings and returns the actual key name whose
 * case-insensitive value matches \a wanted. This is useful when preserving
 * the original key spelling as it appears in the parsed script.
 *
 * \param settings Map of simulation settings to search.
 * \param wanted   Desired key name (case-insensitive).
 *
 * \return The matching key as stored in \a settings, or an empty string if not found.
 **********************************************************************************************************************/
static QString findKeyCi(const QMap<QString, QVariant>& settings,
                         const QString& wanted)
{
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        if (it.key().compare(wanted, Qt::CaseInsensitive) == 0)
            return it.key();
    }
    return QString();
}

/*!*******************************************************************************************************************
 * \brief Converts a QVariant to a trimmed QString if it holds a string.
 *
 * Returns the contained string value when \a v is of type \c QVariant::String.
 * For all other types, an empty string is returned.
 *
 * \param v Input QVariant.
 *
 * \return Trimmed string value, or an empty string if \a v is not a string.
 **********************************************************************************************************************/
static QString variantToStringIfString(const QVariant& v)
{
    return (v.type() == QVariant::String) ? v.toString().trimmed() : QString();
}

/*!*******************************************************************************************************************
 * \brief Checks whether a string ends with a given suffix (case-insensitive).
 *
 * Performs a case-insensitive comparison to determine whether \a s
 * ends with \a suf.
 *
 * \param s   Input string.
 * \param suf Suffix to test for.
 *
 * \return True if \a s ends with \a suf (case-insensitive), false otherwise.
 **********************************************************************************************************************/
static bool endsWithCi(const QString& s, const QString& suf)
{
    return s.toLower().endsWith(suf);
}

/*!*******************************************************************************************************************
 * \brief Infers the GDS filename from explicit simulation settings.
 *
 * Searches for a setting named \c GdsFile (case-insensitive) and, if present,
 * extracts the GDS filename from its value. Explicit settings have the highest
 * priority and override any previously inferred legacy values.
 *
 * \param result Reference to the parsing result structure to be updated.
 **********************************************************************************************************************/
static void inferExplicitGdsFromSettings(PythonParser::Result& result)
{
    const QString k = findKeyCi(result.settings, QStringLiteral("GdsFile"));
    if (k.isEmpty())
        return;

    const QString s = variantToStringIfString(result.settings.value(k));
    if (!s.isEmpty() && endsWithCi(s, QStringLiteral(".gds"))) {
        result.gdsFilename   = s;
        result.gdsSettingKey = k;
        result.gdsLegacyVar.clear();
    }
}

/*!*******************************************************************************************************************
 * \brief Infers the substrate XML filename from explicit simulation settings.
 *
 * Searches for a setting named \c SubstrateFile (case-insensitive) and, if present,
 * extracts the XML filename from its value. Explicit settings have the highest
 * priority and override any previously inferred legacy values.
 *
 * \param result Reference to the parsing result structure to be updated.
 **********************************************************************************************************************/
static void inferExplicitXmlFromSettings(PythonParser::Result& result)
{
    const QString k = findKeyCi(result.settings, QStringLiteral("SubstrateFile"));
    if (k.isEmpty())
        return;

    const QString s = variantToStringIfString(result.settings.value(k));
    if (!s.isEmpty() && endsWithCi(s, QStringLiteral(".xml"))) {
        result.xmlFilename   = s;
        result.xmlSettingKey = k;
        result.xmlLegacyVar.clear();
    }
}

/*!*******************************************************************************************************************
 * \brief Infers the GDS top-cell name from explicit simulation settings.
 *
 * Searches for a setting named \c gds_cellname (case-insensitive) and assigns
 * its value as the top-level GDS cell name. No heuristic inference is applied;
 * only the explicit setting is considered.
 *
 * \param result Reference to the parsing result structure to be updated.
 **********************************************************************************************************************/
static void inferExplicitCellNameFromSettings(PythonParser::Result& result)
{
    const QString k = findKeyCi(result.settings,
                                QStringLiteral("gds_cellname"));
    if (k.isEmpty())
        return;

    const QString s = variantToStringIfString(result.settings.value(k));
    if (!s.isEmpty())
        result.cellName = s;
}

/*!*******************************************************************************************************************
 * \brief Tries to infer GDS and XML filenames from a single simulation setting.
 *
 * Examines the provided setting value and checks whether it represents a GDS or
 * substrate XML file based on its file extension. Inference is performed only
 * if the corresponding filename has not been set yet.
 *
 * This function never overrides explicitly defined values.
 *
 * \param result Reference to the parsing result structure to be updated.
 * \param key    Setting key name associated with \a value.
 * \param value  Setting value to inspect.
 *
 * \return True if at least one filename was inferred and updated; false otherwise.
 **********************************************************************************************************************/
static bool inferHeuristicFilesFromOneSetting(PythonParser::Result& result,
                                              const QString& key,
                                              const QVariant& value)
{
    const QString s = variantToStringIfString(value);
    if (s.isEmpty())
        return false;

    bool changed = false;

    if (result.gdsFilename.isEmpty() && endsWithCi(s, QStringLiteral(".gds"))) {
        result.gdsFilename   = s;
        result.gdsSettingKey = key;
        result.gdsLegacyVar.clear();
        changed = true;
    }

    if (result.xmlFilename.isEmpty() && endsWithCi(s, QStringLiteral(".xml"))) {
        result.xmlFilename   = s;
        result.xmlSettingKey = key;
        result.xmlLegacyVar.clear();
        changed = true;
    }

    return changed;
}

/*!*******************************************************************************************************************
 * \brief Performs heuristic inference of GDS and XML filenames from all simulation settings.
 *
 * Iterates over all settings in \c result.settings and applies file-extension
 * heuristics to determine missing GDS and XML filenames. The scan terminates
 * early once both filenames have been inferred.
 *
 * Explicitly defined settings are never overridden.
 *
 * \param result Reference to the parsing result structure to be updated.
 **********************************************************************************************************************/
static void inferHeuristicFilesFromAllSettings(PythonParser::Result& result)
{
    for (auto it = result.settings.constBegin(); it != result.settings.constEnd(); ++it) {
        inferHeuristicFilesFromOneSetting(result, it.key(), it.value());

        if (!result.gdsFilename.isEmpty() &&
            !result.xmlFilename.isEmpty())
            break;
    }
}

/*!*******************************************************************************************************************
 * \brief Remove trailing inline '#' comments from a Python expression.
 *
 * Strips comments starting with '#' that are not enclosed in single or double quotes.
 * Quoted '#' characters are preserved.
 *
 * \param s Python expression or value string.
 *
 * \return String with inline comments removed and whitespace trimmed.
 **********************************************************************************************************************/
static QString stripInlineHashComment(QString s)
{
    int hashPos = -1;
    bool inSingle = false;
    bool inDouble = false;

    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == '\'' && !inDouble)
            inSingle = !inSingle;
        else if (c == '\"' && !inSingle)
            inDouble = !inDouble;
        else if (c == '#' && !inSingle && !inDouble) {
            hashPos = i;
            break;
        }
    }

    if (hashPos >= 0)
        s = s.left(hashPos).trimmed();

    return s.trimmed();
}

/*!*******************************************************************************************************************
 * \brief Remove surrounding single or double quotes from a string if present.
 *
 * If the input string is enclosed in matching single or double quotes,
 * the quotes are removed. Otherwise, the string is returned unchanged.
 *
 * \param s Input string.
 *
 * \return Unquoted string or original string if no surrounding quotes are found.
 **********************************************************************************************************************/
static QString unquoteIfQuoted(QString s)
{
    s = s.trimmed();
    if ((s.startsWith('\'') && s.endsWith('\'')) ||
        (s.startsWith('\"') && s.endsWith('\"')))
    {
        return s.mid(1, s.size() - 2);
    }
    return s;
}

/*!*******************************************************************************************************************
 * \brief Parse a Python literal or numeric expression into a QVariant.
 *
 * Handles basic Python literals such as True, False, and None, quoted strings,
 * integer and floating-point numbers (including scientific notation).
 * If the value cannot be interpreted numerically, it is returned as a string.
 *
 * \param valueExprIn String representation of a Python literal or expression.
 *
 * \return QVariant containing the parsed value.
 **********************************************************************************************************************/
static QVariant parsePythonLiteralOrNumber(const QString& valueExprIn)
{
    QString valueExpr = valueExprIn.trimmed();

    if (valueExpr == QLatin1String("True"))
        return true;
    if (valueExpr == QLatin1String("False"))
        return false;
    if (valueExpr == QLatin1String("None"))
        return QVariant();

    if ((valueExpr.startsWith('\'') && valueExpr.endsWith('\'')) ||
        (valueExpr.startsWith('\"') && valueExpr.endsWith('\"')))
    {
        return valueExpr.mid(1, valueExpr.size() - 2);
    }

    bool okInt = false;
    const qlonglong intVal = valueExpr.toLongLong(&okInt, 0);

    bool okDouble = false;
    const double dblVal = valueExpr.toDouble(&okDouble);

    const QString lower = valueExpr.toLower();
    const bool looksFloat = lower.contains('.') || lower.contains('e');

    if (okInt && !looksFloat)
        return intVal;
    if (okDouble)
        return dblVal;

    return valueExpr;
}

/*!*******************************************************************************************************************
 * \brief Parse settings dictionary assignments from a Python script.
 *
 * Extracts assignments of the form:
 *     settings['key'] = value
 *
 * The left-hand variable name is ignored. Parsed key/value pairs are stored
 * in the \c settings map of the result structure.
 *
 * \param content Full Python script text.
 * \param result  Result structure to be filled with parsed settings.
 **********************************************************************************************************************/
static void parseSettingsAssignments(const QString& content, PythonParser::Result& result)
{
    QRegularExpression re(
        R"(^\s*(\w+)\s*\[\s*['"]([^'"]+)['"]\s*\]\s*=\s*(.+)$)",
        QRegularExpression::MultilineOption);

    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString key = m.captured(2);
        QString valueExpr = m.captured(3).trimmed();

        valueExpr = stripInlineHashComment(valueExpr);

        const QVariant value = parsePythonLiteralOrNumber(valueExpr);
        result.settings.insert(key, value);
    }
}

/*!*******************************************************************************************************************
 * \brief Parse legacy top-level file variables (any name) from a Python script.
 *
 * Scans simple assignments of the form:
 *     name = "path/to/file.gds"
 *     name = 'path/to/file.xml'
 *
 * The variable name may be arbitrary. If the assigned string value ends with .gds or .xml
 * (case-insensitive), it is treated as a GDS or substrate XML path candidate.
 *
 * NOTE: These are considered "legacy" file vars and should be overridden by explicit
 * settings['GdsFile']/settings['SubstrateFile'] (or other settings-based inference) if present.
 *
 * \param content Full Python script text.
 * \param result  Result structure to be updated with parsed filenames and variable names.
 **********************************************************************************************************************/
static void parseLegacyFileVars(const QString& content, PythonParser::Result& result)
{
    auto endsWithCi = [](const QString& s, const QString& suf) -> bool {
        return s.trimmed().toLower().endsWith(suf);
    };

    QRegularExpression assignRe(
        R"(^\s*([A-Za-z_]\w*)\s*=\s*(.+)$)",
        QRegularExpression::MultilineOption);

    auto it = assignRe.globalMatch(content);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString varName = m.captured(1).trimmed();
        QString valueExpr = m.captured(2).trimmed();

        valueExpr = stripInlineHashComment(valueExpr);
        valueExpr = unquoteIfQuoted(valueExpr);

        if (valueExpr.isEmpty())
            continue;

        if (result.gdsFilename.trimmed().isEmpty() && endsWithCi(valueExpr, QStringLiteral(".gds"))) {
            result.gdsFilename  = valueExpr;
            result.gdsLegacyVar = varName;
            continue;
        }

        if (result.xmlFilename.trimmed().isEmpty() && endsWithCi(valueExpr, QStringLiteral(".xml"))) {
            result.xmlFilename  = valueExpr;
            result.xmlLegacyVar = varName;
            continue;
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Infer GDS and substrate file paths from parsed settings values.
 *
 * Attempts to determine GDS and XML file paths using the following priority:
 *  - Explicit legacy variables (gds_filename / XML_filename)
 *  - Explicit settings keys (GdsFile / SubstrateFile)
 *  - Fallback: any string-valued setting ending with .gds or .xml
 *
 * Existing explicit filenames are not overwritten by inferred values.
 *
 * \param result Result structure containing parsed settings and filenames.
 **********************************************************************************************************************/
static void inferFilesFromSettings(PythonParser::Result& result)
{
    inferExplicitGdsFromSettings(result);
    inferExplicitXmlFromSettings(result);
    inferExplicitCellNameFromSettings(result);
    inferHeuristicFilesFromAllSettings(result);
}

/*!*******************************************************************************************************************
 * \brief Parse documentation blocks and inline tags associated with settings.
 *
 * Processes special comment annotations such as:
 *     @param, @brief, @details, @default
 *
 * Documentation blocks are associated with the following setting assignment
 * and stored as human-readable tooltips in the result structure.
 *
 * \param content Full Python script text.
 * \param result  Result structure to be filled with setting documentation.
 **********************************************************************************************************************/
static void parseSettingTips(const QString& content, PythonParser::Result& result)
{
    QRegularExpression paramRe  (R"(^\s*#\s*@param\s+([^\s]+).*$)");
    QRegularExpression briefRe  (R"(^\s*#\s*@brief\s*(.*)$)");
    QRegularExpression detailsRe(R"(^\s*#\s*@details\s*(.*)$)");
    QRegularExpression defaultRe(R"(^\s*#\s*@default\s*(.*)$)");

    QRegularExpression assignRe(
        R"(^\s*([A-Za-z_]\w*(?:\.[A-Za-z_]\w*|\s*\[\s*['"][^'"]+['"]\s*\])*)\s*=\s*(.+)$)");

    QRegularExpression inlineTagRe(R"(#\s*@(\w+)\s*(.*)$)");

    QString currentKey;
    QString brief, details, deflt;

    enum class LastSection { None, Brief, Details, Default };
    LastSection last = LastSection::None;

    bool pendingDoc = false;

    auto normalizeKey = [](QString k) -> QString
    {
        k = k.trimmed();

        if (k.contains('.') && !k.contains('[')) {
            const int dot = k.lastIndexOf('.');
            if (dot > 0) {
                const QString lhs = k.left(dot).trimmed();
                const QString rhs = k.mid(dot + 1).trimmed();
                if (!lhs.isEmpty() && !rhs.isEmpty())
                    return QStringLiteral("%1['%2']").arg(lhs, rhs);
            }
        }
        return k;
    };

    auto storeTipForKey = [&](const QString &key, const QString &tipText)
    {
        if (key.trimmed().isEmpty() || tipText.trimmed().isEmpty())
            return;

        result.settingTips.insert(key, tipText);

        QRegularExpression dictKeyRe(R"(^\s*\w+\s*\[\s*['"]([^'"]+)['"]\s*\]\s*$)");
        QRegularExpressionMatch mm = dictKeyRe.match(key);
        if (mm.hasMatch()) {
            const QString alias = mm.captured(1).trimmed();
            if (!alias.isEmpty() && !result.settingTips.contains(alias))
                result.settingTips.insert(alias, tipText);
        }
    };

    auto commit = [&]()
    {
        if (currentKey.isEmpty())
            return;

        QString tip;
        if (!brief.trimmed().isEmpty())
            tip += brief.trimmed();

        if (!details.trimmed().isEmpty()) {
            if (!tip.isEmpty())
                tip += "\n\n";
            tip += details.trimmed();
        }

        if (!deflt.trimmed().isEmpty()) {
            if (!tip.isEmpty())
                tip += "\n\n";
            tip += QStringLiteral("Default: %1").arg(deflt.trimmed());
        }

        storeTipForKey(currentKey, tip);

        currentKey.clear();
        brief.clear();
        details.clear();
        deflt.clear();
        last = LastSection::None;
        pendingDoc = false;
    };

    auto applyInlineTag = [&](const QString &line)
    {
        QRegularExpressionMatch im = inlineTagRe.match(line);
        if (!im.hasMatch())
            return;

        const QString tag = im.captured(1).trimmed().toLower();
        const QString txt = im.captured(2).trimmed();
        if (txt.isEmpty())
            return;

        if (tag == QLatin1String("brief")) {
            brief = txt;
            last = LastSection::Brief;
            pendingDoc = true;
        }
        else if (tag == QLatin1String("details")) {
            details = txt;
            last = LastSection::Details;
            pendingDoc = true;
        }
        else if (tag == QLatin1String("default")) {
            deflt = txt;
            last = LastSection::Default;
            pendingDoc = true;
        }
    };

    const QStringList lines = content.split('\n');
    for (const QString &line : lines)
    {
        const QString t = line.trimmed();

        if (t.startsWith('#'))
        {
            QRegularExpressionMatch pm = paramRe.match(line);
            if (pm.hasMatch()) {
                commit();
                currentKey = normalizeKey(pm.captured(1));
                pendingDoc = true;
                last = LastSection::None;
                continue;
            }

            QRegularExpressionMatch bm = briefRe.match(line);
            if (bm.hasMatch()) {
                brief = bm.captured(1).trimmed();
                last = LastSection::Brief;
                pendingDoc = true;
                continue;
            }

            QRegularExpressionMatch dm = detailsRe.match(line);
            if (dm.hasMatch()) {
                details = dm.captured(1).trimmed();
                last = LastSection::Details;
                pendingDoc = true;
                continue;
            }

            QRegularExpressionMatch dfm = defaultRe.match(line);
            if (dfm.hasMatch()) {
                deflt = dfm.captured(1).trimmed();
                last = LastSection::Default;
                pendingDoc = true;
                continue;
            }

            QString c = t.mid(1).trimmed();
            if (c.startsWith('@'))
                continue;

            if (!pendingDoc)
                continue;

            if (last == LastSection::Details && !details.isEmpty())
                details += "\n" + c;
            else if (last == LastSection::Brief && !brief.isEmpty())
                brief += "\n" + c;
            else if (last == LastSection::Default && !deflt.isEmpty())
                deflt += " " + c;
            else if (!details.isEmpty()) {
                details += "\n" + c;
                last = LastSection::Details;
            }
            else if (!brief.isEmpty()) {
                brief += "\n" + c;
                last = LastSection::Brief;
            }

            continue;
        }

        // non-comment lines
        if (pendingDoc && currentKey.isEmpty())
        {
            QRegularExpressionMatch am = assignRe.match(line);
            if (am.hasMatch()) {
                currentKey = normalizeKey(am.captured(1));
                applyInlineTag(line);
                commit();
                continue;
            }
        }

        if (!currentKey.isEmpty())
        {
            applyInlineTag(line);

            QRegularExpressionMatch am = assignRe.match(line);
            if (am.hasMatch()) {
                commit();
                continue;
            }

            if (!t.isEmpty()) {
                commit();
                continue;
            }
        }

        QRegularExpressionMatch am2 = assignRe.match(line);
        if (am2.hasMatch())
        {
            QRegularExpressionMatch im = inlineTagRe.match(line);
            if (im.hasMatch()) {
                const QString tag = im.captured(1).trimmed().toLower();
                if (tag == QLatin1String("brief") ||
                    tag == QLatin1String("details") ||
                    tag == QLatin1String("default"))
                {
                    currentKey = normalizeKey(am2.captured(1));
                    applyInlineTag(line);
                    commit();
                    continue;
                }
            }
        }
    }

    commit();
}

/*!*******************************************************************************************************************
 * \brief Finalize parsing result and derive auxiliary information.
 *
 * Constructs the simulation path from \a scriptDir and \a baseName if provided,
 * and sets the final status flags and error messages in the result structure.
 *
 * \param scriptDir        Directory where the script is conceptually located.
 * \param baseName         Script base name without extension.
 * \param contextForErrors Context string used to enrich error messages.
 * \param result           Result structure to be finalized.
 **********************************************************************************************************************/
static void finalizeResult(const QString& scriptDir,
                           const QString& baseName,
                           const QString& contextForErrors,
                           PythonParser::Result& result)
{
    if (!scriptDir.isEmpty() && !baseName.isEmpty())
        result.simPath = QDir(scriptDir).filePath(baseName);

    if (result.settings.isEmpty()) {
        if (!contextForErrors.isEmpty()) {
            result.error = QStringLiteral("No settings-like assignments found in %1").arg(contextForErrors);
            result.ok = true;
        }
        else {
            result.error = QStringLiteral("No settings-like assignments found in input text.");
            result.ok = true;
        }
    } else {
        result.ok = true;
    }
}

/*!*******************************************************************************************************************
 * \brief Parse "settings-like" key/value pairs and metadata from a Python script.
 *
 * This function coordinates parsing of settings dictionary assignments,
 * legacy file variables, inferred file paths, and documentation blocks.
 * It also derives auxiliary information such as the simulation output path.
 *
 * \param content          Full Python script text to be parsed.
 * \param scriptDir        Directory where the script is conceptually located (may be empty).
 * \param baseName         Script base name without extension (may be empty).
 * \param contextForErrors Context string used to enrich error messages (may be empty).
 *
 * \return Parsed settings and auxiliary information.
 **********************************************************************************************************************/
PythonParser::Result parseSettingsImpl(const QString &content,
                                       const QString &scriptDir,
                                       const QString &baseName,
                                       const QString &contextForErrors)
{
    PythonParser::Result result;

    parseSettingsAssignments(content, result);
    parseLegacyFileVars(content, result);
    inferFilesFromSettings(result);
    parseSettingTips(content, result);
    finalizeResult(scriptDir, baseName, contextForErrors, result);

    return result;
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Try to parse "settings-like" key/value pairs from Palace Python model file.
 *
 * Looks for lines of the form
 *     something['key'] = value
 * or  something["key"] = value
 *
 * The left-hand variable name (e.g. "settings") is ignored on purpose, so that
 * different variable names still work.
 **********************************************************************************************************************/
PythonParser::Result PythonParser::parseSettings(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Result result;
        result.error = QStringLiteral("Cannot open file %1").arg(filePath);
        return result;
    }

    const QString content = QString::fromUtf8(f.readAll());

    QFileInfo fi(filePath);
    const QString scriptDir = fi.absolutePath();
    const QString baseName  = fi.completeBaseName();

    return parseSettingsImpl(content, scriptDir, baseName, filePath);
}

/*!*******************************************************************************************************************
 * \brief Parse "settings-like" key/value pairs from an in-memory Python script.
 *
 * This overload works on a plain text buffer instead of reading from a file.
 * Optional \a scriptDir and \a baseName are used only to construct \c simPath
 * in the result, if provided.
 *
 * \param content   Full Python script text to be parsed.
 * \param scriptDir Directory where the script is conceptually located (may be empty).
 * \param baseName  Script base name without extension (may be empty).
 *
 * \return Parsed settings and auxiliary information.
 **********************************************************************************************************************/
PythonParser::Result PythonParser::parseSettingsFromText(const QString &content,
                                                         const QString &scriptDir,
                                                         const QString &baseName)
{
    return parseSettingsImpl(content, scriptDir, baseName, QString());
}
