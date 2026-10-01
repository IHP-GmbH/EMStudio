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
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "keywordseditor.h"

/*!*******************************************************************************************************************
 * \brief Resolves the absolute path to the keywords CSV/TSV file for a given simulation tool key.
 *
 * The file is expected to live under the application directory:
 *   "<app>/keywords/<tool>.csv" with tool openems, palace, elmer_em or elmer_thermal.
 * Elmer tools fall back to palace.csv when their own file is missing (older keywords folder).
 *
 * \param simKeyLower Simulation tool key in lower case.
 * \return Absolute file path to the keywords file.
 **********************************************************************************************************************/
QString MainWindow::resolveKeywordsPath(const QString& simKeyLower) const
{
    QString key = simKeyLower;
    if (key == QLatin1String("elmer"))
        key = QStringLiteral("elmer_em");

    const QDir base(QCoreApplication::applicationDirPath());
    const QString path = base.filePath(QStringLiteral("keywords/%1.csv").arg(key));
    if ((key == QLatin1String("elmer_em") || key == QLatin1String("elmer_thermal"))
        && !QFileInfo::exists(path))
        return base.filePath(QStringLiteral("keywords/palace.csv"));
    return path;
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
                              : (simKey == "elmer" || simKey == "elmer_em")
                              ? tr("Keywords Editor (Elmer EM)")
                              : (simKey == "elmer_thermal")
                              ? tr("Keywords Editor (Elmer Thermal)")
                              : tr("Keywords Editor (Palace)");

    KeywordsEditorDialog dlg(path, title, this);
    dlg.exec();

    // Reload tips after the editor closes (in case user saved changes).
    refreshKeywordTipsForCurrentTool();
}

/*!*******************************************************************************************************************
 * \brief Reads "keywords/<tool>.csv": keyword, description, topic and default value per line.
 *
 * Columns: <keyword><delimiter><description>[<delimiter><topic>[<delimiter><default>[<delimiter><required>]]],
 * required being "yes" for keys the workflow needs.
 * Supported delimiters: tab (TSV), semicolon, comma, detected from the first non-empty line.
 * Empty lines are ignored; a line without a delimiter is a keyword without description.
 * Duplicate keywords keep the first occurrence. File order is kept: it is the topic and key
 * order of the settings grid.
 *
 * \param simKeyLower Simulation tool key in lower case.
 * \return Entries in file order. Empty when the file does not exist or cannot be read.
 **********************************************************************************************************************/
QVector<MainWindow::KeywordEntry> MainWindow::loadKeywordTable(const QString& simKeyLower) const
{
    QVector<KeywordEntry> out;

    QFile f(resolveKeywordsPath(simKeyLower));
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;

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

    QSet<QString> seen;
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
        // Tab files: split all columns. Other delimiters can occur inside descriptions, so they
        // only separate keyword and description (older 2-column files).
        const QStringList cols = (delim == QLatin1String("\t"))
                ? line.split(QLatin1Char('\t'))
                : QStringList{line.section(delim, 0, 0), line.section(delim, 1)};
        KeywordEntry e;
        e.keyword = cols.value(0).trimmed();
        e.description = cols.value(1).trimmed();
        e.topic = cols.value(2).trimmed();
        e.defaultValue = cols.value(3).trimmed();
        const QString req = cols.value(4).trimmed().toLower();
        e.required = (req == QLatin1String("yes") || req == QLatin1String("true")
                      || req == QLatin1String("1") || req == QLatin1String("required"));
        if (e.keyword.isEmpty() || seen.contains(e.keyword))
            continue;
        seen.insert(e.keyword);
        out << e;
    }
    return out;
}

/*!*******************************************************************************************************************
 * \brief Reads keywords/workflow_signatures.csv: function, positional index, parameter, keyword.
 *
 * One file for all tools (a model is parsed before its tool is known). Written by
 * tools/sync_keywords.py from the gds2palace / gds2openEMS sources.
 *
 * \return Workflow parameters; empty when the file is missing.
 **********************************************************************************************************************/
QVector<PythonParser::WorkflowParam> MainWindow::loadWorkflowSignatures() const
{
    QVector<PythonParser::WorkflowParam> out;
    QFile f(QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("keywords/workflow_signatures.csv")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    while (!ts.atEnd()) {
        const QStringList cols = ts.readLine().split(QLatin1Char('\t'));
        if (cols.size() < 4)
            continue;
        bool ok = false;
        PythonParser::WorkflowParam p;
        p.function = cols.at(0).trimmed();
        p.index = cols.at(1).trimmed().toInt(&ok);
        p.param = cols.at(2).trimmed();
        p.keyword = cols.at(3).trimmed();
        if (ok && !p.function.isEmpty() && !p.keyword.isEmpty())
            out << p;
    }
    return out;
}

/*!*******************************************************************************************************************
 * \brief Keyword tooltips for a simulation tool: "Required.", description, default value when known.
 *
 * \param simKeyLower Simulation tool key in lower case.
 * \return Map keyword -> tooltip text. Empty when the file does not exist or cannot be read.
 **********************************************************************************************************************/
QMap<QString, QString> MainWindow::loadKeywordTipsCsv(const QString& simKeyLower) const
{
    QMap<QString, QString> out;
    for (const KeywordEntry &e : loadKeywordTable(simKeyLower)) {
        QString tip = e.description;
        if (e.required)
            tip = tip.isEmpty() ? tr("Required.") : tr("Required. %1").arg(tip);
        if (!e.defaultValue.isEmpty())
            tip += (tip.isEmpty() ? QString() : QStringLiteral("\n"))
                   + tr("Default: %1").arg(e.defaultValue);
        out.insert(e.keyword, tip);
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
    m_keywordTable = loadKeywordTable(simKey);
    m_keywordTips = loadKeywordTipsCsv(simKey);
    PythonParser::setWorkflowSignatures(loadWorkflowSignatures());

    m_ui->editRunPythonScript->setExtraHighlightKeywords(m_keywordTips.keys());
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
