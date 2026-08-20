/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#ifndef MATERIAL_H
#define MATERIAL_H

#include <QString>
#include <QColor>

class Material
{
public:
    Material();

    QString             name() const;
    void                setName(const QString &name);

    QString             type() const;
    void                setType(const QString &type);

    QString             permittivityRaw() const;
    void                setPermittivityRaw(const QString &raw);
    double              permittivity() const;
    void                setPermittivity(double permittivity);

    QString             lossTangentRaw() const;
    void                setLossTangentRaw(const QString &raw);
    double              lossTangent() const;
    void                setLossTangent(double lossTangent);

    QString             conductivityRaw() const;
    void                setConductivityRaw(const QString &raw);
    double              conductivity() const;
    void                setConductivity(double conductivity);

    QString             colorHex() const;
    void                setColorHex(const QString &hex);
    QColor              color() const;
    void                setColor(const QColor &color);

    QString             thermalConductivityRaw() const;
    void                setThermalConductivityRaw(const QString &raw);

    QString             thermalConductivityTable() const;
    void                setThermalConductivityTable(const QString &table);

    QString             rsRaw() const;
    void                setRsRaw(const QString &raw);

private:
    QString             m_name;
    QString             m_type;
    QString             m_permittivityRaw;
    double              m_permittivity = 0.0;
    QString             m_lossTangentRaw;
    double              m_lossTangent = 0.0;
    QString             m_conductivityRaw;
    double              m_conductivity = 0.0;
    QString             m_colorHex;
    QColor              m_color;
    QString             m_thermalConductivityRaw;
    QString             m_thermalConductivityTable;
    QString             m_rsRaw;
};

#endif // MATERIAL_H
