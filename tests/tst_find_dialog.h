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
 ************************************************************************/

#ifndef TST_FIND_DIALOG_H
#define TST_FIND_DIALOG_H

#include <QObject>

class FindDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void buttons_emit_expected_signals();
};

#endif // TST_FIND_DIALOG_H
