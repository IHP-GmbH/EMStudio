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

#include "filepathmanager.h"

QString FilePathManager::value(const QtProperty *property) const
{
    if (!theValues.contains(property))
        return QString();
    return theValues[property].value;
}

QString FilePathManager::filter(const QtProperty *property) const
{
    if (!theValues.contains(property))
        return QString();
    return theValues[property].filter;
}

void FilePathManager::setValue(QtProperty *property, const QString &val)
{
    if (!theValues.contains(property))
        return;

    Data data = theValues[property];

    if (data.value == val)
        return;

    data.value = val;

    theValues[property] = data;

    emit propertyChanged(property);
    emit valueChanged(property, data.value);
}

void FilePathManager::setFilter(QtProperty *property, const QString &fil)
{
    if (!theValues.contains(property))
        return;

    Data data = theValues[property];

    if (data.filter == fil)
        return;

    data.filter = fil;

    theValues[property] = data;

    emit filterChanged(property, data.filter);
}

