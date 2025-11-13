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
