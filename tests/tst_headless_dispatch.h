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

#ifndef TST_HEADLESS_DISPATCH_H
#define TST_HEADLESS_DISPATCH_H

#include <QObject>

class HeadlessDispatchTest : public QObject
{
    Q_OBJECT

private slots:
    void runHeadless_openems_dispatchesToOpenems();
    void runHeadless_palace_dispatchesToPalace();
    void runHeadless_unknownBackend_setsHeadlessAndRequestsExit();
};

#endif // TST_HEADLESS_DISPATCH_H
