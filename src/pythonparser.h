#ifndef PYTHONPARSER_H
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

#define PYTHONPARSER_H

#include <QMap>
#include <QVariant>
#include <QString>

class PythonParser
{
public:
    struct Result
    {
        bool                        ok = false;
        QMap<QString, QVariant>     settings;
        QString                     error;

        QString                     simPath;
        QString                     gdsFilename;
        QString                     xmlFilename;

        QString getGdsFilename() const { return gdsFilename; }
        QString getXmlFilename() const { return xmlFilename; }
    };

    static Result parseSettings(const QString &filePath);
};

#endif // PYTHONPARSER_H
