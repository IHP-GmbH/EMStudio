#include "palacepythonparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

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
PalacePythonParser::Result PalacePythonParser::parseSettings(const QString &filePath)
{
    Result result;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        result.error = QStringLiteral("Cannot open file %1").arg(filePath);
        return result;
    }

    const QString content = QString::fromUtf8(f.readAll());

    QRegularExpression re(
        R"(^\s*(\w+)\s*\[\s*['"]([^'"]+)['"]\s*\]\s*=\s*(.+)$)",
        QRegularExpression::MultilineOption);

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

    QFileInfo fi(filePath);
    const QString scriptDir = fi.absolutePath();
    const QString baseName  = fi.completeBaseName();
    if (!scriptDir.isEmpty() && !baseName.isEmpty())
        result.simPath = QDir(scriptDir).filePath(baseName);

    if (result.settings.isEmpty())
        result.error = QStringLiteral("No settings-like assignments found in %1").arg(filePath);
    else
        result.ok = true;

    return result;
}
