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
 * \brief Internal implementation for parsing "settings-like" key/value pairs from Python script text.
 *
 * Shared core used by both parseSettings() and parseSettingsFromText().
 * Optional \a scriptDir and \a baseName are used to construct \c simPath.
 * \a contextForErrors is used only to enrich error messages (e.g. with a file path).
 *
 * \param content          Full Python script text to be parsed.
 * \param scriptDir        Directory where the script is conceptually located (may be empty).
 * \param baseName         Script base name without extension (may be empty).
 * \param contextForErrors Context string for error messages (e.g. file path, may be empty).
 *
 * \return Parsed settings and auxiliary information.
 **********************************************************************************************************************/
PythonParser::Result parseSettingsImpl(const QString &content,
                                       const QString &scriptDir,
                                       const QString &baseName,
                                       const QString &contextForErrors)
{
    PythonParser::Result result;

    QRegularExpression re(
        R"(^\s*(\w+)\s*\[\s*['"]([^'"]+)['"]\s*\]\s*=\s*(.+)$)",
        QRegularExpression::MultilineOption);

    // Tips (Doxygen-like) in Python comments:
    //   # @param settings.unit float
    //   # @brief ...
    //   # @details ...
    //   # @default ...
    //
    // Any subset/order of @brief/@details/@default is allowed after @param.
    QRegularExpression paramRe(R"(^\s*#\s*@param\s+settings\.([A-Za-z_]\w*)\b.*$)");
    QRegularExpression briefRe(R"(^\s*#\s*@brief\s*(.*)$)");
    QRegularExpression detailsRe(R"(^\s*#\s*@details\s*(.*)$)");
    QRegularExpression defaultRe(R"(^\s*#\s*@default\s*(.*)$)");

    // ---------------- parse settings assignments ----------------
    auto it = re.globalMatch(content);
    while (it.hasNext())
    {
        QRegularExpressionMatch m = it.next();
        const QString key = m.captured(2);
        QString valueExpr = m.captured(3).trimmed();

        int hashPos = -1;
        bool inSingle = false;
        bool inDouble = false;
        for (int i = 0; i < valueExpr.size(); ++i)
        {
            const QChar c = valueExpr.at(i);
            if (c == '\'' && !inDouble)
                inSingle = !inSingle;
            else if (c == '\"' && !inSingle)
                inDouble = !inDouble;
            else if (c == '#' && !inSingle && !inDouble)
            {
                hashPos = i;
                break;
            }
        }
        if (hashPos >= 0)
            valueExpr = valueExpr.left(hashPos).trimmed();

        QVariant value;
        if (valueExpr == QLatin1String("True"))
            value = true;
        else if (valueExpr == QLatin1String("False"))
            value = false;
        else if (valueExpr == QLatin1String("None"))
            value = QVariant();
        else if ((valueExpr.startsWith('\'') && valueExpr.endsWith('\'')) ||
                 (valueExpr.startsWith('\"') && valueExpr.endsWith('\"')))
        {
            value = valueExpr.mid(1, valueExpr.size() - 2);
        }
        else
        {
            bool okInt = false;
            qlonglong intVal = valueExpr.toLongLong(&okInt, 0);

            bool okDouble = false;
            double dblVal = valueExpr.toDouble(&okDouble);

            QString lower = valueExpr.toLower();
            const bool looksFloat = lower.contains('.') || lower.contains('e');

            if (okInt && !looksFloat)
                value = intVal;
            else if (okDouble)
                value = dblVal;
            else
                value = valueExpr;
        }

        result.settings.insert(key, value);
    }

    // ---------------- parse filenames (gds_filename / XML_filename) ----------------
    QRegularExpression fileRe(
        R"(^\s*(gds_filename|XML_filename)\s*=\s*(.+)$)",
        QRegularExpression::MultilineOption);

    auto fit = fileRe.globalMatch(content);
    while (fit.hasNext())
    {
        QRegularExpressionMatch m = fit.next();
        QString var = m.captured(1);
        QString valueExpr = m.captured(2).trimmed();

        int hashPos = -1;
        bool inSingle = false;
        bool inDouble = false;
        for (int i = 0; i < valueExpr.size(); ++i)
        {
            const QChar c = valueExpr.at(i);
            if (c == '\'' && !inDouble)
                inSingle = !inSingle;
            else if (c == '\"' && !inSingle)
                inDouble = !inDouble;
            else if (c == '#' && !inSingle && !inDouble)
            {
                hashPos = i;
                break;
            }
        }
        if (hashPos >= 0)
            valueExpr = valueExpr.left(hashPos).trimmed();

        if ((valueExpr.startsWith('\'') && valueExpr.endsWith('\'')) ||
            (valueExpr.startsWith('\"') && valueExpr.endsWith('\"')))
        {
            valueExpr = valueExpr.mid(1, valueExpr.size() - 2);
        }

        if (var == QLatin1String("gds_filename"))
            result.gdsFilename = valueExpr;
        else if (var == QLatin1String("XML_filename"))
            result.xmlFilename = valueExpr;
    }

    // ---------------- parse doc blocks into settingTips ----------------
    QString currentKey;
    QString brief, details, deflt;

    enum class LastSection { None, Brief, Details, Default };
    LastSection last = LastSection::None;

    auto commit = [&]()
    {
        if (currentKey.isEmpty())
            return;

        QString tip;
        if (!brief.trimmed().isEmpty())
            tip += brief.trimmed();

        if (!details.trimmed().isEmpty())
        {
            if (!tip.isEmpty())
                tip += "\n\n";
            tip += details.trimmed();
        }

        if (!deflt.trimmed().isEmpty())
        {
            if (!tip.isEmpty())
                tip += "\n\n";
            tip += QStringLiteral("Default: %1").arg(deflt.trimmed());
        }

        if (!tip.isEmpty())
            result.settingTips.insert(currentKey, tip);

        currentKey.clear();
        brief.clear();
        details.clear();
        deflt.clear();
        last = LastSection::None;
    };

    const QStringList lines = content.split('\n');
    for (const QString &line : lines)
    {
        const QString t = line.trimmed();

        if (t.startsWith('#'))
        {
            // @param starts a new block
            QRegularExpressionMatch pm = paramRe.match(line);
            if (pm.hasMatch())
            {
                commit();
                currentKey = pm.captured(1).trimmed();
                last = LastSection::None;
                continue;
            }

            if (currentKey.isEmpty())
                continue; // ignore doc lines until @param appears

            QRegularExpressionMatch bm = briefRe.match(line);
            if (bm.hasMatch())
            {
                brief = bm.captured(1).trimmed();
                last = LastSection::Brief;
                continue;
            }

            QRegularExpressionMatch dm = detailsRe.match(line);
            if (dm.hasMatch())
            {
                details = dm.captured(1).trimmed();
                last = LastSection::Details;
                continue;
            }

            QRegularExpressionMatch dfm = defaultRe.match(line);
            if (dfm.hasMatch())
            {
                deflt = dfm.captured(1).trimmed();
                last = LastSection::Default;
                continue;
            }

            QString c = t.mid(1).trimmed();
            if (c.startsWith('@'))
                continue;

            if (last == LastSection::Details && !details.isEmpty())
            {
                details += "\n" + c;
            }
            else if (last == LastSection::Brief && !brief.isEmpty())
            {
                brief += "\n" + c;
            }
            else if (last == LastSection::Default && !deflt.isEmpty())
            {
                deflt += " " + c;
            }
            else if (!details.isEmpty())
            {
                details += "\n" + c;
                last = LastSection::Details;
            }
            else if (!brief.isEmpty())
            {
                brief += "\n" + c;
                last = LastSection::Brief;
            }

            continue;
        }

        // Allow empty lines inside doc blocks (do not commit on empty).
        if (!currentKey.isEmpty() && !t.isEmpty())
            commit();
    }

    commit(); // flush at EOF

    // Keep only tips for keys that actually exist in settings
    for (auto itTips = result.settingTips.begin(); itTips != result.settingTips.end(); )
    {
        if (!result.settings.contains(itTips.key()))
            itTips = result.settingTips.erase(itTips);
        else
            ++itTips;
    }

    if (!scriptDir.isEmpty() && !baseName.isEmpty())
        result.simPath = QDir(scriptDir).filePath(baseName);

    if (result.settings.isEmpty())
    {
        if (!contextForErrors.isEmpty())
            result.error = QStringLiteral("No settings-like assignments found in %1").arg(contextForErrors);
        else
            result.error = QStringLiteral("No settings-like assignments found in input text.");
    }
    else
    {
        result.ok = true;
    }

    return result;
}

} // close namespce

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
