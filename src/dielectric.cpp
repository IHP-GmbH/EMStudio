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

Dielectric::Dielectric() = default;

QString Dielectric::name() const { return m_name; }
void Dielectric::setName(const QString &name) { m_name = name; }

QString Dielectric::material() const { return m_material; }
void Dielectric::setMaterial(const QString &material) { m_material = material; }

QString Dielectric::thicknessRaw() const { return m_thicknessRaw; }
void Dielectric::setThicknessRaw(const QString &raw)
{
    m_thicknessRaw = raw.trimmed();
    bool ok = false;
    const double d = m_thicknessRaw.startsWith(QLatin1Char('='))
                         ? 0.0
                         : m_thicknessRaw.toDouble(&ok);
    if (ok)
        m_thickness = d;
}

double Dielectric::thickness() const { return m_thickness; }
void Dielectric::setThickness(double thickness)
{
    m_thickness = thickness;
}

QString Dielectric::reference() const { return m_reference; }
void Dielectric::setReference(const QString &reference) { m_reference = reference; }

QString Dielectric::referenceEdge() const { return m_referenceEdge; }
void Dielectric::setReferenceEdge(const QString &edge) { m_referenceEdge = edge; }

double Dielectric::resolvedZmin() const { return m_resolvedZmin; }
void Dielectric::setResolvedZmin(double z) { m_resolvedZmin = z; }

double Dielectric::resolvedZmax() const { return m_resolvedZmax; }
void Dielectric::setResolvedZmax(double z) { m_resolvedZmax = z; }
