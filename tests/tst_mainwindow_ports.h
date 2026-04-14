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
};

#endif // TST_MAINWINDOW_PORTS_H
