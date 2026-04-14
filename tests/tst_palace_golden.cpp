#include "test_utils.h"
#include "tst_palace_golden.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

#include "mainwindow.h"

using namespace GoldenTestUtils;

/*!*******************************************************************************************************************
 * \brief Resolves the platform-specific Palace launcher stub for unit tests.
 *
 * \return Absolute path to the Palace launcher stub script, or empty string if not found.
 **********************************************************************************************************************/
static QString ensureTestPalaceLauncher()
{
#ifdef Q_OS_WIN
    return QFINDTESTDATA("tools/palace_launcher_stub.cmd");
#else
    return QFINDTESTDATA("tools/palace_launcher_stub.sh");
#endif
}

/*!*******************************************************************************************************************
 * \brief Resolves the platform-specific OpenEMS Python launcher stub for unit tests.
 *
 * \return Absolute path to the OpenEMS python stub script, or empty string if not found.
 **********************************************************************************************************************/
static QString ensureTestOpenemsPythonStub()
{
#ifdef Q_OS_WIN
    return QFINDTESTDATA("tools/openems_python_stub.cmd");
#else
    return QFINDTESTDATA("tools/openems_python_stub.sh");
#endif
}

/*!*******************************************************************************************************************
 * \brief Golden test: regenerates Palace script after changing GUI settings and compares to golden file.
 *
 * Initializes MainWindow in a deterministic test configuration, generates the default Palace
 * script, applies a small GUI setting change, regenerates the script from GUI state
 * and compares the normalized editor content against the stored golden reference.
 **********************************************************************************************************************/
void PalaceGolden::defaultPalace_changeSettings_ports_and_compare()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Test Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());

    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));

    QVERIFY2(w.testInitDefaultPalaceModel(),
             "testInitDefaultPalaceModel() failed (default Palace script is empty?)");

    {
        const auto ports = w.testParsePortsFromEditor();
        QVERIFY2(ports.isEmpty(), "No ports shall be parsed from editor after default model init");
    }

    w.testSetSimSetting("margin", 51);

    QString err;
    const QString regenerated = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!regenerated.isEmpty(), qPrintable(err));
    w.testSetEditorText(regenerated);

    const QString goldenPath = QFINDTESTDATA("golden/tst_palace_golden.py");
    QVERIFY2(!goldenPath.isEmpty(), "Golden python file not found via QFINDTESTDATA");

    const QString golden = readUtf8(goldenPath);
    QVERIFY2(!golden.isEmpty(),
             qPrintable(QString("Golden file empty: %1").arg(goldenPath)));

    const QString ng = normalize(golden);
    const QString na = normalize(w.testEditorText());

    if (ng != na) {
        const QString diff = diffText(ng, na);
        QFAIL(qPrintable(QString("Mismatch vs golden:\n\n%1").arg(diff)));
    }
}

/*!*******************************************************************************************************************
 * \brief Verifies that runPalace() starts a process and writes log output in headless test mode.
 *
 * The test does not require a real Palace installation. Instead, it injects a lightweight
 * launcher stub through PALACE_RUN_SCRIPT, enables launcher mode, generates a deterministic
 * Palace model, writes it to a temporary file, and starts runPalace(false).
 *
 * The test verifies:
 *  - the simulation process starts,
 *  - the process finishes within timeout,
 *  - the log contains the preprocessing start banner,
 *  - the log contains launcher information,
 *  - the log contains the finish banner.
 **********************************************************************************************************************/
void PalaceGolden::runPalace_headless_startsProcess_and_logsOutput()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Test Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());

    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));

    QVERIFY2(w.testInitDefaultPalaceModel(),
             "testInitDefaultPalaceModel() failed");

    QString err;
    const QString regenerated = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!regenerated.isEmpty(), qPrintable(err));

    w.testSetEditorText(regenerated);

    const QString scriptPath =
        w.testWriteEditorToTempPyFile("palace_run_test.py");
    QVERIFY2(!scriptPath.isEmpty(),
             "Failed to create temporary Palace python script");

    w.testSetRunPythonScriptPath(scriptPath);
    w.testSetSimSetting("RunDir", QFileInfo(scriptPath).absolutePath());

    w.testRunPalace(false);

    const bool finished = QTest::qWaitFor([&w]() {
        return !w.testIsSimulationRunning();
    }, 5000);

    QVERIFY2(finished, "Palace process did not finish within timeout");

    const QString log = w.testSimulationLogText();

    QVERIFY2(log.contains("Starting Palace Python preprocessing"),
             qPrintable(QString("Missing preprocessing start message:\n%1").arg(log)));

    QVERIFY2(log.contains("Launcher script"),
             qPrintable(QString("Missing launcher information:\n%1").arg(log)));

    QVERIFY2(log.contains("Palace launcher finished") ||
                 log.contains("Palace Python preprocessing finished") ||
                 log.contains("finished with exit code"),
             qPrintable(QString("Missing finish message:\n%1").arg(log)));
}

