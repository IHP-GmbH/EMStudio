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

#include "substrate.h"
#include <QFile>
#include <QXmlStreamReader>
#include <QDebug>

/*!*******************************************************************************************************************
 * \brief Default constructor for the Substrate class.
 *
 * Initializes an empty Substrate object with no materials, dielectrics, or layers.
 **********************************************************************************************************************/
Substrate::Substrate()
{
}

/*!*******************************************************************************************************************
 * \brief Parses the substrate configuration from an XML file.
 *
 * This function reads the given XML file and extracts material, dielectric, and layer
 * information, which are stored internally. The function returns \c true if parsing
 * was successful, otherwise \c false.
 *
 * \param filePath Absolute path to the XML substrate file.
 * \return \c true on success, \c false on failure or parsing error.
 **********************************************************************************************************************/
bool Substrate::parseXmlFile(const QString &filePath)
{
    m_materials.clear();
    m_dielectrics.clear();
    m_layers.clear();
    m_substrateOffset = 0.0;
    m_schemaVersion.clear();
    m_lengthUnit = "um";

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open substrate XML:" << filePath;
        return false;
    }

    QXmlStreamReader xml(&file);

    auto toLower = [](const QString &s){ return s.toLower(); };
    auto attrD   = [](const QXmlStreamAttributes &a, const char *name, double def = 0.0){
        bool ok = false;
        const double v = a.hasAttribute(name) ? a.value(name).toDouble(&ok) : def;
        return ok ? v : def;
    };
    auto attrI   = [](const QXmlStreamAttributes &a, const char *name, int def = 0){
        bool ok = false;
        const int v = a.hasAttribute(name) ? a.value(name).toInt(&ok) : def;
        return ok ? v : def;
    };
    auto attrS   = [](const QXmlStreamAttributes &a, const char *name, const QString &def = {}){
        return a.hasAttribute(name) ? a.value(name).toString() : def;
    };

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        const QStringRef name = xml.name();
        const auto attrs = xml.attributes();

        if (name == QLatin1String("Stackup")) {
            m_schemaVersion = attrS(attrs, "schemaVersion");
            continue;
        }

        if (name == QLatin1String("ELayers")) {
            m_lengthUnit = attrS(attrs, "LengthUnit", "um");
            continue;
        }

        if (name == QLatin1String("Substrate")) {
            m_substrateOffset = attrD(attrs, "Offset", 0.0);
            continue;
        }

        if (name == QLatin1String("Material")) {
            Material mat;
            mat.setName(attrS(attrs, "Name"));
            mat.setType(toLower(attrS(attrs, "Type"))); // normalize
            mat.setPermittivity(attrD(attrs, "Permittivity"));
            mat.setLossTangent(attrD(attrs, "DielectricLossTangent"));
            mat.setConductivity(attrD(attrs, "Conductivity"));
            mat.setColor(attrS(attrs, "Color"));
            m_materials << mat;
            continue;
        }

        if (name == QLatin1String("Dielectric")) {
            Dielectric d;
            d.setName(attrS(attrs, "Name"));
            d.setMaterial(attrS(attrs, "Material"));
            d.setThickness(attrD(attrs, "Thickness"));
            m_dielectrics << d;
            continue;
        }

        if (name == QLatin1String("Layer")) {
            Layer lay;
            lay.setName(attrS(attrs, "Name"));
            lay.setType(toLower(attrS(attrs, "Type")));
            lay.setZmin(attrD(attrs, "Zmin"));
            lay.setZmax(attrD(attrs, "Zmax"));
            lay.setMaterial(attrS(attrs, "Material"));
            lay.setLayerNumber(attrI(attrs, "Layer"));
            m_layers << lay;
            continue;
        }
    }

    file.close();

    if (xml.hasError()) {
        qWarning() << "XML parsing error:" << xml.errorString();
        return false;
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Returns the list of materials parsed from the substrate XML.
 *
 * \return A const reference to the list of Material objects.
 **********************************************************************************************************************/
const QList<Material> &Substrate::materials() const
{
    return m_materials;
}

/*!*******************************************************************************************************************
 * \brief Returns the list of dielectrics parsed from the substrate XML.
 *
 * \return A const reference to the list of Dielectric objects.
 **********************************************************************************************************************/
const QList<Dielectric> &Substrate::dielectrics() const
{
    return m_dielectrics;
}

/*!*******************************************************************************************************************
 * \brief Returns the list of layers parsed from the substrate XML.
 *
 * \return A const reference to the list of Layer objects.
 **********************************************************************************************************************/
const QList<Layer> &Substrate::layers() const
{
    return m_layers;
}

/*!*******************************************************************************************************************
 * \brief Returns the substrate offset parsed from the XML file.
 *
 * The offset typically defines the vertical reference position of the substrate
 * in the stackup description.
 *
 * \return Substrate offset value, or \c 0.0 if not defined.
 **********************************************************************************************************************/
double Substrate::substrateOffset() const
{
    return m_substrateOffset;
}

/*!*******************************************************************************************************************
 * \brief Returns the schema version of the substrate XML file.
 *
 * This corresponds to the \c schemaVersion attribute in the <Stackup> tag.
 *
 * \return Schema version string, or an empty string if not present.
 **********************************************************************************************************************/
const QString &Substrate::schemaVersion() const
{
    return m_schemaVersion;
}

/*!*******************************************************************************************************************
 * \brief Returns the length unit used in the substrate XML file.
 *
 * This corresponds to the \c LengthUnit attribute in the <ELayers> tag. Typical
 * values are "um" or "nm".
 *
 * \return Length unit string, default is "um" if not defined.
 **********************************************************************************************************************/
const QString &Substrate::lengthUnit() const
{
    return m_lengthUnit;
}

