#include "pythoneditor.h"
#include "finddialog.h"
#include "pythonsyntaxhighlighter.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QShortcut>
#include <QRegularExpression>
#include <QPalette>

/*!*******************************************************************************************************************
 * \brief Constructs the PythonEditor widget with syntax highlighting and autocompletion.
 *
 * Initializes the text editor with Python syntax highlighting and sets up a completer
 * for Python keywords and user-defined variables.
 *
 * \param parent Parent widget (optional).
 **********************************************************************************************************************/
PythonEditor::PythonEditor(QWidget *parent)
    : QTextEdit(parent)
{
    new PythonSyntaxHighlighter(this->document());

    m_keywords = QStringList()
                 << "and" << "as" << "assert" << "break" << "class" << "continue" << "def" << "del"
                 << "elif" << "else" << "except" << "False" << "finally" << "for" << "from" << "global"
                 << "if" << "import" << "in" << "is" << "lambda" << "None" << "nonlocal" << "not" << "or"
                 << "pass" << "raise" << "return" << "True" << "try" << "while" << "with" << "yield";

    m_model = new QStringListModel(m_keywords, this);
    auto *completer = new QCompleter(m_model, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    setCompleter(completer);

    connect(this, &QTextEdit::textChanged, this, &PythonEditor::updateVariableList);

    new QShortcut(QKeySequence::Find, this, SLOT(openFindDialog()));
    auto *scNext = new QShortcut(QKeySequence(Qt::Key_F3), this);
    connect(scNext, &QShortcut::activated, this, [this]{
        if (!m_find) openFindDialog();
        emit findNext(QString(), false, false, false);
    });

    auto *scPrev = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    connect(scPrev, &QShortcut::activated, this, [this]{
        if (!m_find) openFindDialog();
        emit findPrev(QString(), false, false, false);
    });
}

/*!*******************************************************************************************************************
 * \brief Sets the QCompleter instance to be used for autocompletion.
 *
 * Connects the completer to the text editor and defines how completions are inserted.
 *
 * \param completer Pointer to QCompleter instance.
 **********************************************************************************************************************/
void PythonEditor::setCompleter(QCompleter *completer)
{
    if (m_completer)
        disconnect(m_completer, nullptr, this, nullptr);

    m_completer = completer;

    if (!m_completer)
        return;

    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);

    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, [=](const QString &completion) {
                QTextCursor tc = textCursor();
                int extra = completion.length() - m_completer->completionPrefix().length();
                tc.movePosition(QTextCursor::Left);
                tc.movePosition(QTextCursor::EndOfWord);
                tc.insertText(completion.right(extra));
                setTextCursor(tc);
            });
}

/*!*******************************************************************************************************************
 * \brief Returns the current QCompleter associated with the editor.
 * \return Pointer to QCompleter object.
 **********************************************************************************************************************/
QCompleter *PythonEditor::completer() const
{
    return m_completer;
}

/*!*******************************************************************************************************************
 * \brief Ensures completer is properly reconnected on focus change.
 * \param e Focus event.
 **********************************************************************************************************************/
void PythonEditor::focusInEvent(QFocusEvent *e)
{
    if (m_completer)
        m_completer->setWidget(this);
    QTextEdit::focusInEvent(e);
}

/*!*******************************************************************************************************************
 * \brief Handles key press events and shows autocomplete suggestions when appropriate.
 * \param e Key event.
 **********************************************************************************************************************/
void PythonEditor::keyPressEvent(QKeyEvent *e)
{
    if (m_completer && m_completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    QTextEdit::keyPressEvent(e);

    if (!m_completer)
        return;

    QString completionPrefix = textUnderCursor();

    if (completionPrefix.length() < 2) {
        m_completer->popup()->hide();
        return;
    }

    if (completionPrefix != m_completer->completionPrefix()) {
        m_completer->setCompletionPrefix(completionPrefix);
        m_completer->popup()->setCurrentIndex(
            m_completer->completionModel()->index(0, 0));
    }

    QRect cr = cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr);
}

/*!*******************************************************************************************************************
 * \brief Extracts the word currently under the cursor.
 * \return QString containing the word under cursor.
 **********************************************************************************************************************/