/*!*******************************************************************************************************************
 * \brief Verifies that buildPalaceRunContext() fails when current tool is not Palace.
 **********************************************************************************************************************/
void PalaceGolden::buildPalaceRunContext_wrongTool_fails()
{
    MainWindow w;

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("Python Path", pyStub);
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("openems", &terr), qPrintable(terr));

    QString err;
    QVERIFY2(!w.testBuildPalaceRunContext(&err), "Context build shall fail for non-Palace tool");
    QVERIFY2(err.contains("not Palace"), qPrintable(err));
}

/*!*******************************************************************************************************************
 * \brief Verifies that buildPalaceRunContext() fails when Palace model path is missing.
 **********************************************************************************************************************/
void PalaceGolden::buildPalaceRunContext_missingModel_fails()
{
    MainWindow w;

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());

    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));

    QString err;
    QVERIFY2(!w.testBuildPalaceRunContext(&err), "Context build shall fail when model is missing");
    QVERIFY2(err.contains("model script"), qPrintable(err));
}

/*!*******************************************************************************************************************
 * \brief Verifies that launcher mode requires PALACE_RUN_SCRIPT.
 **********************************************************************************************************************/
void PalaceGolden::buildPalaceRunContext_scriptMode_missingLauncher_fails()
{
    MainWindow w;

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temporary directory");

    const QString modelPath = dir.filePath("palace_model.py");
    {
        QFile f(modelPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "print('dummy')\n";
    }

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));

    w.testSetRunPythonScriptPath(modelPath);
    w.testSetPreference("PALACE_RUN_SCRIPT", QString());

    QString err;
    QVERIFY2(!w.testBuildPalaceRunContext(&err), "Context build shall fail without PALACE_RUN_SCRIPT");
    QVERIFY2(err.contains("PALACE_RUN_SCRIPT"), qPrintable(err));
}

/*!*******************************************************************************************************************
 * \brief Verifies that native Palace mode requires PALACE_INSTALL_PATH.
 **********************************************************************************************************************/
void PalaceGolden::buildPalaceRunContext_nativeMode_missingInstallPath_fails()
{
    MainWindow w;

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temporary directory");

    const QString modelPath = dir.filePath("palace_model.py");
    {
        QFile f(modelPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "print('dummy')\n";
    }

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));

    w.testSetRunPythonScriptPath(modelPath);
    w.testSetPreference("PALACE_RUN_MODE", 0);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());

    QString err;
    QVERIFY2(!w.testBuildPalaceRunContext(&err), "Context build shall fail without PALACE_INSTALL_PATH");
    QVERIFY2(err.contains("PALACE_INSTALL_PATH"), qPrintable(err));
}

/*!*******************************************************************************************************************
 * \brief Verifies that launcher mode successfully builds a Palace run context.
 **********************************************************************************************************************/
void PalaceGolden::buildPalaceRunContext_scriptMode_succeeds()
{
    MainWindow w;

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temporary directory");

    const QString modelPath = dir.filePath("my_model.py");
    {
        QFile f(modelPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "print('dummy')\n";
    }

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());
    w.testSetPreference("PALACE_PYTHON", QString());

    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));

    w.testSetRunPythonScriptPath(modelPath);

    QString err;
    QString simKeyLower;
    QString modelWin;
    QString launcherWin;
    int runMode = -1;
    QString baseName;
    QString runDirGuessWin;
    QString pythonCmd;
    QString modelDirLinux;
    QString modelLinux;

    QVERIFY2(w.testBuildPalaceRunContext(&err,
                                         &simKeyLower,
                                         &modelWin,
                                         &launcherWin,
                                         &runMode,
                                         &baseName,
                                         &runDirGuessWin,
                                         nullptr,
                                         nullptr,
                                         &pythonCmd,
                                         nullptr,
                                         &modelDirLinux,
                                         &modelLinux),
             qPrintable(err));

    QCOMPARE(simKeyLower, QString("palace"));
    QCOMPARE(modelWin, modelPath);
    QVERIFY2(!launcherWin.isEmpty(), "Launcher path shall not be empty");
    QCOMPARE(runMode, 1);
    QCOMPARE(baseName, QString("my_model"));
    QVERIFY2(runDirGuessWin.contains("palace_model"), qPrintable(runDirGuessWin));
    QCOMPARE(pythonCmd, QString("python3"));
    QVERIFY2(!modelDirLinux.isEmpty(), "Model dir shall not be empty");
    QVERIFY2(!modelLinux.isEmpty(), "Model path shall not be empty");
}

