#ifndef TST_MAINWINDOW_PORTS_H
#define TST_MAINWINDOW_PORTS_H

#include <QObject>

class MainWindowPortsTest : public QObject
{
    Q_OBJECT

private slots:
    void importPortsFromEditor_multilineScript_populatesTable();
    void importPortsFromEditor_targetLayer_onlyToLayerFilled();
    void importPortsFromEditor_sheetReferencePlane_toLayerPreservedOnRoundTrip();
    void addAndRemovePorts_flow_works();
    void toggleSubLayerNames_convertsNumericLayersToNamesAndBack();
    void replacePortSection_multilineVolkerStyle_noDuplicatesOnResync();

    void switchSimTool_updatesState();
    void defaultScriptGeneration_openems_and_palace_notEmpty();
    void setInputs_updatesState_withoutCrash();
    void boundaryOptions_updateOnToolChange_withoutCrash();
    void saveAction_writesScriptToFile_and_updatesState();
    void saveAction_keepsEditedTextSettings();
    void saveAction_keepsUnchangedNumberSpelling();
    void saveAction_keepsIndentedSweepModel();
    void settingsGrid_groupsByTopic();
    void settingsGrid_nestedEditSurvivesSave();
    void fileNew_createsTemplateForEachTool();
    void openemsSave_needsModulesOnlyWithoutPackage();
    void stackupDialog_startsInConfiguredFolder();
    void addSetting_insertsByTopic();
    void addSetting_saveEditResetRemove();
    void addSetting_menuAndOldModels();
    void settingsGrid_tooltipsCombineModelAndKeywordFile();
    void collectSanityFindings_reportsMissingInputs();
    void layoutPreview_withGoldenGds_populatesLayerPanel();
    void convertLooseModel_backsUpConvertsAndReloads();
    void convertLooseModel_refusalLeavesModelUntouched();
    void fieldsPage_noDumpMessageGoesToLog();
};

#endif // TST_MAINWINDOW_PORTS_H
