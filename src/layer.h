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

#ifndef LAYER_H
#define LAYER_H

#include <QString>

/*!*******************************************************************************************************************
 * \class Layer
 * \brief Represents a single physical layer in the substrate stackup.
 *
 * The Layer class encapsulates metadata for a layer such as its name, type (e.g., "conductor", "via", or "dielectric"),
 * Z-position boundaries (minimum and maximum), associated material, and an optional numeric identifier.
 * It is used in modeling and visualizing semiconductor or EM substrate layer structures.
 **********************************************************************************************************************/
class Layer
{
public:
    Layer();

    QString             name() const;
    void                setName(const QString &name);

    QString             type() const;
    void                setType(const QString &type);

    double              zmin() const;
    void                setZmin(double zmin);

    double              zmax() const;
    void                setZmax(double zmax);

    QString             material() const;
    void                setMaterial(const QString &material);

    int                 layerNumber() const;
    void                setLayerNumber(int number);

private:
    QString             m_name;
    QString             m_type;
    double              m_zmin = 0.0;
    double              m_zmax = 0.0;
    QString             m_material;
    int                 m_layerNumber = 0;
};

#endif // LAYER_H
