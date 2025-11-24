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

#include "pythonsyntaxhighlighter.h"

/*!*******************************************************************************************************************
 * \brief Constructs a Python syntax highlighter for the given QTextDocument.
 *
 * Initializes formatting rules for Python keywords, strings, comments, numbers,
 * and function definitions using regular expressions.
 *
 * \param parent Pointer to the QTextDocument to apply the syntax highlighter to.
 **********************************************************************************************************************/
PythonSyntaxHighlighter::PythonSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    // Keywords
    keywordFormat.setForeground(Qt::blue);
    keywordFormat.setFontWeight(QFont::Bold);
    const QStringList keywords = {
        "and", "as", "assert", "break", "class", "continue", "def", "del",
        "elif", "else", "except", "False", "finally", "for", "from", "global",
        "if", "import", "in", "is", "lambda", "None", "nonlocal", "not", "or",
        "pass", "raise", "return", "True", "try", "while", "with", "yield"
    };
    for (const QString &word : keywords) {
        rule.pattern = QRegularExpression(QString("\\b%1\\b").arg(word));
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    // Strings
    stringFormat.setForeground(Qt::darkGreen);
    rule.pattern = QRegularExpression(R"(["'][^"']*["'])");
    rule.format = stringFormat;
    highlightingRules.append(rule);

    // Comments
    commentFormat.setForeground(Qt::darkGray);
    commentFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("#[^\n]*");
    rule.format = commentFormat;
    highlightingRules.append(rule);

    // Numbers
    numberFormat.setForeground(Qt::darkMagenta);
    rule.pattern = QRegularExpression(R"(\b[0-9]+(\.[0-9]+)?\b)");
    rule.format = numberFormat;
    highlightingRules.append(rule);

    // Function definitions
    functionFormat.setFontItalic(true);
    functionFormat.setForeground(Qt::darkCyan);
    rule.pattern = QRegularExpression(R"(\bdef\s+([A-Za-z_][A-Za-z0-9_]*)\b)");
    rule.format = functionFormat;
    highlightingRules.append(rule);
}

/*!*******************************************************************************************************************
 * \brief Applies syntax highlighting rules to the provided text block.
 *
 * Iterates through all defined highlighting rules and applies formatting to matches
 * in the given block of text.
 *
 * \param text The text block to be syntax highlighted.
 **********************************************************************************************************************/
void PythonSyntaxHighlighter::highlightBlock(const QString &text)
{
    for (const HighlightingRule &rule : qAsConst(highlightingRules)) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
