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

/*!*******************************************************************************************************************
 * \class PythonSyntaxHighlighter
 * \brief Provides syntax highlighting for Python code in QTextDocument-based editors.
 *
 * Implements QSyntaxHighlighter to define and apply formatting rules for:
 * - Keywords
 * - Strings
 * - Comments
 * - Numbers
 * - Function names
 *
 * Useful for enhancing readability of Python scripts in custom code editors.
 *
 * \see QSyntaxHighlighter, QTextCharFormat, QRegularExpression
 **********************************************************************************************************************/
class PythonSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    PythonSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;

    QTextCharFormat keywordFormat;
    QTextCharFormat stringFormat;
    QTextCharFormat commentFormat;
    QTextCharFormat numberFormat;
    QTextCharFormat functionFormat;
};

#endif // PYTHONSYNTAXHIGHLIGHTER_H
