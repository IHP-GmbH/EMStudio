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

#ifndef PYTHONSYNTAXHIGHLIGHTER_H
#define PYTHONSYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QStringList>
#include <QSet>
#include <QVector>

/*!*******************************************************************************************************************
 * \class PythonSyntaxHighlighter
 * \brief Provides syntax highlighting for Python code in QTextDocument-based editors.
 *
 * Implements QSyntaxHighlighter to define and apply formatting rules for:
 *  - Built-in Python keywords (highest priority)
 *  - Extra keywords (e.g., tips from EMStudio keywords CSV) (lower priority)
 *  - Strings
 *  - Comments
 *  - Numbers
 *  - Function names
 *
 * Extra keywords are intended as a "soft" highlight layer: they should not override
 * the built-in Python keyword highlighting.
 *
 * \see QSyntaxHighlighter, QTextCharFormat, QRegularExpression
 **********************************************************************************************************************/
class PythonSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

    struct HighlightingRule
    {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };

public:
    explicit PythonSyntaxHighlighter(QTextDocument *parent = nullptr);

    void                        setExtraKeywords(const QStringList &words);
    QStringList                 extraKeywords() const { return m_extraKeywords; }

protected:
    void                        highlightBlock(const QString &text) override;

private:
    void                        rebuildRules();

    static QRegularExpression   makeWordRegex(const QString &word);

private:
    QVector<HighlightingRule>   highlightingRules;

    QStringList                 m_pythonKeywords;
    QSet<QString>               m_pythonKeywordSet;

    QStringList                 m_extraKeywords;

    QTextCharFormat             keywordFormat;
    QTextCharFormat             extraKeywordFormat;
    QTextCharFormat             stringFormat;
    QTextCharFormat             commentFormat;
    QTextCharFormat             numberFormat;
    QTextCharFormat             functionFormat;
};

#endif // PYTHONSYNTAXHIGHLIGHTER_H
