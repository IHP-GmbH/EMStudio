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

#ifndef PYTHONEDITOR_H
#define PYTHONEDITOR_H

#include <QTextEdit>
#include <QCompleter>
#include <QStringListModel>

class FindDialog;
class PythonSyntaxHighlighter;

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
    void                                setEditorFontSize(qreal pt);
    void                                setPlainTextUndoable(const QString &text);
    void                                setExtraHighlightKeywords(const QStringList &words);

signals:
    void                                sigFontSizeChanged(qreal newSize);

public slots:
    void                                openFindDialog();
    void                                findNext(const QString &pat, bool matchCase, bool wholeWords, bool useRegex);
    void                                findPrev(const QString &pat, bool matchCase, bool wholeWords, bool useRegex);
    void                                highlightAll(const QString &pat, bool matchCase, bool wholeWords, bool useRegex);
    void                                clearHighlights();

private slots:
    void                                updateVariableList();

protected:
    void                                wheelEvent(QWheelEvent *e) override;
    void                                keyPressEvent(QKeyEvent *e) override;
    void                                focusInEvent(QFocusEvent *e) override;

private:
    void                                zoomInText();
    void                                zoomOutText();

    QString                             textUnderCursor() const;
    bool                                doFind(const QString &pat, bool forward, bool matchCase, bool wholeWords, bool useRegex, bool wrap = true);
    void                                applyHighlights(const QList<QTextEdit::ExtraSelection> &sel);

private:
    QStringListModel                    *m_model;
    QStringList                         m_keywords;

    QCompleter                          *m_completer = nullptr;

    FindDialog                          *m_find = nullptr;
    QList<QTextEdit::ExtraSelection>    m_searchSelections;

    PythonSyntaxHighlighter*            m_highlighter = nullptr;
};

#endif // PYTHONEDITOR_H
