/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#include "layer.h"

Layer::Layer() = default;

QString Layer::name() const { return m_name; }
void Layer::setName(const QString &name) { m_name = name; }

QString Layer::type() const { return m_type; }
void Layer::setType(const QString &type) { m_type = type; }

QString Layer::zminRaw() const { return m_zminRaw; }
void Layer::setZminRaw(const QString &raw)
{
    m_zminRaw = raw.trimmed();
    bool ok = false;
    if (!m_zminRaw.startsWith(QLatin1Char('='))) {
        const double d = m_zminRaw.toDouble(&ok);
        if (ok)
            m_zmin = d;
    }
}

QString Layer::zmaxRaw() const { return m_zmaxRaw; }
void Layer::setZmaxRaw(const QString &raw)
{
    m_zmaxRaw = raw.trimmed();
    bool ok = false;
    if (!m_zmaxRaw.startsWith(QLatin1Char('='))) {
        const double d = m_zmaxRaw.toDouble(&ok);
        if (ok)
            m_zmax = d;
    }
}

double Layer::zmin() const { return m_zmin; }
void Layer::setZmin(double zmin) { m_zmin = zmin; }

double Layer::zmax() const { return m_zmax; }
void Layer::setZmax(double zmax) { m_zmax = zmax; }

QString Layer::material() const { return m_material; }
void Layer::setMaterial(const QString &material) { m_material = material; }

int Layer::layerNumber() const { return m_layerNumber; }
void Layer::setLayerNumber(int number) { m_layerNumber = number; }

QString Layer::reference() const { return m_reference; }
void Layer::setReference(const QString &reference) { m_reference = reference; }

QString Layer::referenceEdge() const { return m_referenceEdge; }
void Layer::setReferenceEdge(const QString &edge) { m_referenceEdge = edge; }
