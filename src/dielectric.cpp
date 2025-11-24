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


#include "dielectric.h"

/*!*******************************************************************************************************************
 * \brief Constructs a new Dielectric object with default values.
 **********************************************************************************************************************/
Dielectric::Dielectric()
{
}

/*!*******************************************************************************************************************
 * \brief Returns the name of the dielectric.
 * \return The dielectric name as a QString.
 **********************************************************************************************************************/
QString Dielectric::name() const
{
    return m_name;
}

/*!*******************************************************************************************************************
 * \brief Sets the name of the dielectric.
 * \param name The name to assign to the dielectric.
 **********************************************************************************************************************/
void Dielectric::setName(const QString &name)
{
    m_name = name;
}

/*!*******************************************************************************************************************
 * \brief Returns the associated material of the dielectric.
 * \return The name of the material as a QString.
 **********************************************************************************************************************/
QString Dielectric::material() const
{
    return m_material;
}

/*!*******************************************************************************************************************
 * \brief Sets the material associated with the dielectric.
 * \param material The name of the material.
 **********************************************************************************************************************/
void Dielectric::setMaterial(const QString &material)
{
    m_material = material;
}

/*!*******************************************************************************************************************
 * \brief Returns the thickness of the dielectric.
 * \return The thickness value as a double.
 **********************************************************************************************************************/
double Dielectric::thickness() const
{
    return m_thickness;
}

/*!*******************************************************************************************************************
 * \brief Sets the thickness of the dielectric.
 * \param thickness The thickness value to set.
 **********************************************************************************************************************/
void Dielectric::setThickness(double thickness)
{
    m_thickness = thickness;
}