QString PythonEditor::textUnderCursor() const
{
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

/*!*******************************************************************************************************************
 * \brief Scans the editor content and updates the completion list with variables and keywords.
 **********************************************************************************************************************/
void PythonEditor::updateVariableList()
{
    const QString text = this->toPlainText();
    QSet<QString> identifiers;
    QRegularExpression re(R"(\b([A-Za-z_][A-Za-z0-9_]*)\b)");
    QRegularExpressionMatchIterator i = re.globalMatch(text);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString word = match.captured(1);
        if (!m_keywords.contains(word)) {
            identifiers.insert(word);
        }
    }

    QStringList fullList = m_keywords;
    fullList.append(identifiers.values());
    m_model->setStringList(fullList);
}

/*!*******************************************************************************************************************
 * \brief Opens the Find dialog for searching text within the editor.
 *
 * Creates and initializes the FindDialog instance on first invocation, connects all its signals
 * to the corresponding search and highlight slots, and then displays the dialog as a floating tool window.
 **********************************************************************************************************************/
void PythonEditor::openFindDialog()
{
    if (!m_find) {
        m_find = new FindDialog(this);
        m_find->setWindowFlag(Qt::Tool, true);
        m_find->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(m_find, &FindDialog::sigFindNext,  this, &PythonEditor::findNext);
        connect(m_find, &FindDialog::sigFindPrev,  this, &PythonEditor::findPrev);
        connect(m_find, &FindDialog::sigHighlightAll, this, &PythonEditor::highlightAll);
        connect(m_find, &FindDialog::sigClearHighlights, this, &PythonEditor::clearHighlights);
    }
    m_find->show();
    m_find->raise();
    m_find->activateWindow();
}

/*!*******************************************************************************************************************
 * \brief Finds the next occurrence of a given pattern in the text editor.
 *
 * Attempts to locate the next match of the specified pattern according to the search parameters.
 * If no match is found, the search wraps around the document and triggers a system beep.
 *
 * \param pat        Search pattern or text to find. If empty, the current selection or word under cursor is used.
 * \param matchCase  Enables case-sensitive matching if true.
 * \param wholeWords Restricts search to whole words if true.
 * \param useRegex   Interprets the pattern as a regular expression if true.
 **********************************************************************************************************************/
void PythonEditor::findNext(const QString &pat, bool matchCase, bool wholeWords, bool useRegex)
{
    QString pattern = pat;
    if (pattern.isEmpty()) {
        pattern = textCursor().selectedText();
        if (pattern.isEmpty())
            pattern = textUnderCursor();
    }
    if (!doFind(pattern, /*forward*/true, matchCase, wholeWords, useRegex, /*wrap*/true))
        QApplication::beep();
}

/*!*******************************************************************************************************************
 * \brief Finds the previous occurrence of a given pattern in the text editor.
 *
 * Searches backwards through the document using the specified parameters.
 * If no match is found, the search wraps to the end of the document and emits a system beep.
 *
 * \param pat        Search pattern or text to find. If empty, the current selection or word under cursor is used.
 * \param matchCase  Enables case-sensitive matching if true.
 * \param wholeWords Restricts search to whole words if true.
 * \param useRegex   Interprets the pattern as a regular expression if true.
 **********************************************************************************************************************/
void PythonEditor::findPrev(const QString &pat, bool matchCase, bool wholeWords, bool useRegex)
{
    QString pattern = pat;
    if (pattern.isEmpty()) {
        pattern = textCursor().selectedText();
        if (pattern.isEmpty())
            pattern = textUnderCursor();
    }
    if (!doFind(pattern, /*forward*/false, matchCase, wholeWords, useRegex, /*wrap*/true))
        QApplication::beep();
}

/*!*******************************************************************************************************************
 * \brief Performs the actual search operation within the QTextDocument.
 *
 * Searches forward or backward depending on the \a forward flag. Supports regular expressions,
 * whole-word matching, and wrap-around. Updates the text cursor position if a match is found.
 *
 * \param pat        Search string or regular expression pattern.
 * \param forward    Direction of search: true for forward, false for backward.
 * \param matchCase  Enables case-sensitive matching if true.
 * \param wholeWords Restricts search to whole words if true.
 * \param useRegex   Interprets the pattern as a regular expression if true.
 * \param wrap       If true, wraps search around the document after reaching an end.
 * \return True if a match was found, otherwise false.
 **********************************************************************************************************************/
