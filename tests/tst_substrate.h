/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_SUBSTRATE_H
#define TST_SUBSTRATE_H

#include <QObject>

class SubstrateTest : public QObject
{
    Q_OBJECT

private slots:
    void parseGoldenXml_andResolve();
    void writeRoundTrip_withThermalDerivedAndDescription();
    void resolve_overridesComputedVariables();
    void computeMinimumSchemaVersion_detectsFeatures();
    void parseMissingFile_fails();
    void resolve_dielectricAndLayerReferences();
};

#endif // TST_SUBSTRATE_H
