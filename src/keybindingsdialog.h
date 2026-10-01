/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
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

#ifndef KEYBINDINGSDIALOG_H
#define KEYBINDINGSDIALOG_H

#include <QDialog>

#include "navigationstyle.h"

class QComboBox;
class QLabel;
class QTableWidget;

/*!*******************************************************************************************************************
 * \class KeyBindingsDialog
 * \brief Setup → Key Bindings: chooses the viewer navigation style and lists all bindings.
 *
 * The table is read-only and comes from \c NavigationStyle::bindingTable; it refreshes when
 * the style combo changes. The caller reads \c style() after \c exec() returns Accepted.
 **********************************************************************************************************************/
class KeyBindingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeyBindingsDialog(NavStyle style, QWidget *parent = nullptr);

    /*! Style currently selected in the combo. */
    NavStyle            style() const;

private slots:
    void                refreshTable();

private:
    QComboBox          *m_styleCombo = nullptr;
    QLabel             *m_descriptionLbl = nullptr;
    QTableWidget       *m_table = nullptr;
};

#endif // KEYBINDINGSDIALOG_H
