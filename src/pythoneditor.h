#ifndef PYTHONEDITOR_H
#define PYTHONEDITOR_H

#include <QTextEdit>
#include <QCompleter>
#include <QStringListModel>

class FindDialog;

/*!*******************************************************************************************************************
 * \class PythonEditor
 * \brief A QTextEdit-derived widget for editing Python code with syntax highlighting and autocompletion.
 *
 * This class integrates a Python syntax highlighter and supports intelligent autocompletion
 * for Python keywords and user-defined variables using QCompleter.
 *
 * Features:
 * - Highlighting of keywords, strings, comments, numbers, and functions.
 * - Dynamic autocompletion as the user types.
 * - Tracking and updating of variables used in the script.
 *
 * Usage:
 *  \code
 *  PythonEditor *editor = new PythonEditor(parent);
 *  \endcode
 **********************************************************************************************************************/
class PythonEditor : public QTextEdit
{
    Q_OBJECT

public:
    explicit                            PythonEditor(QWidget *parent = nullptr);
    void                                setCompleter(QCompleter *completer);
    QCompleter                          *completer() const;

public slots:
    void                                openFindDialog();
    void                                findNext(const QString &pat, bool matchCase, bool wholeWords, bool useRegex);
    void                                findPrev(const QString &pat, bool matchCase, bool wholeWords, bool useRegex);
    void                                highlightAll(const QString &pat, bool matchCase, bool wholeWords, bool useRegex);
    void                                clearHighlights();

private slots:
    void                                updateVariableList();

protected:
    void                                keyPressEvent(QKeyEvent *e) override;
    void                                focusInEvent(QFocusEvent *e) override;

private:
    QString                             textUnderCursor() const;
    bool                                doFind(const QString &pat, bool forward, bool matchCase, bool wholeWords, bool useRegex, bool wrap = true);
    void                                applyHighlights(const QList<QTextEdit::ExtraSelection> &sel);

private:
    QStringListModel                    *m_model;
    QStringList                         m_keywords;

    QCompleter                          *m_completer = nullptr;

    FindDialog                          *m_find = nullptr;
    QList<QTextEdit::ExtraSelection>    m_searchSelections;
};

#endif // PYTHONEDITOR_H
