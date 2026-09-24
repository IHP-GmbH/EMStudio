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

#ifndef MATERIAL_H
#define MATERIAL_H

#include <QString>
#include <QColor>

class Material
{
public:
    Material();

    QString             name() const;
    void                setName(const QString &name);

    QString             type() const;
    void                setType(const QString &type);

    QString             permittivityRaw() const;
    void                setPermittivityRaw(const QString &raw);
    double              permittivity() const;
    void                setPermittivity(double permittivity);

    QString             lossTangentRaw() const;
    void                setLossTangentRaw(const QString &raw);
    double              lossTangent() const;
    void                setLossTangent(double lossTangent);

    QString             conductivityRaw() const;
    void                setConductivityRaw(const QString &raw);
    double              conductivity() const;
    void                setConductivity(double conductivity);

    QString             colorHex() const;
    void                setColorHex(const QString &hex);
    QColor              color() const;
    void                setColor(const QColor &color);

    QString             thermalConductivityRaw() const;
    void                setThermalConductivityRaw(const QString &raw);

    QString             thermalConductivityTable() const;
    void                setThermalConductivityTable(const QString &table);

    QString             rsRaw() const;
    void                setRsRaw(const QString &raw);

private:
    QString             m_name;
    QString             m_type;
    QString             m_permittivityRaw;
    double              m_permittivity = 0.0;
    QString             m_lossTangentRaw;
    double              m_lossTangent = 0.0;
    QString             m_conductivityRaw;
    double              m_conductivity = 0.0;
    QString             m_colorHex;
    QColor              m_color;
    QString             m_thermalConductivityRaw;
    QString             m_thermalConductivityTable;
    QString             m_rsRaw;
};

#endif // MATERIAL_H
