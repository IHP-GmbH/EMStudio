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

    m_pythonKeywords = QStringList{
        "and", "as", "assert", "break", "class", "continue", "def", "del",
        "elif", "else", "except", "False", "finally", "for", "from", "global",
        "if", "import", "in", "is", "lambda", "None", "nonlocal", "not", "or",
        "pass", "raise", "return", "True", "try", "while", "with", "yield"
    };

    for (const QString& k : m_pythonKeywords) {
        m_pythonKeywordSet.insert(k);
    }

    keywordFormat.setForeground(Qt::blue);
    //keywordFormat.setFontWeight(QFont::Bold);

    extraKeywordFormat.setForeground(Qt::black);
    extraKeywordFormat.setFontWeight(QFont::DemiBold);

    stringFormat.setForeground(Qt::darkGreen);

    commentFormat.setForeground(Qt::darkGray);
    commentFormat.setFontItalic(true);

    numberFormat.setForeground(Qt::darkMagenta);

    functionFormat.setForeground(Qt::darkCyan);
    functionFormat.setFontItalic(true);

    rebuildRules();
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

/*!*******************************************************************************************************************
 * \brief Sets extra user-defined keywords to highlight (e.g. tips keywords).
 *
 * These keywords are highlighted with lower priority than built-in Python keywords.
 * Calling this function rebuilds internal rules and triggers rehighlight().
 *
 * \param words List of extra keywords.
 **********************************************************************************************************************/
void PythonSyntaxHighlighter::setExtraKeywords(const QStringList& words)
{
    m_extraKeywords = words;
    rebuildRules();
    rehighlight();
}

/*!*******************************************************************************************************************
 * \brief Helper: builds a word-boundary regex for a single identifier.
 **********************************************************************************************************************/
QRegularExpression PythonSyntaxHighlighter::makeWordRegex(const QString& word)
{
    return QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word)));
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the list of highlighting rules in the correct priority order.
 *
 * Priority (later rules overwrite earlier if they overlap):
 *  1) strings/comments (so they can override keyword highlighting inside them)
 *  2) extra keywords (lower priority than Python keywords)
 *  3) Python keywords (highest among keywords)
 *  4) numbers, function defs, etc. (you can reorder if you want different behavior)
 **********************************************************************************************************************/
void PythonSyntaxHighlighter::rebuildRules()
{
    highlightingRules.clear();
    HighlightingRule rule;

    rule.pattern = QRegularExpression(R"(["'][^"']*["'])");
    rule.format  = stringFormat;
    highlightingRules.append(rule);

    // Comments
    rule.pattern = QRegularExpression("#[^\n]*");
    rule.format  = commentFormat;
    highlightingRules.append(rule);

    for (const QString& w0 : m_extraKeywords) {
        const QString w = w0.trimmed();
        if (w.isEmpty())
            continue;

        if (m_pythonKeywordSet.contains(w))
            continue;

        rule.pattern = makeWordRegex(w);
        rule.format  = extraKeywordFormat;
        highlightingRules.append(rule);
    }

    // Python keywords
    for (const QString& word : m_pythonKeywords) {
        rule.pattern = makeWordRegex(word);
        rule.format  = keywordFormat;
        highlightingRules.append(rule);
    }

    // Numbers
    rule.pattern = QRegularExpression(R"(\b[0-9]+(\.[0-9]+)?\b)");
    rule.format  = numberFormat;
    highlightingRules.append(rule);

    // Function definitions
    rule.pattern = QRegularExpression(R"(\bdef\s+([A-Za-z_][A-Za-z0-9_]*)\b)");
    rule.format  = functionFormat;
    highlightingRules.append(rule);
}

