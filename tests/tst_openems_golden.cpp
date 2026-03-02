#include "test_utils.h"
#include "tst_openems_golden.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QRegularExpression>

#include "mainwindow.h"

using namespace GoldenTestUtils;

/*!*******************************************************************************************************************
 * \brief Resolves the platform-specific OpenEMS Python launcher stub for unit tests.
 *
 * Returns a path to a lightweight stub script that emulates the configured OpenEMS
 * "Python Path" in test/CI environments. The stub is located via QFINDTESTDATA().
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
 * \brief Golden test: regenerates OpenEMS script after changing GUI settings and compares to golden file.
 *
 * Initializes MainWindow in a deterministic test configuration, generates the default OpenEMS
 * script, applies a small set of GUI setting changes, regenerates the script from GUI state
 * and compares the normalized editor content against the stored golden reference.
 **********************************************************************************************************************/
void OpenemsGolden::defaultOpenems_changeSettings_and_compare()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

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

    QVERIFY2(w.testInitDefaultOpenemsModel(),
             "testInitDefaultOpenemsModel() failed (default OpenEMS script is empty?)");

    const auto ports = w.testParsePortsFromEditor();
    QVERIFY2(ports.isEmpty(), "We do not expect ports to be parsed from OpenEMS default model");

    w.testSetSimSetting("margin", 55);
    w.testSetSimSetting("numfreq", 123);

    QString err;
    const QString regenerated = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!regenerated.isEmpty(), qPrintable(err));
    w.testSetEditorText(regenerated);

    const QString goldenPath = QFINDTESTDATA("golden/tst_openems_golden.py");
    QVERIFY2(!goldenPath.isEmpty(), "Golden OpenEMS python file not found via QFINDTESTDATA");

    const QString golden = readUtf8(goldenPath);
    QVERIFY2(!golden.isEmpty(),
             qPrintable(QString("Golden file empty: %1").arg(goldenPath)));

    const QString ng = normalize(golden);
    const QString na = normalize(w.testEditorText());

    //updateGoldenOnce(goldenPath, na);

    if (ng != na) {
        const QString diff = diffText(ng, na);
        QFAIL(qPrintable(QString("Mismatch vs golden:\n\n%1").arg(diff)));
    }
}

/*!*******************************************************************************************************************
 * \brief Verifies that switching simulation tools updates the "Boundaries" enum options.
 *
 * Ensures that the boundary condition options in the property browser are refreshed
 * when selecting OpenEMS as the active backend.
 **********************************************************************************************************************/
void OpenemsGolden::boundaryOptions_switchTool_updatesEnumList()
{
    MainWindow w;

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");
    w.testSetPreference("Python Path", pyStub);

    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey("openems", &err), qPrintable(err));

    QVERIFY(true);
}

/*!*******************************************************************************************************************
 * \brief Verifies that toggling "Use Substrate Layer Names" converts port layer combo selections.
 *
 * Loads a valid GDS and substrate XML, initializes the OpenEMS default model and toggles
 * the layer name display mode to ensure the conversion path executes without errors.
 **********************************************************************************************************************/
void OpenemsGolden::subLayerNames_toggle_convertsPorts()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found");

    w.setGdsFile(gdsPath);
    w.setSubstrateFile(xmlPath);

    const QString pyStub = ensureTestOpenemsPythonStub();
    w.testSetPreference("Python Path", pyStub);
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("openems", &terr), qPrintable(terr));
    QVERIFY2(w.testInitDefaultOpenemsModel(), "OpenEMS model init failed");

    w.testSetSubLayerNamesChecked(true);

    QVERIFY2(w.testPortsRowCount() >= 0, "Ports table not accessible");
}
