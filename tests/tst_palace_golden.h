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
    void buildPalaceRunContext_scriptMode_missingLauncherFile_fails();
    void buildPalaceRunContext_nativeMode_missingInstallPath_fails();
    void buildPalaceRunContext_scriptMode_succeeds();

    void detectRunDirFromLog_parsesSimulationDirectory();
    void guessDefaultPalaceRunDir_returnsExistingPathOnly();
    void chooseSearchDir_prefersDetectedDir();
    void findPalaceConfigJson_prefersConfigJson_and_handlesEmptyDir();

    void buildPalaceRunContext_usesConfiguredPython();
    void logPalaceStartupInfo_writesExpectedText();
    void preparePalaceSolverLaunch_emptyConfig_fails();
    void failPalaceSolver_resetsPhase_and_process();
    void onPalaceProcessFinished_pythonPhase_nonZeroExit_logsFailure();
    void onPalaceProcessFinished_solverPhase_logsFinish();
    void startPalaceSolverStage_missingSearchDir_fails();
    void startPalaceSolverStage_missingConfig_fails();

#ifdef Q_OS_WIN
    void parsePhysicalCoresFromLscpuCsv_countsUniqueSocketCorePairs();
#endif
};

#endif // TST_PALACE_GOLDEN_H