bool PythonEditor::doFind(const QString &pat, bool forward, bool matchCase, bool wholeWords, bool useRegex, bool wrap)
{
    if (pat.isEmpty())
        return false;

    auto flags = QTextDocument::FindFlags{};
    if (!forward) flags |= QTextDocument::FindBackward;
    if (matchCase) flags |= QTextDocument::FindCaseSensitively;
    if (wholeWords) flags |= QTextDocument::FindWholeWords;

    QTextCursor start = textCursor();
    QTextCursor found;

    if (useRegex) {
        QString expr = pat;
        if (wholeWords) expr = QStringLiteral("\\b%1\\b").arg(pat);
        QRegularExpression re(expr,
            matchCase ? QRegularExpression::NoPatternOption
                      : QRegularExpression::CaseInsensitiveOption);
        found = document()->find(re, start, flags);
        if (!found.isNull()) {
            setTextCursor(found);
            return true;
        }
        if (wrap) {
            QTextCursor wrapCur = forward ? QTextCursor(document()->begin())
                                          : QTextCursor(document()->end());
            found = document()->find(re, wrapCur, flags);
        }
    } else {
        found = document()->find(pat, start, flags);
        if (!found.isNull()) {
            setTextCursor(found);
            return true;
        }
        if (wrap) {
            QTextCursor wrapCur = forward ? QTextCursor(document()->begin())
                                          : QTextCursor(document()->end());
            found = document()->find(pat, wrapCur, flags);
        }
    }

    if (!found.isNull()) {
        setTextCursor(found);
        return true;
    }
    return false;
}

/*!*******************************************************************************************************************
 * \brief Highlights all occurrences of a search pattern within the text editor.
 *
 * Applies a background highlight to every match of the given pattern across the entire document.
 * Supports both plain text and regular expression searches, with optional case sensitivity and
 * whole-word matching.
 *
 * \param pat        Search string or regular expression pattern.
 * \param matchCase  Enables case-sensitive matching if true.
 * \param wholeWords Restricts search to whole words if true.
 * \param useRegex   Interprets the pattern as a regular expression if true.
 **********************************************************************************************************************/
void PythonEditor::highlightAll(const QString &pat, bool matchCase, bool wholeWords, bool useRegex)
{
    m_searchSelections.clear();
    if (pat.isEmpty()) { applyHighlights(m_searchSelections); return; }

    auto bg = palette().color(QPalette::Highlight).lighter(130);
    QTextCursor cur(document());
    cur.beginEditBlock();

    auto flags = QTextDocument::FindFlags{};
    if (matchCase)  flags |= QTextDocument::FindCaseSensitively;
    if (wholeWords) flags |= QTextDocument::FindWholeWords;

    QTextCursor found;
    if (useRegex) {
        QString expr = pat;
        if (wholeWords) expr = QStringLiteral("\\b%1\\b").arg(pat);
        QRegularExpression re(expr,
            matchCase ? QRegularExpression::NoPatternOption
                      : QRegularExpression::CaseInsensitiveOption);

        QTextCursor c(document()->begin());
        while (true) {
            found = document()->find(re, c, flags);
            if (found.isNull()) break;
            QTextEdit::ExtraSelection sel;
            sel.cursor = found;
            sel.format.setBackground(bg);
            m_searchSelections.push_back(sel);
            c = found;
        }
    } else {
        QTextCursor c(document()->begin());
        while (true) {
            found = document()->find(pat, c, flags);
            if (found.isNull()) break;
            QTextEdit::ExtraSelection sel;
            sel.cursor = found;
            sel.format.setBackground(bg);
            m_searchSelections.push_back(sel);
            c = found;
        }
    }

    cur.endEditBlock();
    applyHighlights(m_searchSelections);
}

/*!*******************************************************************************************************************
 * \brief Removes all current search highlights from the editor.
 *
 * Clears the internal list of ExtraSelections used for highlighting and refreshes the display.
 **********************************************************************************************************************/
void PythonEditor::clearHighlights()
{
    m_searchSelections.clear();
    applyHighlights(m_searchSelections);
}

/*!*******************************************************************************************************************
 * \brief Applies the provided set of text highlights to the editor.
 *
 * Updates the ExtraSelections of the QTextEdit, combining them with a visual indication
 * of the current cursor line for better readability.
 *
 * \param sel List of ExtraSelection objects representing highlighted text regions.
 **********************************************************************************************************************/
void PythonEditor::applyHighlights(const QList<QTextEdit::ExtraSelection> &sel)
{
    QList<QTextEdit::ExtraSelection> combined = sel;

    QTextEdit::ExtraSelection currentSel;
    currentSel.cursor = textCursor();
    currentSel.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentSel.format.setBackground(palette().color(QPalette::AlternateBase));
    combined.push_back(currentSel);

    setExtraSelections(combined);
}
