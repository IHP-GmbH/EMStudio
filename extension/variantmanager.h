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

#ifndef VARIANTMANAGER_H
#define VARIANTMANAGER_H

#include "QtPropertyBrowser/qtvariantproperty.h"

class VariantManager : public QtVariantPropertyManager
{
    Q_OBJECT
public:
    VariantManager(QObject *parent = 0)
        : QtVariantPropertyManager(parent)
            { }

    virtual QVariant value(const QtProperty *property) const;
    virtual int valueType(int propertyType) const;
    virtual bool isPropertyTypeSupported(int propertyType) const;

    virtual QStringList attributes(int propertyType) const;
    virtual int attributeType(int propertyType, const QString &attribute) const;
    virtual QVariant attributeValue(const QtProperty *property, const QString &attribute);


    static int filePathTypeId();
public slots:
    virtual void setValue(QtProperty *property, const QVariant &val);
    virtual void setAttribute(QtProperty *property,
                const QString &attribute, const QVariant &value);
protected:
    virtual QString valueText(const QtProperty *property) const;
    virtual void initializeProperty(QtProperty *property);
    virtual void uninitializeProperty(QtProperty *property);
private:
    struct Data {
        QString value;
        QString filter;
    };
    QMap<const QtProperty *, Data> theValues;
};



#endif