/*!*******************************************************************************************************************
 * \brief Verifies that detectRunDirFromLog() parses the Palace simulation data directory.
 **********************************************************************************************************************/
void PalaceGolden::detectRunDirFromLog_parsesSimulationDirectory()
{
    MainWindow w;

    const QString pathText = "C:/temp/palace_model/t1_data";
    w.testSetSimulationLogText(QString("foo\nSimulation data directory: %1\nbar\n").arg(pathText));

    const QString detected = w.testDetectRunDirFromLog();
    QVERIFY2(!detected.isEmpty(), "Expected run directory to be detected");
}

/*!*******************************************************************************************************************
 * \brief Verifies that guessDefaultPalaceRunDir() returns an existing expected path and empty otherwise.
 **********************************************************************************************************************/
void PalaceGolden::guessDefaultPalaceRunDir_returnsExistingPathOnly()
{
    MainWindow w;

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temporary directory");

    const QString modelPath = dir.filePath("abc.py");
    {
        QFile f(modelPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "print('dummy')\n";
    }

    const QString expectedDir = dir.filePath("palace_model/abc_data");
    QVERIFY2(QDir().mkpath(expectedDir), "Failed to create expected Palace run directory");

    QCOMPARE(w.testGuessDefaultPalaceRunDir(modelPath, "abc"), expectedDir);
    QCOMPARE(w.testGuessDefaultPalaceRunDir(modelPath, "missing"), QString());
}

/*!*******************************************************************************************************************
 * \brief Verifies that chooseSearchDir() prefers detected run directory over default one.
 **********************************************************************************************************************/
void PalaceGolden::chooseSearchDir_prefersDetectedDir()
{
    MainWindow w;

    QCOMPARE(w.testChooseSearchDir("C:/detected", "C:/default"), QString("C:/detected"));
    QCOMPARE(w.testChooseSearchDir(QString(), "C:/default"), QString("C:/default"));
}

/*!*******************************************************************************************************************
 * \brief Verifies that findPalaceConfigJson() prefers config.json and handles empty directories.
 **********************************************************************************************************************/
void PalaceGolden::findPalaceConfigJson_prefersConfigJson_and_handlesEmptyDir()
{
    MainWindow w;

    QTemporaryDir emptyDir;
    QVERIFY2(emptyDir.isValid(), "Failed to create empty temp directory");
    QCOMPARE(w.testFindPalaceConfigJson(emptyDir.path()), QString());

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "Failed to create temp directory");

    const QString otherJson = dir.filePath("zzz.json");
    {
        QFile f(otherJson);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "{}\n";
    }

    const QString configJson = dir.filePath("config.json");
    {
        QFile f(configJson);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "{ \"config\": true }\n";
    }

    QCOMPARE(w.testFindPalaceConfigJson(dir.path()), configJson);
}

#ifdef Q_OS_WIN
/*!*******************************************************************************************************************
 * \brief Verifies that parsePhysicalCoresFromLscpuCsv() counts unique socket/core pairs.
 **********************************************************************************************************************/
void PalaceGolden::parsePhysicalCoresFromLscpuCsv_countsUniqueSocketCorePairs()
{
    MainWindow w;

    const QString csv =
        "# comment\n"
        "0,0\n"
        "1,0\n"
        "0,0\n"
        "1,0\n"
        "0,1\n"
        "1,1\n"
        "badline\n"
        ",\n";

    QCOMPARE(w.testParsePhysicalCoresFromLscpuCsv(csv), QString("4"));
    QCOMPARE(w.testParsePhysicalCoresFromLscpuCsv("# only comments\n"), QString());
}
#endif
