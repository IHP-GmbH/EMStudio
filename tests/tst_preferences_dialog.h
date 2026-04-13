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

#ifndef TST_PREFERENCES_DIALOG_H
#define TST_PREFERENCES_DIALOG_H

#include <QObject>

class PreferencesDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void setupPreferencesPanel_initializesValues_and_runModeState();
    void apply_updates_preferences_map();
    void cancel_closes_dialog();
};

#endif // TST_PREFERENCES_DIALOG_H
