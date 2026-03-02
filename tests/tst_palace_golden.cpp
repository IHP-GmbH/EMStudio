#include "test_utils.h"
#include "tst_palace_golden.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QCoreApplication>

#include "mainwindow.h"

using namespace GoldenTestUtils;

static QString ensureTestPalaceLauncher()
{
#ifdef Q_OS_WIN
    return QFINDTESTDATA("tools/palace_launcher_stub.cmd");
#else
    return QFINDTESTDATA("tools/palace_launcher_stub.sh");
#endif
}

void PalaceGolden::defaultPalace_changeSettings_ports_and_compare()
{
    MainWindow w;

    // ------------------------------------------------------------------
    // Set GDS and XML from tests/golden first (so default script uses them)
    // ------------------------------------------------------------------
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
    // Ensure executable bit on Linux
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

    // ------------------------------------------------------------------
    // Generate the default Palace model (deterministic, no dialogs)
    // This MUST populate the editor
    // ------------------------------------------------------------------
    QVERIFY2(w.testInitDefaultPalaceModel(),
             "testInitDefaultPalaceModel() failed (default Palace script is empty?)");

    // ------------------------------------------------------------------
    // Ports must be parsed from the script currently in the editor
    // ------------------------------------------------------------------
    {
        const auto ports = w.testParsePortsFromEditor();
        QVERIFY2(ports.isEmpty(), "No ports shall be parsed from editor after default model init");
    }

    // ------------------------------------------------------------------
    // Change a GUI setting (direct write into m_simSettings)
    // ------------------------------------------------------------------
    w.testSetSimSetting("margin", 51);

    // ------------------------------------------------------------------
    // Regenerate script from GUI state and write it back to editor
    // (so we compare editor content)
    // ------------------------------------------------------------------
    QString err;
    const QString regenerated = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!regenerated.isEmpty(), qPrintable(err));
    w.testSetEditorText(regenerated);

    // ------------------------------------------------------------------
    // Load golden and compare EDITOR CONTENT
    // ------------------------------------------------------------------
    const QString goldenPath = QFINDTESTDATA("golden/tst_palace_golden.py");
    QVERIFY2(!goldenPath.isEmpty(), "Golden python file not found via QFINDTESTDATA");

    const QString golden = readUtf8(goldenPath);
    QVERIFY2(!golden.isEmpty(),
             qPrintable(QString("Golden file empty: %1").arg(goldenPath)));

    const QString ng = normalize(golden);
    const QString na = normalize(w.testEditorText());

    if (ng != na) {

        /*QString werr;
        const bool ok = writeUtf8Atomic(goldenPath, na, &werr);
        QVERIFY2(ok, qPrintable(QString("Failed to write golden: %1").arg(werr)));
        return; // make test PASS after golden update*/

        const QString diff = diffText(ng, na);
        QFAIL(qPrintable(QString("Mismatch vs golden:\n\n%1").arg(diff)));
    }
}
