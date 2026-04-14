#ifndef TST_PALACE_GOLDEN_H
#define TST_PALACE_GOLDEN_H

#include <QObject>

class PalaceGolden : public QObject
{
    Q_OBJECT

private slots:
    void defaultPalace_changeSettings_ports_and_compare();
    void runPalace_headless_startsProcess_and_logsOutput();

    void buildPalaceRunContext_wrongTool_fails();
    void buildPalaceRunContext_missingModel_fails();
    void buildPalaceRunContext_scriptMode_missingLauncher_fails();
    void buildPalaceRunContext_nativeMode_missingInstallPath_fails();
    void buildPalaceRunContext_scriptMode_succeeds();

    void detectRunDirFromLog_parsesSimulationDirectory();
    void guessDefaultPalaceRunDir_returnsExistingPathOnly();
    void chooseSearchDir_prefersDetectedDir();
    void findPalaceConfigJson_prefersConfigJson_and_handlesEmptyDir();

#ifdef Q_OS_WIN
    void parsePhysicalCoresFromLscpuCsv_countsUniqueSocketCorePairs();
#endif
};

#endif // TST_PALACE_GOLDEN_H
