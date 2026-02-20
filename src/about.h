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

#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

namespace Ui {
class AboutDialog;
}

/*!*******************************************************************************************************************
 * \class AboutDialog
 * \brief Modal dialog displaying information about EMStudio.
 *
 * The AboutDialog presents application metadata such as:
 *  - application name and version,
 *  - Qt runtime version,
 *  - build type and timestamp,
 *  - license and project information.
 *
 * The dialog is implemented using a Qt Designer (.ui) file and is shown
 * modally from the Help → About EMStudio menu action.
 **********************************************************************************************************************/
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog();

private:
    void                initUi();

private:
    Ui::AboutDialog*    m_ui;
};

#endif // ABOUTDIALOG_H
