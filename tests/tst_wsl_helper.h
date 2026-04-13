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

#ifndef TST_WSL_HELPER_H
#define TST_WSL_HELPER_H

#include <QObject>

class WslHelperTest : public QObject
{
    Q_OBJECT

private slots:
    void decodeWslOutput_utf8_returnsText();
    void decodeWslOutput_utf16le_returnsText();
    void exportWslDistroToEnv_setsAndClearsVariable();
    void wslExePath_matchesAvailability();
    void runWslCmdCapture_echo_works_ifWslAvailable();
    void listWslDistrosFromSystem_returnsSomething_ifWslAvailable();
    void mainWindow_toLinuxPathPortable_handlesBasicCases_ifWslAvailable();
    void mainWindow_pathExistsPortable_checksLocalPath();
};

#endif // TST_WSL_HELPER_H
