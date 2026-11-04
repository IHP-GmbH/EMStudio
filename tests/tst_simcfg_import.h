/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_SIMCFG_IMPORT_H
#define TST_SIMCFG_IMPORT_H

#include <QObject>

class SimcfgImportTest : public QObject
{
    Q_OBJECT

private slots:
    void read_convertsFrequenciesAndSkipsGuiKeys();
    void read_rejectsOtherFiles();
    void importNewModel_matchesGolden();
    void importExistingModel_updatesOnlyLayoutAndPorts();
    void settingsFor_openemsTakesSharedKeysOnly();
    void importNewOpenemsModel_matchesGolden();
};

#endif // TST_SIMCFG_IMPORT_H
