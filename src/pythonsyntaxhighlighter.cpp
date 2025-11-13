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
