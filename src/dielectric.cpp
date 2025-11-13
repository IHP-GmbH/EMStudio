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
