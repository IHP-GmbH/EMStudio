/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_ELMER_H
#define TST_ELMER_H

#include <QObject>

class ElmerTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizeSimToolKey_mapsLegacyElmerToEm();
    void isElmerKeyHelpers_classifyFamily();
    void detectPythonModelSimKey_elmerThermalMarkers();
    void detectPythonModelSimKey_elmerEmMarkers();
    void refreshSimToolOptions_enablesElmerWhenSolverStubConfigured();
    void defaultElmerThermalTemplate_containsThermalWorkflow();
    void thermalTable_roundTripFromScript();
    void findThermalResultsVtu_prefersThermalResultsPrefix();
    void resolveParaViewExecutable_usesPreferenceWhenPresent();
    void substrateOffset_expressionResolvesWithVariables();
    void thermalRows_addRemoveAndWorkflowHelpers();
    void openThermalResults_noVtuIsNoop();
    void openThermalResults_withVtuAndParaViewStub();
    void generateScript_elmerThermalFromGui();
};

#endif // TST_ELMER_H
