/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#ifndef SUBSTRATE_H
#define SUBSTRATE_H

#include "material.h"
#include "dielectric.h"
#include "layer.h"

#include <QString>
#include <QList>
#include <QHash>
#include <QVariant>
#include <QPair>

struct StackupVariable {
    QString name;
    QString valueRaw;   // literal or "=expr"
    QString type;       // optional: "string", empty => number
    QString resolved;   // display string after resolve
    bool isComputed() const { return valueRaw.trimmed().startsWith(QLatin1Char('=')); }
};

struct DerivedLayer {
    QString name;
    int layerNumber = 0;
    QString operation;          // AND/OR/XOR/NOT/SIZE
    QList<int> operands;
    QString sizeValue;          // for SIZE
};

struct ThermalTablePoint {
    QString temperatureRaw;
    QString valueRaw;
};

struct ThermalTable {
    QString name;
    QList<ThermalTablePoint> points;
};

class Substrate
{
public:
    Substrate();

    bool                            parseXmlFile(const QString &filePath);
    bool                            writeXmlFile(const QString &filePath) const;

    /*! Apply model-level overrides then resolve expressions and Reference geometry. */
    bool                            resolve(const QHash<QString, QVariant> &overrides = {},
                                            QString *error = nullptr);

    const QList<StackupVariable>    &variables() const;
    QList<StackupVariable>          &variables();

    const QList<Material>           &materials() const;
    QList<Material>                 &materials();

    const QList<Dielectric>         &dielectrics() const;
    QList<Dielectric>               &dielectrics();

    const QList<Layer>              &layers() const;
    QList<Layer>                    &layers();

    const QList<DerivedLayer>       &derivedLayers() const;
    QList<DerivedLayer>             &derivedLayers();

    const QList<ThermalTable>       &thermalTables() const;
    QList<ThermalTable>             &thermalTables();

    double                          substrateOffset() const;
    void                            setSubstrateOffset(double offset);
    QString                         substrateOffsetRaw() const;
    void                            setSubstrateOffsetRaw(const QString &raw);

    const QString                   &schemaVersion() const;
    void                            setSchemaVersion(const QString &v);

    const QString                   &lengthUnit() const;
    void                            setLengthUnit(const QString &u);

    const QString                   &description() const;
    void                            setDescription(const QString &d);

    /*! Plain (non-computed) variables suitable for model overrides. */
    QList<StackupVariable>          overridableVariables() const;

    QString                         computeMinimumSchemaVersion() const;

private:
    bool                            resolveVariables(const QHash<QString, QVariant> &overrides,
                                                     QHash<QString, QVariant> *outVars,
                                                     QString *error);
    bool                            resolveGeometry(const QHash<QString, QVariant> &vars,
                                                    QString *error);

    QList<StackupVariable>          m_variables;
    QList<Material>                 m_materials;
    QList<Dielectric>               m_dielectrics;
    QList<Layer>                    m_layers;
    QList<DerivedLayer>             m_derivedLayers;
    QList<ThermalTable>             m_thermalTables;

    double                          m_substrateOffset = 0.0;
    QString                         m_substrateOffsetRaw;
    QString                         m_schemaVersion;
    QString                         m_lengthUnit = QStringLiteral("um");
    QString                         m_description;
};

#endif // SUBSTRATE_H
