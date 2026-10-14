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

#include <QFile>
#include <QDebug>
#include <QTimer>

#include "wslHelper.h"
#include "mainwindow.h"
#include "preferences.h"
#include "ui_mainwindow.h"
#include "substrateview.h"
#include "pythonparser.h"
#include "keywordseditor.h"

void MainWindow::runHeadless(const QString& simKeyLower)
{
    m_headless = true;

    const QString key = simKeyLower.trimmed().toLower();
    if (key == QLatin1String("palace")) {
        runPalace(false);
        return;
    }
    if (key == QLatin1String("openems")) {
        runOpenEMS(false);
        return;
    }

    qCritical() << "Unknown backend for headless run:" << simKeyLower;
    QCoreApplication::exit(1);
}
