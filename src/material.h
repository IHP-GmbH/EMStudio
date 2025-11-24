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

/*!*******************************************************************************************************************
 * \class Material
 * \brief Represents a material used in substrate layers.
 *
 * Encapsulates physical and visual properties of materials used in simulation or layout visualization,
 * including dielectric and conductive properties.
 *
 * Properties include:
 * - Name and type (e.g., dielectric, conductor)
 * - Relative permittivity
 * - Loss tangent (for dielectrics)
 * - Electrical conductivity (for conductors)
 * - Visualization color
 **********************************************************************************************************************/
class Material
{
public:
    Material();

    QString             name() const;
    void                setName(const QString &name);

    QString             type() const;
    void                setType(const QString &type);

    double              permittivity() const;
    void                setPermittivity(double permittivity);

    double              lossTangent() const;
    void                setLossTangent(double lossTangent);

    double              conductivity() const;
    void                setConductivity(double conductivity);

    QColor              color() const;
    void                setColor(const QColor &color);

private:
    QString             m_name;
    QString             m_type;
    double              m_permittivity = 0.0;
    double              m_lossTangent = 0.0;
    double              m_conductivity = 0.0;
    QColor              m_color;
};

#endif // MATERIAL_H
