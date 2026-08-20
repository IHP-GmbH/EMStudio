/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#ifndef DIELECTRIC_H
#define DIELECTRIC_H

#include <QString>

class Dielectric
{
public:
    Dielectric();

    QString             name() const;
    void                setName(const QString &name);

    QString             material() const;
    void                setMaterial(const QString &material);

    QString             thicknessRaw() const;
    void                setThicknessRaw(const QString &raw);

    double              thickness() const;
    void                setThickness(double thickness);

    QString             reference() const;
    void                setReference(const QString &reference);

    QString             referenceEdge() const;
    void                setReferenceEdge(const QString &edge);

    double              resolvedZmin() const;
    void                setResolvedZmin(double z);

    double              resolvedZmax() const;
    void                setResolvedZmax(double z);

private:
    QString             m_name;
    QString             m_material;
    QString             m_thicknessRaw;
    double              m_thickness = 0.0;
    QString             m_reference;
    QString             m_referenceEdge;
    double              m_resolvedZmin = 0.0;
    double              m_resolvedZmax = 0.0;
};

#endif // DIELECTRIC_H
