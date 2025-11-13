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
