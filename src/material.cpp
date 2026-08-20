/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#include "material.h"

static QColor colorFromHex(const QString &hex)
{
    QString h = hex.trimmed();
    if (h.startsWith(QLatin1Char('#')))
        h = h.mid(1);
    if (h.size() == 6)
        return QColor(QLatin1Char('#') + h);
    return QColor(hex);
}

Material::Material() = default;

QString Material::name() const { return m_name; }
void Material::setName(const QString &name) { m_name = name; }

QString Material::type() const { return m_type; }
void Material::setType(const QString &type) { m_type = type; }

QString Material::permittivityRaw() const { return m_permittivityRaw; }
void Material::setPermittivityRaw(const QString &raw)
{
    m_permittivityRaw = raw.trimmed();
    bool ok = false;
    if (!m_permittivityRaw.startsWith(QLatin1Char('='))) {
        const double d = m_permittivityRaw.toDouble(&ok);
        if (ok)
            m_permittivity = d;
    }
}
double Material::permittivity() const { return m_permittivity; }
void Material::setPermittivity(double permittivity)
{
    m_permittivity = permittivity;
    if (m_permittivityRaw.isEmpty())
        m_permittivityRaw = QString::number(permittivity);
}

QString Material::lossTangentRaw() const { return m_lossTangentRaw; }
void Material::setLossTangentRaw(const QString &raw)
{
    m_lossTangentRaw = raw.trimmed();
    bool ok = false;
    if (!m_lossTangentRaw.startsWith(QLatin1Char('='))) {
        const double d = m_lossTangentRaw.toDouble(&ok);
        if (ok)
            m_lossTangent = d;
    }
}
double Material::lossTangent() const { return m_lossTangent; }
void Material::setLossTangent(double lossTangent)
{
    m_lossTangent = lossTangent;
    if (m_lossTangentRaw.isEmpty())
        m_lossTangentRaw = QString::number(lossTangent);
}

QString Material::conductivityRaw() const { return m_conductivityRaw; }
void Material::setConductivityRaw(const QString &raw)
{
    m_conductivityRaw = raw.trimmed();
    bool ok = false;
    if (!m_conductivityRaw.startsWith(QLatin1Char('='))) {
        const double d = m_conductivityRaw.toDouble(&ok);
        if (ok)
            m_conductivity = d;
    }
}
double Material::conductivity() const { return m_conductivity; }
void Material::setConductivity(double conductivity)
{
    m_conductivity = conductivity;
    if (m_conductivityRaw.isEmpty())
        m_conductivityRaw = QString::number(conductivity);
}

QString Material::colorHex() const { return m_colorHex; }
void Material::setColorHex(const QString &hex)
{
    m_colorHex = hex.trimmed();
    if (m_colorHex.startsWith(QLatin1Char('#')))
        m_colorHex = m_colorHex.mid(1);
    m_color = colorFromHex(m_colorHex);
}
QColor Material::color() const { return m_color; }
void Material::setColor(const QColor &color)
{
    m_color = color;
    if (color.isValid())
        m_colorHex = color.name(QColor::HexRgb).mid(1);
}

QString Material::thermalConductivityRaw() const { return m_thermalConductivityRaw; }
void Material::setThermalConductivityRaw(const QString &raw) { m_thermalConductivityRaw = raw.trimmed(); }

QString Material::thermalConductivityTable() const { return m_thermalConductivityTable; }
void Material::setThermalConductivityTable(const QString &table) { m_thermalConductivityTable = table.trimmed(); }

QString Material::rsRaw() const { return m_rsRaw; }
void Material::setRsRaw(const QString &raw) { m_rsRaw = raw.trimmed(); }
