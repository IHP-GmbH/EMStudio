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
