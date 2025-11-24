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


#include "layer.h"

/*!*******************************************************************************************************************
 * \brief Default constructor for the Layer class.
 **********************************************************************************************************************/
Layer::Layer()
{
}

/*!*******************************************************************************************************************
 * \brief Default constructor for the Layer class.
 **********************************************************************************************************************/
QString Layer::name() const
{
    return m_name;
}

/*!*******************************************************************************************************************
 * \brief Sets the name of the layer.
 * \param name The name to assign to the layer.
 **********************************************************************************************************************/
void Layer::setName(const QString &name)
{
    m_name = name;
}

/*!*******************************************************************************************************************
 * \brief Returns the type of the layer (e.g., conductor, via, dielectric).
 * \return The type of the layer.
 **********************************************************************************************************************/
QString Layer::type() const
{
    return m_type;
}

/*!*******************************************************************************************************************
 * \brief Sets the type of the layer.
 * \param type The type to assign to the layer.
 **********************************************************************************************************************/
void Layer::setType(const QString &type)
{
    m_type = type;
}

/*!*******************************************************************************************************************
 * \brief Returns the minimum Z-coordinate (bottom) of the layer.
 * \return The Zmin value.
 **********************************************************************************************************************/
double Layer::zmin() const
{
    return m_zmin;
}

/*!*******************************************************************************************************************
 * \brief Sets the minimum Z-coordinate (bottom) of the layer.
 * \param zmin The Zmin value to set.
 **********************************************************************************************************************/
void Layer::setZmin(double zmin)
{
    m_zmin = zmin;
}

/*!*******************************************************************************************************************
 * \brief Returns the maximum Z-coordinate (top) of the layer.
 * \return The Zmax value.
 **********************************************************************************************************************/
double Layer::zmax() const
{
    return m_zmax;
}

/*!*******************************************************************************************************************
 * \brief Sets the maximum Z-coordinate (top) of the layer.
 * \param zmax The Zmax value to set.
 **********************************************************************************************************************/
void Layer::setZmax(double zmax)
{
    m_zmax = zmax;
}

/*!*******************************************************************************************************************
 * \brief Returns the material name assigned to the layer.
 * \return The material name.
 **********************************************************************************************************************/
QString Layer::material() const
{
    return m_material;
}

/*!*******************************************************************************************************************
 * \brief Sets the material name for the layer.
 * \param material The material name to assign.
 **********************************************************************************************************************/
void Layer::setMaterial(const QString &material)
{
    m_material = material;
}

/*!*******************************************************************************************************************
 * \brief Returns the numeric layer index.
 * \return The layer number.
 **********************************************************************************************************************/
int Layer::layerNumber() const
{
    return m_layerNumber;
}

/*!*******************************************************************************************************************
 * \brief Sets the numeric layer index.
 * \param number The layer number to assign.
 **********************************************************************************************************************/
void Layer::setLayerNumber(int number)
{
    m_layerNumber = number;
}
