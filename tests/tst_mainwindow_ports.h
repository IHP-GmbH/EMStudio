#ifndef TST_MAINWINDOW_PORTS_H
#define TST_MAINWINDOW_PORTS_H

#include <QObject>

class MainWindowPortsTest : public QObject
{
    Q_OBJECT

private slots:
    void importPortsFromEditor_multilineScript_populatesTable();
    void importPortsFromEditor_targetLayer_onlyToLayerFilled();
    void addAndRemovePorts_flow_works();
    void toggleSubLayerNames_convertsNumericLayersToNamesAndBack();

    void switchSimTool_updatesState();
    void defaultScriptGeneration_openems_and_palace_notEmpty();
    void setInputs_updatesState_withoutCrash();
    void boundaryOptions_updateOnToolChange_withoutCrash();
    void saveAction_writesScriptToFile_and_updatesState();
};

#endif // TST_MAINWINDOW_PORTS_H
