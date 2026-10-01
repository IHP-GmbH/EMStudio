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

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QSettings>
#include <QString>

/*!*******************************************************************************************************************
 * \brief Opens EMStudio's persistent settings store (QSettings "EMStudio" / "EMStudioApp").
 *
 * Use this instead of constructing QSettings directly. Test builds (\c EMSTUDIO_TESTING)
 * use a separate INI store ("EMStudioTests"), which the test runner places in a
 * temporary folder, so running the tests never changes the user's preferences.
 *
 * \return Settings object; C++17 guaranteed copy elision allows returning it by value.
 **********************************************************************************************************************/
inline QSettings emstudioSettings()
{
#ifdef EMSTUDIO_TESTING
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("EMStudioTests"), QStringLiteral("EMStudioApp"));
#else
    return QSettings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
#endif
}

#endif // APPSETTINGS_H
