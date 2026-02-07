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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "mainwindow.h"
#include "keywordseditor.h"

/*!*******************************************************************************************************************
 * \brief Resolves the absolute path to the keywords CSV/TSV file for a given simulation tool key.
 *
 * The file is expected to live under the application directory:
 *   "<app>/keywords/<simKeyLower>.csv"
 *
 * \param simKeyLower Simulation tool key in lower case ("openems" / "palace").
 * \return Absolute file path to the keywords file.
 **********************************************************************************************************************/
QString MainWindow::resolveKeywordsPath(const QString& simKeyLower) const
{
    const QString base = QCoreApplication::applicationDirPath();
    return QDir(base).filePath(QStringLiteral("keywords/%1.csv").arg(simKeyLower));
}

/*!*******************************************************************************************************************
 * \brief Opens the Keywords Editor dialog for the currently selected simulation tool.
 *
 * Resolves the tool-specific keywords file path and shows KeywordsEditorDialog modally.
 * After the dialog is closed, refreshes the cached keyword tips by re-reading the file
 * from disk (so updates become effective immediately).
 **********************************************************************************************************************/
void MainWindow::on_actionKeywords_triggered()
{
    const QString simKey = currentSimToolKey().toLower();
    if (simKey.isEmpty()) {
        error("No simulation tool selected.");
        return;
    }

    const QString path = resolveKeywordsPath(simKey);
    const QString title = (simKey == "openems")
                              ? tr("Keywords Editor (OpenEMS)")
                              : tr("Keywords Editor (Palace)");

    KeywordsEditorDialog dlg(path, title, this);
    dlg.exec();

    // Reload tips after the editor closes (in case user saved changes).
    refreshKeywordTipsForCurrentTool();
}

/*!*******************************************************************************************************************
 * \brief Loads keyword tips from a CSV/TSV file for the given simulation tool key.
 *
 * Reads "keywords/<tool>.csv" from the application directory and parses it as a two-column table:
 *   <keyword><delimiter><description>
 *
 * Supported delimiters: tab (TSV), semicolon, comma. The delimiter is detected from the first non-empty line.
 * Empty lines are ignored. If a line has no delimiter, it is treated as keyword-only with an empty description.
 *
 * The result is a map: keyword -> description.
 * Duplicate keywords are resolved by keeping the first occurrence (stable, deterministic behaviour).
 *
 * \param simKeyLower Simulation tool key in lower case ("openems" / "palace").
 * \return Map of keyword tips. Empty when the file does not exist or cannot be read.
 **********************************************************************************************************************/
QMap<QString, QString> MainWindow::loadKeywordTipsCsv(const QString& simKeyLower) const
{
    QMap<QString, QString> out;

    const QString path = resolveKeywordsPath(simKeyLower);
    QFile f(path);
    if (!f.exists())
        return out;

    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }

    QTextStream ts(&f);

    const QByteArray head = f.peek(4);
    if (head.startsWith("\xFF\xFE") || head.startsWith("\xFE\xFF")) ts.setCodec("UTF-16");
    else ts.setCodec("UTF-8");

    auto detectDelimiter = [](const QString& line) -> QString {
        if (line.contains('\t')) return "\t";
        if (line.contains(';'))  return ";";
        if (line.contains(','))  return ",";
        return "\t";
    };

    auto splitLine2 = [](const QString& line, const QString& delim) -> QPair<QString, QString> {
        const int idx = line.indexOf(delim);
        if (idx < 0)
            return { line.trimmed(), QString() };

        const QString k = line.left(idx).trimmed();
        const QString d = line.mid(idx + delim.size()).trimmed();
        return { k, d };
    };

    bool firstNonEmpty = true;
    QString delim = "\t";

    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.trimmed().isEmpty())
            continue;

        if (firstNonEmpty) {
            delim = detectDelimiter(line);
            firstNonEmpty = false;
        }

        const auto kv = splitLine2(line, delim);
        const QString key = kv.first;
        const QString val = kv.second;

        if (key.isEmpty())
            continue;

        if (!out.contains(key))
            out.insert(key, val);
    }

    return out;
}

/*!*******************************************************************************************************************
 * \brief Refreshes the cached keyword tips for the currently selected simulation tool.
 *
 * Loads the tips from "keywords/<tool>.csv" and stores them into \c m_keywordTips.
 * This cache is used as a fallback when rebuilding the simulation settings from a parsed Python model.
 *
 * The cache is safe to refresh at startup and whenever the simulation tool selection changes.
 **********************************************************************************************************************/
void MainWindow::refreshKeywordTipsForCurrentTool()
{
    const QString simKey = currentSimToolKey().toLower();
    m_keywordTips = loadKeywordTipsCsv(simKey);
}

/*!*******************************************************************************************************************
 * \brief Merges two tip maps with preference to model-provided tips.
 *
 * Creates a combined map where:
 *  - If a key exists in \a modelTips, it is used (higher priority).
 *  - Otherwise, the value from \a fallbackTips is used.
 *
 * This is intended to apply keyword CSV tips as "defaults", without overriding tips coming from the Python model.
 *
 * \param modelTips Tips parsed from the Python model (higher priority).
 * \param fallbackTips Tips loaded from keywords CSV/TSV (lower priority).
 * \return Combined tips map.
 **********************************************************************************************************************/
QMap<QString, QString> MainWindow::mergeTipsPreferModel(const QMap<QString, QString>& modelTips,
                                                        const QMap<QString, QString>& fallbackTips) const
{
    QMap<QString, QString> out = modelTips;

    for (auto it = fallbackTips.constBegin(); it != fallbackTips.constEnd(); ++it) {
        if (!out.contains(it.key()))
            out.insert(it.key(), it.value());
    }

    return out;
}
