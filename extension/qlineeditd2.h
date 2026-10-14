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
 *
 *  Property-browser editors maintained as part of EMStudio (GPL-3.0-or-later).
 ************************************************************************/

#ifndef QLINEEDITD2_H
#define QLINEEDITD2_H

#include <QLineEdit>

class QLineEditD2 : public QLineEdit
{
    Q_OBJECT
public:
    QLineEditD2(QWidget *parent = 0) : QLineEdit(parent) {}

signals:
    void mouseDoubleClickSignal(QMouseEvent * e);
protected:
    void mouseDoubleClickEvent(QMouseEvent * e);
private slots:
    //void mouseDoubleClickEvent();
private:
};

#endif
