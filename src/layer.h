/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#ifndef LAYER_H
#define LAYER_H

#include <QString>

class Layer
{
public:
    Layer();

    QString             name() const;
    void                setName(const QString &name);

    QString             type() const;
    void                setType(const QString &type);

    QString             zminRaw() const;
    void                setZminRaw(const QString &raw);

    QString             zmaxRaw() const;
    void                setZmaxRaw(const QString &raw);

    double              zmin() const;
    void                setZmin(double zmin);

    double              zmax() const;
    void                setZmax(double zmax);

    QString             material() const;
    void                setMaterial(const QString &material);

    int                 layerNumber() const;
    void                setLayerNumber(int number);

    QString             reference() const;
    void                setReference(const QString &reference);

    QString             referenceEdge() const;
    void                setReferenceEdge(const QString &edge);

private:
    QString             m_name;
    QString             m_type;
    QString             m_zminRaw;
    QString             m_zmaxRaw;
    double              m_zmin = 0.0;
    double              m_zmax = 0.0;
    QString             m_material;
    int                 m_layerNumber = 0;
    QString             m_reference;
    QString             m_referenceEdge;
};

#endif // LAYER_H
