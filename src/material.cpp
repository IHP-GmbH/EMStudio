#include "material.h"

/*!*******************************************************************************************************************
 * \brief Default constructor for the Material class.
 **********************************************************************************************************************/
Material::Material()
{
}

/*!*******************************************************************************************************************
 * \brief Returns the name of the material.
 *
 * \return The material name as a QString.
 **********************************************************************************************************************/
QString Material::name() const
{
    return m_name;
}

/*!*******************************************************************************************************************
 * \brief Sets the name of the material.
 *
 * \param name A QString representing the name of the material.
 **********************************************************************************************************************/
void Material::setName(const QString &name)
{
    m_name = name;
}

/*!*******************************************************************************************************************
 * \brief Returns the type of the material.
 *
 * \return The material type as a QString.
 **********************************************************************************************************************/
QString Material::type() const
{
    return m_type;
}

/*!*******************************************************************************************************************
 * \brief Sets the type of the material.
 *
 * \param type A QString representing the type of the material.
 **********************************************************************************************************************/
void Material::setType(const QString &type)
{
    m_type = type;
}

/*!*******************************************************************************************************************
 * \brief Returns the permittivity of the material.
 *
 * \return A double representing the permittivity value.
 **********************************************************************************************************************/
double Material::permittivity() const
{
    return m_permittivity;
}

/*!*******************************************************************************************************************
 * \brief Sets the permittivity of the material.
 *
 * \param permittivity A double value for the material's permittivity.
 **********************************************************************************************************************/
void Material::setPermittivity(double permittivity)
{
    m_permittivity = permittivity;
}

/*!*******************************************************************************************************************
 * \brief Returns the dielectric loss tangent of the material.
 *
 * \return A double representing the dielectric loss tangent.
 **********************************************************************************************************************/
double Material::lossTangent() const
{
    return m_lossTangent;
}

/*!*******************************************************************************************************************
 * \brief Sets the dielectric loss tangent of the material.
 *
 * \param lossTangent A double value for the dielectric loss tangent.
 **********************************************************************************************************************/
void Material::setLossTangent(double lossTangent)
{
    m_lossTangent = lossTangent;
}

/*!*******************************************************************************************************************
 * \brief Returns the electrical conductivity of the material.
 *
 * \return A double representing the conductivity in S/m.
 **********************************************************************************************************************/
double Material::conductivity() const
{
    return m_conductivity;
}

/*!*******************************************************************************************************************
 * \brief Sets the electrical conductivity of the material.
 *
 * \param conductivity A double value for the conductivity in S/m.
 **********************************************************************************************************************/
void Material::setConductivity(double conductivity)
{
    m_conductivity = conductivity;
}

/*!*******************************************************************************************************************
 * \brief Returns the display color of the material.
 *
 * \return A QColor object representing the material's color.
 **********************************************************************************************************************/
QColor Material::color() const
{
    return m_color;
}

/*!*******************************************************************************************************************
 * \brief Sets the display color of the material.
 *
 * \param color A QColor object representing the new color.
 **********************************************************************************************************************/
void Material::setColor(const QColor &color)
{
    m_color = color;
}
