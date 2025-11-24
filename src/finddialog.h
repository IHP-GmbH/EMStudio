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

#ifndef FINDDIALOG_H
#define FINDDIALOG_H

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QPushButton;

/*!*******************************************************************************************************************
 * \class FindDialog
 * \brief A non-modal dialog for searching and highlighting text within the PythonEditor.
 *
 * The FindDialog provides an interactive interface for text search operations, allowing users
 * to locate and highlight occurrences of specific patterns in the editor. It supports:
 * - Case-sensitive and whole-word searches.
 * - Regular expression matching.
 * - Navigation to the next and previous matches.
 * - Highlighting or clearing all matches in the document.
 *
 * The dialog emits corresponding signals when search actions are triggered, enabling seamless
 * integration with the PythonEditor for find and highlight functionality.
 *
 * Usage example:
 * \code
 * FindDialog *dlg = new FindDialog(this);
 * connect(dlg, &FindDialog::sigFindNext, editor, &PythonEditor::findNext);
 * dlg->show();
 * \endcode
 **********************************************************************************************************************/
class FindDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FindDialog(QWidget *parent = nullptr);

signals:
    void sigFindNext(const QString &pattern, bool matchCase, bool wholeWords, bool useRegex);
    void sigFindPrev(const QString &pattern, bool matchCase, bool wholeWords, bool useRegex);
    void sigHighlightAll(const QString &pattern, bool matchCase, bool wholeWords, bool useRegex);
    void sigClearHighlights();

private:
    QLineEdit   *m_edit;
    QCheckBox   *m_case;
    QCheckBox   *m_whole;
    QCheckBox   *m_regex;
    QPushButton *m_btnNext;
    QPushButton *m_btnPrev;
    QPushButton *m_btnHighlight;
    QPushButton *m_btnClear;
};

#endif // FINDDIALOG_H
