#ifndef TST_OPENEMS_GOLDEN_H
#define TST_OPENEMS_GOLDEN_H

#include <QObject>

class OpenemsGolden : public QObject
{
    Q_OBJECT

private slots:
    void defaultOpenems_changeSettings_and_compare();
    void boundaryOptions_switchTool_updatesEnumList();
    void subLayerNames_toggle_convertsPorts();
};

#endif // TST_OPENEMS_GOLDEN_H
