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
