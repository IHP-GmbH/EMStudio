#include "finddialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

/*!*******************************************************************************************************************
 * \brief Constructs the FindDialog used for searching and highlighting text in the PythonEditor.
 *
 * Initializes the dialog interface with all search-related controls, including:
 * - A text input field for the search query.
 * - Checkboxes for case sensitivity, whole-word search, and regular expression mode.
 * - Buttons for navigating to the next and previous matches.
 * - Buttons for highlighting all occurrences and clearing existing highlights.
 *
 * The dialog operates in a non-modal state, allowing the user to continue editing while it remains open.
 * Each button emits a corresponding signal carrying the current search parameters.
 *
 * \param parent Parent widget (optional).
 **********************************************************************************************************************/
FindDialog::FindDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Find"));
    setModal(false);

    auto *lbl = new QLabel(tr("Find what:"));
    m_edit = new QLineEdit;

    m_case  = new QCheckBox(tr("Match case"));
    m_whole = new QCheckBox(tr("Whole words"));
    m_regex = new QCheckBox(tr("Regex"));

    m_btnNext      = new QPushButton(tr("Find Next"));
    m_btnPrev      = new QPushButton(tr("Find Previous"));
    m_btnHighlight = new QPushButton(tr("Highlight All"));
    m_btnClear     = new QPushButton(tr("Clear Highlights"));

    m_btnNext->setDefault(true);

    auto *grid = new QGridLayout;
    grid->addWidget(lbl, 0, 0);
    grid->addWidget(m_edit, 0, 1, 1, 3);
    grid->addWidget(m_case, 1, 1);
    grid->addWidget(m_whole, 1, 2);
    grid->addWidget(m_regex, 1, 3);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_btnPrev);
    row2->addWidget(m_btnNext);
    row2->addSpacing(16);
    row2->addWidget(m_btnHighlight);
    row2->addWidget(m_btnClear);

    auto *layout = new QGridLayout(this);
    layout->addLayout(grid, 0, 0);
    layout->addLayout(row2, 1, 0);

    connect(m_btnNext, &QPushButton::clicked, this, [this]{
        emit sigFindNext(m_edit->text(), m_case->isChecked(), m_whole->isChecked(), m_regex->isChecked());
    });

    connect(m_btnPrev, &QPushButton::clicked, this, [this]{
        emit sigFindPrev(m_edit->text(), m_case->isChecked(), m_whole->isChecked(), m_regex->isChecked());
    });

    connect(m_btnHighlight, &QPushButton::clicked, this, [this]{
        emit sigHighlightAll(m_edit->text(), m_case->isChecked(), m_whole->isChecked(), m_regex->isChecked());
    });

    connect(m_btnClear, &QPushButton::clicked, this, [this]{ emit sigClearHighlights(); });
}
