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

#include <cmath>

#include "variantmanager.h"

#include <QLocale>
#include <algorithm>

class FilePathPropertyType
{
};

Q_DECLARE_METATYPE(FilePathPropertyType)

int VariantManager::filePathTypeId()
{
    return qMetaTypeId<FilePathPropertyType>();
}

bool VariantManager::isPropertyTypeSupported(int propertyType) const
{
    if (propertyType == filePathTypeId())
        return true;
    return QtVariantPropertyManager::isPropertyTypeSupported(propertyType);
}

int VariantManager::valueType(int propertyType) const
{
    if (propertyType == filePathTypeId())
        return QVariant::String;
    return QtVariantPropertyManager::valueType(propertyType);
}

QVariant VariantManager::value(const QtProperty *property) const
{
    if (theValues.contains(property))
        return theValues[property].value;
    return QtVariantPropertyManager::value(property);
}

QStringList VariantManager::attributes(int propertyType) const
{
    if (propertyType == filePathTypeId()) {
        QStringList attr;
        attr << QLatin1String("filter");
        return attr;
    }
    return QtVariantPropertyManager::attributes(propertyType);
}

int VariantManager::attributeType(int propertyType, const QString &attribute) const
{
    if (propertyType == filePathTypeId()) {
        if (attribute == QLatin1String("filter"))
            return QVariant::String;
        return 0;
    }
    return QtVariantPropertyManager::attributeType(propertyType, attribute);
}

QVariant VariantManager::attributeValue(const QtProperty *property, const QString &attribute)
{
    if (theValues.contains(property)) {
        if (attribute == QLatin1String("filter"))
            return theValues[property].filter;
        return QVariant();
    }
    return QtVariantPropertyManager::attributeValue(property, attribute);
}

static inline QString formatScientificCompact(double v, int mantissaSigDigits = 2)
{
    if (v == 0.0)
        return QStringLiteral("0");

    const double av = std::abs(v);
    const int exp10 = int(std::floor(std::log10(av)));
    const double mantissa = v / std::pow(10.0, exp10);

    // 'g' => compact mantissa, no trailing zeros
    QString m = QLocale::c().toString(mantissa, 'g', mantissaSigDigits);

    // Build exponent like e+10 / e-6
    const QString e = QStringLiteral("e%1%2")
                          .arg(exp10 >= 0 ? QLatin1Char('+') : QLatin1Char('-'))
                          .arg(std::abs(exp10));

    return m + e;
}

QString VariantManager::valueText(const QtProperty *property) const
{
    if (propertyType(property) == QVariant::Double) {
        const double v = QtVariantPropertyManager::value(property).toDouble();
        const double av = std::abs(v);

        const bool useSci = (av != 0.0) && (av >= 1e6 || av < 1e-3);

        if (useSci) {
            return formatScientificCompact(v, /*mantissaSigDigits=*/2);
        }

        int decimals = QtVariantPropertyManager::attributeValue(
                           property, QLatin1String("decimals")).toInt();
        decimals = std::max(decimals, 12);

        QString s = QLocale::c().toString(v, 'f', decimals);

        while (s.contains('.') && s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
        return s;
    }

    if (propertyType(property) == filePathTypeId())
        return value(property).toString();

    return QtVariantPropertyManager::valueText(property);
}


void VariantManager::setValue(QtProperty *property, const QVariant &val)
{
    if (theValues.contains(property)) {
        if (val.type() != QVariant::String && !val.canConvert(QVariant::String))
            return;
#if QT_VERSION >= 0x050000
        QString str = val.value<QString>();
#else
        QString str = qVariantValue<QString>(val);
#endif
        Data d = theValues[property];
        if (d.value == str)
            return;
        d.value = str;
        theValues[property] = d;
        emit propertyChanged(property);
        emit valueChanged(property, str);
        return;
    }
    QtVariantPropertyManager::setValue(property, val);
}

void VariantManager::setAttribute(QtProperty *property,
                const QString &attribute, const QVariant &val)
{
    if (theValues.contains(property)) {
        if (attribute == QLatin1String("filter")) {
            if (val.type() != QVariant::String && !val.canConvert(QVariant::String))
                return;
#if QT_VERSION >= 0x050000
            QString str = val.value<QString>();
#else
            QString str = qVariantValue<QString>(val);
#endif
            Data d = theValues[property];
            if (d.filter == str)
                return;
            d.filter = str;
            theValues[property] = d;
            emit attributeChanged(property, attribute, str);
        }
        return;
    }
    QtVariantPropertyManager::setAttribute(property, attribute, val);
}

void VariantManager::initializeProperty(QtProperty *property)
{
    if (propertyType(property) == filePathTypeId())
        theValues[property] = Data();
    QtVariantPropertyManager::initializeProperty(property);
}

void VariantManager::uninitializeProperty(QtProperty *property)
{
    theValues.remove(property);
    QtVariantPropertyManager::uninitializeProperty(property);
}

