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

#ifndef FILEPATHMANAGER_H
#define FILEPATHMANAGER_H

#include "QtPropertyBrowser/qtpropertybrowser.h"
#include <QMap>

class FilePathManager : public QtAbstractPropertyManager
{
    Q_OBJECT
public:
    FilePathManager(QObject *parent = 0)
        : QtAbstractPropertyManager(parent)
            { }

    QString value(const QtProperty *property) const;
    QString filter(const QtProperty *property) const;

public slots:
    void setValue(QtProperty *property, const QString &val);
    void setFilter(QtProperty *property, const QString &fil);
signals:
    void valueChanged(QtProperty *property, const QString &val);
    void filterChanged(QtProperty *property, const QString &fil);
protected:
    virtual QString valueText(const QtProperty *property) const { return value(property); }
    virtual void initializeProperty(QtProperty *property) { theValues[property] = Data(); }
    virtual void uninitializeProperty(QtProperty *property) { theValues.remove(property); }
private:

    struct Data
    {
        QString value;
        QString filter;
    };

    QMap<const QtProperty *, Data> theValues;
};


#endif
