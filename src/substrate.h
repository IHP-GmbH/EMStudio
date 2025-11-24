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

#ifndef SUBSTRATE_H
#define SUBSTRATE_H

#include "material.h"
#include "dielectric.h"
#include "layer.h"

#include <QString>
#include <QList>

/*!*******************************************************************************************************************
 * \class Substrate
 * \brief Represents a parsed substrate stack including materials, dielectrics, and metal/via layers.
 *
 * Provides access to the parsed substrate configuration by storing:
 * - A list of materials (with electrical properties and colors)
 * - A list of dielectric layers (with thickness and name)
 * - A list of metal or via layers (with Z-range, material and type)
 *
 * The data is typically loaded from an XML file using the `parseXmlFile()` method.
 *
 * \see Material, Dielectric, Layer
 **********************************************************************************************************************/
class Substrate
{
public:
    Substrate();

    bool                            parseXmlFile(const QString &filePath);

    const QList<Material>           &materials() const;
    const QList<Dielectric>         &dielectrics() const;
    const QList<Layer>              &layers() const;

    double                          substrateOffset() const;
    const QString                   &schemaVersion() const;
    const QString                   &lengthUnit()    const;

private:
    QList<Material>                 m_materials;
    QList<Dielectric>               m_dielectrics;
    QList<Layer>                    m_layers;

    double                          m_substrateOffset = 0.0;
    QString                         m_schemaVersion;
    QString                         m_lengthUnit = "um";
};

#endif // SUBSTRATE_H
