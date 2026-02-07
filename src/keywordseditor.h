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

#ifndef KEYWORDSEDITOR_H
#define KEYWORDSEDITOR_H

#include <QDialog>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>

class QLineEdit;
class QTableView;
class QPushButton;
class QLabel;
class QCloseEvent;
class QResizeEvent;

/*!*******************************************************************************************************************
 * \class KeywordsEditorDialog
 * \brief Dialog for editing keyword/description tables used by EMStudio.
 *
 * The KeywordsEditorDialog provides a table-based editor for keyword definition files
 * (CSV/TSV), supporting filtering, sorting, editing, and validation.
 *
 * The dialog tracks unsaved changes, marks the window title with an asterisk (*),
 * and prompts the user to save modifications when closing.
 **********************************************************************************************************************/
class KeywordsEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeywordsEditorDialog(const QString& csvPath,
                                  const QString& title,
                                  QWidget* parent = nullptr);

    bool                load();
    bool                save();

private slots:
    void                onAddRow();
    void                onRemoveSelected();
    void                onReload();
    void                onFilterTextChanged(const QString& text);
    void                onSortAZ();

protected:
    void                closeEvent(QCloseEvent* e) override;
    void                resizeEvent(QResizeEvent* e) override;

private:
    QString             detectDelimiter(const QString& line) const;
    QStringList         splitLine(const QString& line,
                          const QString& delim) const;

    void                buildUi();
    void                setDirty(bool on);
    void                fitKeywordColumn();

private:
    QString             m_csvPath;
    QString             m_lastDelimiter = "\t"; // default = TSV (tabs)

    bool                m_loading = false;
    bool                m_dirty   = false;

    QStandardItemModel*     m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;

    QLineEdit*          m_filter     = nullptr;
    QTableView*         m_view       = nullptr;
    QLabel*             m_pathLabel  = nullptr;

    QPushButton*        m_btnAdd     = nullptr;
    QPushButton*        m_btnRemove  = nullptr;
    QPushButton*        m_btnReload  = nullptr;
    QPushButton*        m_btnSave    = nullptr;
    QPushButton*        m_btnClose   = nullptr;
    QPushButton*        m_btnSortAZ  = nullptr;
};

#endif // KEYWORDSEDITOR_H
