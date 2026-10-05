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
    void refreshSimToolOptions_listsElmerThermalEvenWithoutSolverPath();
    void defaultElmerThermalTemplate_containsThermalWorkflow();
    void thermalTable_roundTripFromScript();
    void findThermalResultsVtu_prefersThermalResultsPrefix();
    void findThermalResultsVtu_prefersPvtuOverVtu();
    void substrateOffset_expressionResolvesWithVariables();
    void thermalRows_addRemoveAndWorkflowHelpers();
    void openThermalResults_switchesToFieldView();
    void fieldChoices_parseListingAndViewerArgs();
    void generateScript_elmerThermalFromGui();
    void forceStartSimulationOff_clearsTrueFlags();
    void applyGdsAndXmlPaths_updatesCellnameAndGdsCellname();
    void applyGdsAndXmlPaths_doesNotTouchReadGdsKwarg();
    void applyGdsAndXmlPaths_doesNotPrependGdsCellnameWhenSettingsCellnameExists();
    void applyGdsAndXmlPaths_updatesSettingsGdsAndSubstrateFile();
    void thermalWorkflow_keepsSingleElmerThermalFlag();
    void loadModel_selectsSettingsCellnameAndKeepsItOnSave();
    void readGdsCellRef_findsTheCellArgument();
    void readGdsPurposes_resolvesPurposelist();
    void applyTopCell_writesOnlyTheReadGdsVariable();
    void loadModel_withoutCellSelectsGdsTopCell();
    void loadModel_findsMissingInputFilesNextToModel();
    void stackupOverrides_followReadSubstrateArgument();
    void loadModel_replacesPreviousModelInputs();
    void stackupOverrides_keepUnquotedExpressions();
    void indentedThermalBlockAndCell_keepIndentation();
    void elmerSolverStage_runsSolverWithoutRunElmerScript();
    void layoutPreview_drawsThermalObjectsNotPorts();
    void thermalTargets_offerStackupLayersByType();
    void fdump_neverWrittenAsBoolAfterToolSwitch();
    void toolSwitch_warnsOnlyForUserChoiceWithOpenModel();
};

#endif // TST_ELMER_H
