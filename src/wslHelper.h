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

#ifndef WSLHELPER_H
#define WSLHELPER_H

#include <QMap>
#include <QString>
#include <QVariant>

QString wslExePath();
bool isWslAvailable();

bool existsPortable(const QString &path,
                    const QString &distro,
                    int timeoutMs);

bool isReadablePortable(const QString &path,
                        const QString,
                        int timeoutMs);

bool isExecutablePortable(const QString &path,
                          const QString &distro,
                          int timeoutMs);

bool isReadableLocalThenWsl(const QString &path,
                            const QString &distro,
                            int timeoutMs);

QString toLinuxPathPortable(const QString &path,
                            const QString &distro,
                            int timeoutMs);

QString runWslCmdCapture(const QString &distro,
                         const QStringList &cmd,
                         int timeoutMs = 2000);

QString decodeWslOutput(const QByteArray &ba);

QString shellQuoteSingle(const QString &s);

QStringList listWslDistrosFromSystem(int timeoutMs = 8000);
void exportWslDistroToEnv(const QMap<QString, QVariant>& prefs);

#endif // WSLHELPER_H
