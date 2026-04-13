#include "test_utils.h"
#include "tst_palace_golden.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QCoreApplication>

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
