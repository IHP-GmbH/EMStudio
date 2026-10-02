#include "tst_mainwindow_ports.h"

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QMenu>
#include <QLineEdit>
#include <QComboBox>
#include <QAction>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QListWidget>
#include <QSlider>
#include <QWheelEvent>
#include <QSignalSpy>
#include <QShortcut>

#include "mainwindow.h"
#include "layoutlayerpanel.h"
#include "pythonparser.h"
#include "test_utils.h"

/*!*******************************************************************************************************************
 * \brief Resolves the platform-specific OpenEMS Python launcher stub for unit tests.
 *
 * \return Absolute path to the OpenEMS launcher stub script, or empty string if not found.
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
 * \brief Verifies that testParsePortsFromEditor() parses multiline simulation_port() calls correctly.
 *
 * The test validates parser output independently from UI combo-box population.
 * It checks that:
 *  - two ports are parsed,
 *  - numeric and string fields are extracted correctly,
 *  - direction is preserved,
 * and then verifies that importing them creates table rows with the expected
 * numeric cells and direction combo texts.
 **********************************************************************************************************************/
void MainWindowPortsTest::importPortsFromEditor_multilineScript_populatesTable()
{
    MainWindow w;

    const QString script =
        "simulation_ports.add_port(simulation_setup.simulation_port(\n"
        "    portnumber=1,\n"
        "    voltage=1.25,\n"
        "    port_Z0=75,\n"
        "    source_layernum=8,\n"
        "    from_layername='Metal1',\n"
        "    to_layername='TopMetal1',\n"
        "    direction='z'\n"
        "))\n"
        "\n"
        "simulation_ports.add_port(simulation_setup.simulation_port(\n"
        "    portnumber=2,\n"
        "    voltage=0.5,\n"
        "    port_Z0=50,\n"
        "    source_layername=\"Metal2\",\n"
        "    from_layername=\"Via1\",\n"
        "    to_layername=\"TopMetal2\",\n"
        "    direction=\"-z\"\n"
        "))\n";

    w.testSetEditorText(script);

    const auto ports = w.testParsePortsFromEditor();
    QCOMPARE(ports.size(), 2);

    QCOMPARE(ports[0].portnumber, 1);
    QCOMPARE(ports[0].voltage, 1.25);
    QCOMPARE(ports[0].z0, 75.0);
    QCOMPARE(ports[0].sourceLayer, QString("8"));
    QCOMPARE(ports[0].sourceIsNumber, true);
    QCOMPARE(ports[0].fromLayer, QString("Metal1"));
    QCOMPARE(ports[0].toLayer, QString("TopMetal1"));
    QCOMPARE(ports[0].direction, QString("z"));

    QCOMPARE(ports[1].portnumber, 2);
    QCOMPARE(ports[1].voltage, 0.5);
    QCOMPARE(ports[1].z0, 50.0);
    QCOMPARE(ports[1].sourceLayer, QString("Metal2"));
    QCOMPARE(ports[1].sourceIsNumber, false);
    QCOMPARE(ports[1].fromLayer, QString("Via1"));
    QCOMPARE(ports[1].toLayer, QString("TopMetal2"));
    QCOMPARE(ports[1].direction, QString("-z"));

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setSubstrateFile(xmlPath);
    w.testImportPortsFromEditor();

    QCOMPARE(w.testPortsRowCount(), 2);

    QCOMPARE(w.testPortCellText(0, 0), QString("1"));
    QCOMPARE(w.testPortCellText(0, 1), QString("1.25"));
    QCOMPARE(w.testPortCellText(0, 2), QString("75"));
    QCOMPARE(w.testPortComboText(0, 6), QString("z"));

    QCOMPARE(w.testPortCellText(1, 0), QString("2"));
    QCOMPARE(w.testPortCellText(1, 1), QString("0.5"));
    QCOMPARE(w.testPortCellText(1, 2), QString("50"));
    QCOMPARE(w.testPortComboText(1, 6), QString("-z"));
}

/*!*******************************************************************************************************************
 * \brief Verifies that target_layername is parsed as fallback into toLayer.
 *
 * The test checks parser-level behavior and then verifies that one row is imported
 * into the UI table. Missing direction shall become "z" in the UI.
 **********************************************************************************************************************/
void MainWindowPortsTest::importPortsFromEditor_targetLayer_onlyToLayerFilled()
{
    MainWindow w;

    const QString script =
        "simulation_ports.add_port(simulation_setup.simulation_port(\n"
        "    portnumber=7,\n"
        "    voltage=1,\n"
        "    port_Z0=50,\n"
        "    source_layernum=6,\n"
        "    target_layername='TopMetal2'\n"
        "))\n";

    w.testSetEditorText(script);

    const auto ports = w.testParsePortsFromEditor();
    QCOMPARE(ports.size(), 1);

    QCOMPARE(ports[0].portnumber, 7);
    QCOMPARE(ports[0].voltage, 1.0);
    QCOMPARE(ports[0].z0, 50.0);
    QCOMPARE(ports[0].sourceLayer, QString("6"));
    QCOMPARE(ports[0].sourceIsNumber, true);
    QCOMPARE(ports[0].fromLayer, QString(""));
    QCOMPARE(ports[0].toLayer, QString("TopMetal2"));
    QCOMPARE(ports[0].direction, QString(""));

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setSubstrateFile(xmlPath);
    w.testImportPortsFromEditor();

    QCOMPARE(w.testPortsRowCount(), 1);

    QCOMPARE(w.testPortCellText(0, 0), QString("7"));
    QCOMPARE(w.testPortCellText(0, 1), QString("1"));
    QCOMPARE(w.testPortCellText(0, 2), QString("50"));
    QCOMPARE(w.testPortComboText(0, 6), QString("z"));
}

/*!*******************************************************************************************************************
 * \brief Verifies that a via port's to-layer on a "sheet" reference plane survives an import/export round trip.
 *
 * Regression test for https://github.com/IHP-GmbH/EMStudio/issues/22: readSubstrateLayers() used to
 * only collect substrate layers of Type="conductor", so a sheet-type reference plane (used as an
 * artificial ground plane for via ports, with a Material that is never declared in <Materials> at
 * all) was missing from the port "to layer" combo box choices. QComboBox::setCurrentText() then
 * silently failed to select it, the to-layer combo stayed empty, and regenerating the script
 * collapsed the port to target_layername=<fromLayer> instead of the original from_layername/
 * to_layername pair.
 **********************************************************************************************************************/
void MainWindowPortsTest::importPortsFromEditor_sheetReferencePlane_toLayerPreservedOnRoundTrip()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um_with_ref_plane.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    w.testSetPreference("Python Path", pyStub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey("openems", &err), qPrintable(err));

    const QString script =
        "simulation_ports.add_port(simulation_setup.simulation_port(\n"
        "    portnumber=3,\n"
        "    voltage=1,\n"
        "    port_Z0=50,\n"
        "    source_layernum=203,\n"
        "    from_layername='Metal2',\n"
        "    to_layername='REF_FOR_TRANSISTOR',\n"
        "    direction='z'\n"
        "))\n";

    w.testSetEditorText(script);
    w.testImportPortsFromEditor();

    QCOMPARE(w.testPortsRowCount(), 1);
    QCOMPARE(w.testPortComboText(0, 4), QString("Metal2"));
    QCOMPARE(w.testPortComboText(0, 5), QString("REF_FOR_TRANSISTOR"));

    QString genErr;
    const QString generated = w.testGenerateScriptFromGuiState(&genErr);
    QVERIFY2(!generated.isEmpty(), qPrintable(genErr));

    QVERIFY2(generated.contains("from_layername='Metal2'"),
             "Regenerated script lost the via port's from_layername");
    QVERIFY2(generated.contains("to_layername='REF_FOR_TRANSISTOR'"),
             "Regenerated script lost the via port's to_layername for the sheet reference plane");
    QVERIFY2(!generated.contains("target_layername"),
             "Regenerated script wrongly collapsed the port to target_layername");
}

/*!*******************************************************************************************************************
 * \brief Multiline Volker-style add_port() must not grow extra port-2 rows on each GUI→script sync.
 *
 * findPortBlocks used to stop at the first wrapped argument line, leaving orphaned
 * simulation_ports.add_port(...) tails that re-imported as duplicate Source Layer 202 rows.
 **********************************************************************************************************************/
void MainWindowPortsTest::replacePortSection_multilineVolkerStyle_noDuplicatesOnResync()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS missing");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML missing");

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub missing");
#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);
    w.testSetPreference("Python Path", pyStub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey("openems", &err), qPrintable(err));

    // Same wrapping style as gds2openEMS more_accurate_models_L6n2/run_L6n2_*.py
    const QString script = QStringLiteral(
        "simulation_ports = simulation_setup.all_simulation_ports()\n"
        "\n"
        "simulation_ports.add_port(simulation_setup.simulation_port(portnumber=1, \n"
        "                                                           voltage=1, \n"
        "                                                           port_Z0=50, \n"
        "                                                           source_layernum=201, \n"
        "                                                           from_layername='SUBGND', \n"
        "                                                           to_layername='TopMetal1', \n"
        "                                                           direction='z'))\n"
        "\n"
        "simulation_ports.add_port(simulation_setup.simulation_port(portnumber=2, \n"
        "                                                           voltage=1, \n"
        "                                                           port_Z0=50, \n"
        "                                                           source_layernum=202, \n"
        "                                                           from_layername='SUBGND', \n"
        "                                                           to_layername='TopMetal1', \n"
        "                                                           direction='z'))\n"
        "\n"
        "# ======================== simulation ================================\n");

    w.testSetEditorText(script);
    w.testImportPortsFromEditor();
    QCOMPARE(w.testPortsRowCount(), 2);

    for (int pass = 0; pass < 4; ++pass) {
        QString genErr;
        const QString out = w.testGenerateScriptFromGuiState(&genErr);
        QVERIFY2(!out.isEmpty(), qPrintable(genErr));
        w.testSetEditorText(out);

        const auto parsed = w.testParsePortsFromEditor();
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed[0].portnumber, 1);
        QCOMPARE(parsed[1].portnumber, 2);
        QCOMPARE(parsed[1].sourceLayer, QStringLiteral("202"));

        QCOMPARE(out.count(QStringLiteral("simulation_ports.add_port")), 2);
        QCOMPARE(out.count(QStringLiteral("source_layernum=202")), 1);
    }
}

/*!*******************************************************************************************************************
 * \brief Verifies manual add/remove operations on the ports table.
 *
 * The test checks:
 *  - adding the first port creates one row,
 *  - default values are initialized correctly,
 *  - adding another port increments numbering,
 *  - removing the selected port works,
 *  - removing all ports clears the table.
 **********************************************************************************************************************/
void MainWindowPortsTest::addAndRemovePorts_flow_works()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setSubstrateFile(xmlPath);

    QCOMPARE(w.testPortsRowCount(), 0);

    w.testClickAddPort();

    QCOMPARE(w.testPortsRowCount(), 1);
    QCOMPARE(w.testPortCellText(0, 0), QString("1"));
    QCOMPARE(w.testPortCellText(0, 1), QString("1"));
    QCOMPARE(w.testPortCellText(0, 2), QString("50"));
    QCOMPARE(w.testPortComboText(0, 6), QString("z"));

    w.testClickAddPort();

    QCOMPARE(w.testPortsRowCount(), 2);
    QCOMPARE(w.testPortCellText(1, 0), QString("2"));
    QCOMPARE(w.testPortCellText(1, 1), QString("1"));
    QCOMPARE(w.testPortCellText(1, 2), QString("50"));
    QCOMPARE(w.testPortComboText(1, 6), QString("z"));

    w.testSetCurrentPortRow(0);
    w.testClickRemoveCurrentPort();

    QCOMPARE(w.testPortsRowCount(), 1);
    QCOMPARE(w.testPortCellText(0, 0), QString("2"));

    w.testRemoveAllPorts();
    QCOMPARE(w.testPortsRowCount(), 0);
}

/*!*******************************************************************************************************************
 * \brief Verifies conversion between numeric GDS layers and substrate layer names in the ports table.
 *
 * The exact combo text depends on the real XML/GDS mapping, so the test checks
 * that the row stays valid and the direction remains intact while toggling the mode.
 **********************************************************************************************************************/
void MainWindowPortsTest::toggleSubLayerNames_convertsNumericLayersToNamesAndBack()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setSubstrateFile(xmlPath);

    const QString script =
        "simulation_ports.add_port(simulation_setup.simulation_port(\n"
        "    portnumber=3,\n"
        "    voltage=1,\n"
        "    port_Z0=50,\n"
        "    source_layernum=8,\n"
        "    from_layername='Metal1',\n"
        "    to_layername='TopMetal1',\n"
        "    direction='z'\n"
        "))\n";

    w.testSetEditorText(script);
    w.testImportPortsFromEditor();

    QCOMPARE(w.testPortsRowCount(), 1);
    QCOMPARE(w.testPortCellText(0, 0), QString("3"));
    QCOMPARE(w.testPortComboText(0, 6), QString("z"));

    w.testSetSubLayerNamesChecked(true);
    QCOMPARE(w.testPortsRowCount(), 1);
    QCOMPARE(w.testPortCellText(0, 0), QString("3"));
    QCOMPARE(w.testPortComboText(0, 6), QString("z"));

    w.testSetSubLayerNamesChecked(false);
    QCOMPARE(w.testPortsRowCount(), 1);
    QCOMPARE(w.testPortCellText(0, 0), QString("3"));
    QCOMPARE(w.testPortComboText(0, 6), QString("z"));
}

/*!*******************************************************************************************************************
 * \brief Verifies that switching simulation tools through test wrappers works without errors.
 *
 * The test configures both backends via lightweight stubs, refreshes available tools and
 * switches from OpenEMS to Palace. This executes the corresponding UI update paths.
 **********************************************************************************************************************/
void MainWindowPortsTest::switchSimTool_updatesState()
{
    MainWindow w;

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);

    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("Python Path", pyStub);
    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());

    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey("openems", &err), qPrintable(err));
    QCOMPARE(w.testCurrentSimToolKey(), QString("openems"));

    QVERIFY2(w.testSetSimToolKey("palace", &err), qPrintable(err));
    QCOMPARE(w.testCurrentSimToolKey(), QString("palace"));
}

/*!*******************************************************************************************************************
 * \brief Verifies that default OpenEMS and Palace script generation returns non-empty scripts.
 *
 * The test configures both backends, switches between them and initializes default models
 * through the dedicated testing helpers.
 **********************************************************************************************************************/
void MainWindowPortsTest::defaultScriptGeneration_openems_and_palace_notEmpty()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);

    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    w.testSetPreference("Python Path", pyStub);
    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());

    w.refreshSimToolOptionsForTests();

    QString err;

    QVERIFY2(w.testSetSimToolKey("openems", &err), qPrintable(err));
    QVERIFY2(w.testInitDefaultOpenemsModel(), "OpenEMS default model init failed");
    QVERIFY2(!w.testEditorText().trimmed().isEmpty(), "OpenEMS default editor text is empty");

    QVERIFY2(w.testSetSimToolKey("palace", &err), qPrintable(err));
    QVERIFY2(w.testInitDefaultPalaceModel(), "Palace default model init failed");
    QVERIFY2(!w.testEditorText().trimmed().isEmpty(), "Palace default editor text is empty");
}

/*!*******************************************************************************************************************
 * \brief Verifies that setting GDS, top cell and substrate path updates MainWindow state without crashing.
 **********************************************************************************************************************/
void MainWindowPortsTest::setInputs_updatesState_withoutCrash()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    QVERIFY(true);
}

/*!*******************************************************************************************************************
 * \brief Verifies that boundary options update when changing simulation tool.
 *
 * The test configures both backends and switches between them so that
 * boundary enum update and tooltip update paths are executed.
 **********************************************************************************************************************/
void MainWindowPortsTest::boundaryOptions_updateOnToolChange_withoutCrash()
{
    MainWindow w;

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");

    const QString launcherPath = ensureTestPalaceLauncher();
    QVERIFY2(!launcherPath.isEmpty(), "Palace launcher stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);

    QFile::setPermissions(launcherPath,
                          QFile::permissions(launcherPath) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference("Python Path", pyStub);
    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcherPath);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());

    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey("openems", &err), qPrintable(err));
    QVERIFY2(w.testSetSimToolKey("palace", &err), qPrintable(err));

    QVERIFY(true);
}

/*!*******************************************************************************************************************
 * \brief Verifies that Save writes the current script to the configured file path and updates state.
 *
 * The test avoids the Save As dialog by preconfiguring txtRunPythonScript with a temporary file path.
 * It then triggers the normal Save action and checks that:
 *  - the file is created,
 *  - the saved content is not empty,
 *  - the configured path is preserved in the UI,
 *  - the operation completes without errors.
 **********************************************************************************************************************/
void MainWindowPortsTest::saveAction_writesScriptToFile_and_updatesState()
{
    MainWindow w;

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    const QString pyStub = ensureTestOpenemsPythonStub();
    QVERIFY2(!pyStub.isEmpty(), "OpenEMS python stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(pyStub,
                          QFile::permissions(pyStub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    w.testSetPreference("Python Path", pyStub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey("openems", &err), qPrintable(err));

    QVERIFY2(w.testInitDefaultOpenemsModel(), "OpenEMS default model init failed");

    const QString savePath =
        QDir::temp().filePath("emstudio_test_saved_model.py");

    QFile::remove(savePath);

    w.testSetRunPythonScriptLinePath(savePath);
    w.testTriggerSave();

    QVERIFY2(QFileInfo::exists(savePath),
             qPrintable(QString("Expected saved file does not exist: %1").arg(savePath)));

    QFile f(savePath);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QString("Failed to open saved file: %1").arg(savePath)));

    const QString saved = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY2(!saved.trimmed().isEmpty(), "Saved script is empty");

    QFile::remove(savePath);
}

/*!*******************************************************************************************************************
 * \brief Text cells of the Simulation Settings grid (fdump list, other lists, quoted strings)
 *        must survive Save: they used to be skipped when writing the script and then
 *        reverted by the re-parse that Save does.
 **********************************************************************************************************************/
void MainWindowPortsTest::saveAction_keepsEditedTextSettings()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);

    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QVERIFY(w.testInitDefaultPalaceModel());
    // A quoted string setting next to the template's list settings.
    w.testSetEditorText(w.testEditorText()
                        + QStringLiteral("\nsettings['solver_note'] = \"direct\"  # keep comment\n"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savePath = dir.filePath(QStringLiteral("text_settings_model.py"));
    w.testSetRunPythonScriptLinePath(savePath);
    w.testTriggerSave(); // parses the model into the grid
    // A newly saved model (Save As / first Save) is listed first under File > Recent.
    QCOMPARE(w.testRecentPythonModels().value(0), QFileInfo(savePath).absoluteFilePath());

    auto readSaved = [&]() {
        QFile f(savePath);
        return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    };
    QVERIFY(readSaved().contains(QStringLiteral("settings['solver_note'] = \"direct\"")));

    // Edit like the grid does: text values for text cells.
    w.testSetSimSetting(QStringLiteral("fdump"), QStringLiteral("10e9"));            // bare value -> list
    w.testSetSimSetting(QStringLiteral("fpoint"), QStringLiteral("[1.5e9, 2e9]"));   // list with '.'
    w.testSetSimSetting(QStringLiteral("solver_note"), QStringLiteral("it's iterative"));
    w.testTriggerSave();

    const QString saved = readSaved();
    QVERIFY2(saved.contains(QRegularExpression(QStringLiteral(R"(settings\['fdump'\]\s*=\s*\[10e9\])"))),
             qPrintable(saved));
    QVERIFY2(saved.contains(QRegularExpression(QStringLiteral(R"(settings\['fpoint'\]\s*=\s*\[1\.5e9, 2e9\])"))),
             qPrintable(saved));
    QVERIFY2(saved.contains(QStringLiteral("settings['solver_note'] = 'it\\'s iterative'  # keep comment")),
             qPrintable(saved));
    // Untouched list cells keep the script's own text.
    QVERIFY(saved.contains(QRegularExpression(QStringLiteral(R"(settings\['purpose'\]\s*=\s*\[0\])"))));

    // A third Save (values now come from the re-parse) must not change anything.
    w.testTriggerSave();
    QCOMPARE(readSaved(), saved);
}

void MainWindowPortsTest::collectSanityFindings_reportsMissingInputs()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);

    // Fresh window: empty GDS / topcell / no ports â†’ several findings.
    const auto emptyFindings = w.testCollectSanityFindings();
    QVERIFY(emptyFindings.size() >= 2);
    QStringList codes;
    for (const SanityFinding &f : emptyFindings)
        codes << f.code;
    QVERIFY(codes.contains(QStringLiteral("missing_gds"))
            || codes.contains(QStringLiteral("empty_topcell"))
            || codes.contains(QStringLiteral("no_ports")));

    // Load a model with ports that have inconsistent direction layers.
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QVERIFY(w.testInitDefaultPalaceModel());
    w.testSetEditorText(
        QStringLiteral(
            "simulation_ports.add_port(simulation_setup.simulation_port(\n"
            "    portnumber=1, voltage=1.0, port_Z0=50,\n"
            "    source_layernum=1, from_layername='', to_layername='',\n"
            "    direction='x'\n"
            "))\n"));
    w.testImportPortsFromEditor();

    w.setGdsFile(QStringLiteral("C:/definitely/missing/file.gds"));
    w.setTopCell(QString());
    w.setSubstrateFile(QStringLiteral("C:/definitely/missing/stack.xml"));
    w.testSetSimSetting(QStringLiteral("margin"), 5.0);

    auto findings = w.testCollectSanityFindings();
    QVERIFY(findings.size() >= 3);
    codes.clear();
    for (const SanityFinding &f : findings)
        codes << f.code;
    QVERIFY(codes.contains(QStringLiteral("gds_not_found")));
    QVERIFY(codes.contains(QStringLiteral("empty_topcell")));
    QVERIFY(codes.contains(QStringLiteral("substrate_not_found"))
            || codes.contains(QStringLiteral("port_xy_needs_target"))
            || codes.contains(QStringLiteral("no_ports"))
            || codes.contains(QStringLiteral("port_xy_has_from_and_to"))
            || codes.contains(QStringLiteral("port_no_source")));
    QVERIFY(codes.contains(QStringLiteral("margin_small"))
            || codes.contains(QStringLiteral("margin_missing")));

    // Thermal path: empty thermal table warning.
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_thermal"), &err), qPrintable(err));
    findings = w.testCollectSanityFindings();
    codes.clear();
    for (const SanityFinding &f : findings)
        codes << f.code;
    QVERIFY(codes.contains(QStringLiteral("no_thermal_objects"))
            || codes.contains(QStringLiteral("missing_gds"))
            || codes.contains(QStringLiteral("gds_not_found")));

    // Real golden GDS + stackup: exercise layer / mapping checks.
    const QString gdsPath = QFINDTESTDATA("golden/SG13G2_200um.gds");
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    if (!gdsPath.isEmpty() && !xmlPath.isEmpty()) {
        QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
        w.setGdsFile(gdsPath);
        w.setSubstrateFile(xmlPath);
        w.setTopCell(QStringLiteral("TOP"));
        w.testSetSimSetting(QStringLiteral("margin"), 50.0);
        findings = w.testCollectSanityFindings();
        // Path exercised; findings depend on whether GDS layers loaded into the window.
        Q_UNUSED(findings);
    }
}

void MainWindowPortsTest::layoutPreview_withGoldenGds_populatesLayerPanel()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 800);
    w.show();
    QTest::qWait(30);

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS missing");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML missing");

    w.setGdsFile(gdsPath);
    w.setTopCell(QStringLiteral("t1"));
    w.setSubstrateFile(xmlPath);
    w.testRefreshLayoutPreview();
    QTest::qWait(40);

    auto *layoutView = w.findChild<QWidget *>(QStringLiteral("layoutView"));
    QVERIFY(layoutView);

    // Layer panel is created in MainWindow ctor setup.
    auto *list = w.findChild<QListWidget *>();
    // May be the ports or layers list â€” prefer one that looks like layers (checkable items).
    QListWidget *layerList = nullptr;
    for (QListWidget *lw : w.findChildren<QListWidget *>()) {
        if (lw->count() > 0 && (lw->item(0)->flags() & Qt::ItemIsUserCheckable)) {
            layerList = lw;
            break;
        }
    }
    QVERIFY2(layerList, "LayoutLayerPanel list not found");
    QVERIFY(layerList->count() >= 1);

    // Port markers 201 / 202 have no Ports table rows yet: listed as not mapped.
    auto *panel = w.findChild<LayoutLayerPanel *>();
    QVERIFY(panel);
    auto *panelList = panel->findChild<QListWidget *>();
    QVERIFY(panelList);
    QStringList portTexts;
    for (int i = 0; i < panelList->count(); ++i)
        if (panelList->item(i)->text().startsWith(QLatin1Char('P')))
            portTexts << panelList->item(i)->text();
    QVERIFY2(portTexts.contains(QStringLiteral("P1 (not mapped)"))
             && portTexts.contains(QStringLiteral("P2 (not mapped)")),
             qPrintable(portTexts.join(QStringLiteral(" | "))));

    // Order: stack layers from top to bottom, port markers last.
    panel->setUsedLayersOnly(false);
    QStringList texts;
    for (int i = 1; i < panelList->count(); ++i)   // row 0 is "All layers"
        texts << panelList->item(i)->text();
    const QStringList topDown = {QStringLiteral("TopMetal2"), QStringLiteral("TopVia2"),
                                 QStringLiteral("TopMetal1"), QStringLiteral("Metal5"),
                                 QStringLiteral("Metal3"), QStringLiteral("Metal1")};
    int prev = -1;
    for (const QString &name : topDown) {
        const int idx = texts.indexOf(name);
        QVERIFY2(idx > prev, qPrintable(texts.join(QStringLiteral(" | "))));
        prev = idx;
    }
    const int firstPort = texts.indexOf(QStringLiteral("P1 (not mapped)"));
    QVERIFY(firstPort > prev);
    for (int i = firstPort; i < texts.size(); ++i)
        QVERIFY2(texts.at(i).startsWith(QLatin1Char('P')), qPrintable(texts.join(QStringLiteral(" | "))));
    panel->setUsedLayersOnly(true);

    QListWidgetItem *item = layerList->item(0);
    layerList->setCurrentItem(item);
    item->setCheckState(Qt::Unchecked);
    item->setCheckState(Qt::Checked);

    if (auto *slider = w.findChild<QSlider *>()) {
        if (slider->isEnabled())
            slider->setValue(70);
    }

    // Empty / bad paths clear the preview.
    w.setTopCell(QString());
    w.testRefreshLayoutPreview();
    w.setTopCell(QStringLiteral("t1"));
    w.setGdsFile(QStringLiteral("C:/no/such/layout.gds"));
    w.testRefreshLayoutPreview();
    w.setGdsFile(gdsPath);
    w.setTopCell(QStringLiteral("t1"));
    w.testRefreshLayoutPreview();

    // Interact with layout preview widgets if present.
    for (QGraphicsView *gv : w.findChildren<QGraphicsView *>()) {
        if (!gv->objectName().contains(QStringLiteral("layout"), Qt::CaseInsensitive)
            && gv->objectName() != QStringLiteral("layoutView")) {
            // Still exercise the first graphics view on substrate tab.
        }
        QKeyEvent fKey(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
        QApplication::sendEvent(gv, &fKey);
        QWheelEvent wheel(QPointF(50, 50), QPointF(50, 50), QPoint(0, 0), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollPhase::NoScrollPhase, false);
        QApplication::sendEvent(gv->viewport(), &wheel);
        break;
    }
}

/*!*******************************************************************************************************************
 * \brief The settings grid groups settings by the topics of the keyword file (file order), unknown
 *        keys go under "Other", loose variables follow the workflow parameter they are passed to,
 *        and collapsed topics stay collapsed when the grid is rebuilt.
 **********************************************************************************************************************/
void MainWindowPortsTest::settingsGrid_groupsByTopic()
{
    KeywordFileBackup palace(QStringLiteral("palace.csv"));
    QVERIFY(palace.write("no_gui\tBatch\tScript control and output files\tFalse\n"
                         "unit\tUnit\tInput files\t1e-6\tyes\n"
                         "fstart\tStart\tFrequencies\t\tyes\n"
                         "refined_cellsize\tEdge mesh\tMesh size and accuracy\t\tyes\n"
                         "order\tFEM order\tMesh size and accuracy\t2\n"
                         "cells_per_wavelength\tCells\tMesh size and accuracy\t10\n"));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    w.testRefreshKeywordTipsForCurrentTool();

    QVector<PythonParser::WorkflowParam> sig;
    PythonParser::WorkflowParam p;
    p.function = QStringLiteral("setupSimulation");
    p.index = 8;
    p.param = p.keyword = QStringLiteral("refined_cellsize");
    sig << p;
    PythonParser::setWorkflowSignatures(sig);

    const QString script = QStringLiteral(
        "settings = {}\n"
        "settings['order'] = 2\n"
        "settings['zeta'] = 1\n"
        "settings['fstart'] = 1e9\n"
        "settings['unit'] = 1e-6\n"
        "settings['alpha'] = 2\n"
        "settings['cells_per_wavelength'] = 10\n"
        "settings['no_gui'] = True\n"
        "cs_fine = 1\n"
        "FDTD = simulation_setup.setupSimulation(e, s, F, m, d, me, a, mx, cs_fine, mg, u)\n");
    w.testRebuildSettingsGrid(script);
    QCOMPARE(w.testSettingTopicLayout(),
             QStringList({QStringLiteral("Script control and output files: no_gui"),
                          QStringLiteral("Input files: unit"),
                          QStringLiteral("Frequencies: fstart"),
                          QStringLiteral("Mesh size and accuracy: cs_fine, order, cells_per_wavelength"),
                          QStringLiteral("Other: alpha, zeta")}));
    // Required settings are drawn in bold, also a loose variable bound to a required parameter.
    QVERIFY(w.testIsSettingShownRequired(QStringLiteral("unit")));
    QVERIFY(w.testIsSettingShownRequired(QStringLiteral("cs_fine")));
    QVERIFY(!w.testIsSettingShownRequired(QStringLiteral("order")));
    QVERIFY(!w.testIsSettingShownRequired(QStringLiteral("alpha")));

    // Collapse a topic: still collapsed after the next rebuild (Save rebuilds the grid).
    QVERIFY(w.testIsSettingTopicExpanded(QStringLiteral("Input files")));
    w.testSetSettingTopicExpanded(QStringLiteral("Input files"), false);
    w.testRebuildSettingsGrid(script);
    QVERIFY(!w.testIsSettingTopicExpanded(QStringLiteral("Input files")));
    QVERIFY(w.testIsSettingTopicExpanded(QStringLiteral("Other")));

    PythonParser::setWorkflowSignatures({});
}

/*!*******************************************************************************************************************
 * \brief An edit of a setting inside a topic group reaches the script on Save, and the grid keeps
 *        its groups after the re-parse.
 **********************************************************************************************************************/
void MainWindowPortsTest::settingsGrid_nestedEditSurvivesSave()
{
    KeywordFileBackup palace(QStringLiteral("palace.csv"));
    QVERIFY(palace.write("unit\tUnit\tRequired\t1e-6\n"
                         "cells_per_wavelength\tCells\tMesh size and accuracy\t10\n"));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    w.testRefreshKeywordTipsForCurrentTool();
    QVERIFY(w.testInitDefaultPalaceModel());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savePath = dir.filePath(QStringLiteral("grouped_model.py"));
    w.testSetRunPythonScriptLinePath(savePath);
    w.testTriggerSave();

    QVERIFY(w.testSetGridSettingValue(QStringLiteral("cells_per_wavelength"), 12.0));
    w.testTriggerSave();

    QFile f(savePath);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString saved = QString::fromUtf8(f.readAll());
    QVERIFY2(saved.contains(QRegularExpression(QStringLiteral(R"(settings\['cells_per_wavelength'\]\s*=\s*12\b)"))),
             qPrintable(saved));
    QVERIFY(w.testSettingTopicLayout().contains(QStringLiteral("Mesh size and accuracy: cells_per_wavelength")));
}

/*!*******************************************************************************************************************
 * \brief File → New → <tool> selects the tool, puts its default template into the editor, opens the
 *        Main page and forgets the previous model file, so Save can't overwrite it.
 **********************************************************************************************************************/
void MainWindowPortsTest::fileNew_createsTemplateForEachTool()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabSettings"));
    auto *scriptPath = w.findChild<QLineEdit *>(QStringLiteral("txtRunPythonScript"));
    QVERIFY(tabs && scriptPath);

    const QList<QPair<QString, QString>> cases = {
        {QStringLiteral("elmer_thermal"), QStringLiteral("create_elmer_thermal")},
        {QStringLiteral("elmer_em"), QStringLiteral("create_elmer")}};
    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY(!gdsPath.isEmpty() && !xmlPath.isEmpty());
    auto *gdsEdit = w.findChild<QLineEdit *>(QStringLiteral("txtGdsFile"));
    auto *xmlEdit = w.findChild<QLineEdit *>(QStringLiteral("txtSubstrate"));
    auto *topCell = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    auto *ports = w.findChild<QTableWidget *>(QStringLiteral("tblPorts"));
    QVERIFY(gdsEdit && xmlEdit && topCell && ports);

    for (const auto &c : cases) {
        // The previous model: file, GDS, stackup, a port and a thermal object.
        w.testSetRunPythonScriptLinePath(QStringLiteral("/tmp/previous_model.py"));
        w.setGdsFile(gdsPath);
        w.setSubstrateFile(xmlPath);
        ports->setRowCount(1);
        w.testClickAddThermalObject();
        QVERIFY(topCell->count() > 0);

        auto *action = w.findChild<QAction *>(QStringLiteral("actionNew_%1").arg(c.first));
        QVERIFY2(action, qPrintable(c.first));
        QVERIFY(action->isEnabled());  // Elmer tools are always offered
        action->trigger();

        QCOMPARE(w.testCurrentSimToolKey(), c.first);
        QVERIFY2(w.testEditorText().contains(c.second), qPrintable(w.testEditorText().left(400)));
        QCOMPARE(tabs->tabText(0), QStringLiteral("Main"));
        QVERIFY(scriptPath->text().isEmpty());
        QVERIFY(!w.testSettingTopicLayout().isEmpty());  // the grid shows the template's settings

        // Nothing of the previous model's inputs is left, in the GUI or in the new script.
        QVERIFY(gdsEdit->text().isEmpty());
        QVERIFY(xmlEdit->text().isEmpty());
        QCOMPARE(topCell->count(), 0);
        QCOMPARE(ports->rowCount(), 0);
        QVERIFY(!w.testEditorText().contains(QStringLiteral("line_simple_viaport")));
        QVERIFY(!w.testEditorText().contains(QStringLiteral("SG13G2_200um")));
    }

    // Tools missing from the tool list (not configured) are greyed out.
    auto *menu = w.findChild<QMenu *>(QStringLiteral("menuNew"));
    QVERIFY(menu);
    QCOMPARE(menu->actions().size(), 4);
    auto *combo = w.findChild<QComboBox *>(QStringLiteral("cbxSimTool"));
    QVERIFY(combo);
    for (QAction *a : menu->actions())
        QCOMPARE(a->isEnabled(), combo->findData(a->data()) >= 0);
}

/*!*******************************************************************************************************************
 * \brief Saving an OpenEMS model asks for a local 'modules' folder only when the script can need it:
 *        it imports 'modules' and the OpenEMS Python has no gds2openEMS (template: package first).
 **********************************************************************************************************************/
void MainWindowPortsTest::openemsSave_needsModulesOnlyWithoutPackage()
{
#ifdef Q_OS_WIN
    QSKIP("uses shell wrappers for the OpenEMS Python");
#endif
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        QSKIP("python3 not found");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("site/gds2openEMS")));
    QFile init(dir.filePath(QStringLiteral("site/gds2openEMS/__init__.py")));
    QVERIFY(init.open(QIODevice::WriteOnly));
    init.close();
    auto wrapper = [&](const QString &name, const QString &pythonPath) {
        const QString path = dir.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        // -S: no site-packages, so only PYTHONPATH decides whether gds2openEMS is found.
        f.write(QStringLiteral("#!/bin/sh\nPYTHONPATH='%1' exec '%2' -S \"$@\"\n").arg(pythonPath, python).toUtf8());
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        return path;
    };
    const QString withPkg = wrapper(QStringLiteral("py_with.sh"), dir.filePath(QStringLiteral("site")));
    const QString withoutPkg = wrapper(QStringLiteral("py_without.sh"), dir.filePath(QStringLiteral("empty")));
    const QString saveDir = dir.filePath(QStringLiteral("models"));
    QVERIFY(QDir().mkpath(saveDir));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    const QString fallback = QStringLiteral(
        "try:\n    from gds2openEMS import *\nexcept ImportError:\n"
        "    sys.path.insert(0, 'modules')\n    from modules import *\n");

    w.testSetEditorText(fallback);
    w.testSetPreference(QStringLiteral("Python Path"), withPkg);
    QVERIFY(w.testValidateRequiredFolder(saveDir, QStringLiteral("openems")));   // package installed
    w.testSetPreference(QStringLiteral("Python Path"), withoutPkg);
    QVERIFY(!w.testValidateRequiredFolder(saveDir, QStringLiteral("openems")));  // falls back to modules

    // Old scripts that only import the local copy always need the folder.
    w.testSetEditorText(QStringLiteral("from modules import *\n"));
    w.testSetPreference(QStringLiteral("Python Path"), withPkg);
    QVERIFY(!w.testValidateRequiredFolder(saveDir, QStringLiteral("openems")));
    QVERIFY(QDir(saveDir).mkpath(QStringLiteral("modules")));
    QVERIFY(w.testValidateRequiredFolder(saveDir, QStringLiteral("openems")));

    // Package-only scripts never need the folder; Palace never does.
    QVERIFY(QDir(saveDir + QStringLiteral("/modules")).removeRecursively());
    w.testSetEditorText(QStringLiteral("from gds2openEMS import *\n"));
    w.testSetPreference(QStringLiteral("Python Path"), withoutPkg);
    QVERIFY(w.testValidateRequiredFolder(saveDir, QStringLiteral("openems")));
    QVERIFY(w.testValidateRequiredFolder(saveDir, QStringLiteral("palace")));
}

/*!*******************************************************************************************************************
 * \brief The stackup file dialog starts in STACKUP_DIR_OPENEMS (OpenEMS) or STACKUP_DIR_FEM (Palace,
 *        Elmer) when that is an existing folder,
 *        else in the last stackup file's folder, else in the home folder.
 **********************************************************************************************************************/
void MainWindowPortsTest::stackupDialog_startsInConfiguredFolder()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QTemporaryDir fdtd;
    QTemporaryDir fem;
    QVERIFY(fdtd.isValid() && fem.isValid());
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY(!xmlPath.isEmpty());
    const QDir lastDir(QFileInfo(xmlPath).absolutePath());
    QString err;

    w.testSetPreference(QStringLiteral("STACKUP_DIR_OPENEMS"), QString());
    w.testSetPreference(QStringLiteral("STACKUP_DIR_FEM"), QString());
    w.setSubstrateFile(xmlPath);   // remembers the last stackup folder
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_em"), &err), qPrintable(err));
    QCOMPARE(QDir(w.testStackupDialogStartDir()), lastDir);

    // FDTD folder for OpenEMS, FEM folder for Palace / Elmer.
    w.testSetPreference(QStringLiteral("STACKUP_DIR_OPENEMS"), fdtd.path());
    w.testSetPreference(QStringLiteral("STACKUP_DIR_FEM"), fem.path());
    for (const char *key : {"elmer_em", "elmer_thermal"}) {
        QVERIFY2(w.testSetSimToolKey(QString::fromLatin1(key), &err), qPrintable(err));
        QCOMPARE(QDir(w.testStackupDialogStartDir()), QDir(fem.path()));
    }
    if (w.testSetSimToolKey(QStringLiteral("openems"), &err))  // only when OpenEMS is configured
        QCOMPARE(QDir(w.testStackupDialogStartDir()), QDir(fdtd.path()));
    if (w.testSetSimToolKey(QStringLiteral("palace"), &err))
        QCOMPARE(QDir(w.testStackupDialogStartDir()), QDir(fem.path()));

    // Not an existing folder: present behaviour.
    QVERIFY(w.testSetSimToolKey(QStringLiteral("elmer_em"), &err));
    w.testSetPreference(QStringLiteral("STACKUP_DIR_FEM"), fem.path() + QStringLiteral("/missing"));
    QCOMPARE(QDir(w.testStackupDialogStartDir()), lastDir);
    w.testSetPreference(QStringLiteral("STACKUP_DIR_FEM"), xmlPath);  // a file, not a folder
    QCOMPARE(QDir(w.testStackupDialogStartDir()), lastDir);
}

namespace {
/*! Line index of the first line starting with \a prefix (-1 if none). */
int lineOf(const QString &script, const QString &prefix)
{
    const QStringList lines = script.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i)
        if (lines.at(i).startsWith(prefix))
            return i;
    return -1;
}

const QByteArray kGridKeywords =
    "preview_only\tPreview\tScript control and output files\tFalse\n"
    "no_gui\tBatch\tScript control and output files\tFalse\n"
    "unit\tUnit\tInput files\t1e-6\tyes\n"
    "fstart\tStart\tFrequencies\t\tyes\n"
    "fstep\tStep\tFrequencies\n"
    "fpoint\tPoints\tFrequencies\t[]\n"
    "fdump\tDumps\tFrequencies\t[]\n"
    "refined_cellsize\tEdge mesh\tMesh size and accuracy\t\tyes\n"
    "order\tFEM order\tMesh size and accuracy\t2\n"
    "amr_tol\tAMR tolerance\tAdaptive mesh refinement\t1e-2\n";
} // namespace

/*!*******************************************************************************************************************
 * \brief Add setting inserts the line where it fits by topic: after its topic neighbour, before the
 *        first key, after a multi-line statement; custom keys at the end; then shows it in the grid.
 **********************************************************************************************************************/
void MainWindowPortsTest::addSetting_insertsByTopic()
{
    KeywordFileBackup palace(QStringLiteral("palace.csv"));
    QVERIFY(palace.write(kGridKeywords));
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    w.testRefreshKeywordTipsForCurrentTool();

    const QString script = QStringLiteral(
        "settings = {}\n"
        "settings['no_gui'] = False\n"
        "settings['unit'] = 1e-6\n"
        "settings['fstart'] = 1e9\n"
        "settings['fpoint'] = [1e9,\n"
        "                      2e9]\n"
        "settings['refined_cellsize'] = 2\n"
        "settings['my_custom'] = 3\n"
        "x = simulation_setup.create_palace(excite_ports, settings)\n");
    w.testSetEditorText(script);
    w.testRebuildSettingsGrid(script);

    QVERIFY(w.testAddSetting(QStringLiteral("fstep"), QStringLiteral("1e8")));        // between neighbours
    QVERIFY(w.testAddSetting(QStringLiteral("fdump"), QStringLiteral("[]")));         // after multi-line fpoint
    QVERIFY(w.testAddSetting(QStringLiteral("amr_tol"), QStringLiteral("0.01")));     // next topic
    QVERIFY(w.testAddSetting(QStringLiteral("preview_only"), QStringLiteral("False")));  // before the first
    QVERIFY(w.testAddSetting(QStringLiteral("zz_custom"), QStringLiteral("'x'")));    // unknown: at the end

    const QString s = w.testEditorText();
    auto at = [&](const char *key) { return lineOf(s, QStringLiteral("settings['%1']").arg(QLatin1String(key))); };
    QVERIFY2(at("fstart") < at("fstep") && at("fstep") < at("fpoint"), qPrintable(s));
    QCOMPARE(at("fdump"), at("fpoint") + 2);            // after both lines of fpoint
    QCOMPARE(at("amr_tol"), at("refined_cellsize") + 1);
    QVERIFY(lineOf(s, QStringLiteral("settings = {}")) < at("preview_only"));
    QCOMPARE(at("preview_only") + 1, at("no_gui"));
    QCOMPARE(at("zz_custom"), at("my_custom") + 1);
    // Unchanged numbers keep their spelling through the grid sync.
    QVERIFY2(s.contains(QStringLiteral("settings['fstep'] = 1e8\n")), qPrintable(s));
    QVERIFY(s.contains(QStringLiteral("settings['fstart'] = 1e9\n")));
    QVERIFY(s.contains(QStringLiteral("settings['unit'] = 1e-6\n")));

    // The grid shows the new keys in their topics (alphabetical under Other for unknown ones).
    const QStringList layout = w.testSettingTopicLayout();
    QVERIFY2(layout.contains(QStringLiteral("Script control and output files: preview_only, no_gui")),
             qPrintable(layout.join(QStringLiteral(" / "))));
    QVERIFY(layout.contains(QStringLiteral("Frequencies: fstart, fstep, fpoint, fdump")));
    QVERIFY(layout.contains(QStringLiteral("Adaptive mesh refinement: amr_tol")));
    QVERIFY(layout.contains(QStringLiteral("Other: my_custom, zz_custom")));

    // Only settings{}: inserted right after it.
    const QString empty = QStringLiteral("import os\nsettings = {}\nx = create_palace(p, settings)\n");
    w.testSetEditorText(empty);
    w.testRebuildSettingsGrid(empty);
    QVERIFY(w.testAddSetting(QStringLiteral("order"), QStringLiteral("2")));
    QVERIFY2(w.testEditorText().startsWith(QStringLiteral("import os\nsettings = {}\nsettings['order'] = 2\n")),
             qPrintable(w.testEditorText()));
}

/*!*******************************************************************************************************************
 * \brief An added setting survives Save and can be edited in the grid; Reset writes the default;
 *        Remove deletes the line, refusing required keys and keys inside blocks.
 **********************************************************************************************************************/
void MainWindowPortsTest::addSetting_saveEditResetRemove()
{
    KeywordFileBackup palace(QStringLiteral("palace.csv"));
    QVERIFY(palace.write(kGridKeywords));
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    w.testRefreshKeywordTipsForCurrentTool();
    QVERIFY(w.testInitDefaultPalaceModel());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savePath = dir.filePath(QStringLiteral("added.py"));
    w.testSetRunPythonScriptLinePath(savePath);
    w.testTriggerSave();
    auto saved = [&]() {
        QFile f(savePath);
        return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    };

    QVERIFY(w.testAddSetting(QStringLiteral("order"), QStringLiteral("1")));
    w.testTriggerSave();
    QVERIFY2(saved().contains(QStringLiteral("settings['order'] = 1")), qPrintable(saved()));
    QVERIFY(w.testSetGridSettingValue(QStringLiteral("order"), 3.0));
    w.testTriggerSave();
    QVERIFY(saved().contains(QRegularExpression(QStringLiteral(R"(settings\['order'\]\s*=\s*3\b)"))));

    w.testResetSetting(QStringLiteral("order"));
    w.testTriggerSave();
    QVERIFY(saved().contains(QRegularExpression(QStringLiteral(R"(settings\['order'\]\s*=\s*2\b)"))));

    // Context menu: required keys can't be removed, optional ones can.
    QVERIFY(w.testSettingsContextActions(QStringLiteral("order")).contains(QStringLiteral("Remove from model|on")));
    QVERIFY(w.testSettingsContextActions(QStringLiteral("order")).contains(QStringLiteral("Reset to default (2)|on")));
    QVERIFY(w.testSettingsContextActions(QStringLiteral("unit")).contains(QStringLiteral("Remove from model|off")));
    w.testRemoveSetting(QStringLiteral("order"));
    w.testTriggerSave();
    QVERIFY(!saved().contains(QStringLiteral("settings['order']")));
    QVERIFY(!w.testSettingTopicLayout().join(QStringLiteral(" ")).contains(QStringLiteral("order")));
    w.testRemoveSetting(QStringLiteral("unit"));  // refused
    QVERIFY(saved().contains(QStringLiteral("settings['unit']")));

    // A key also set inside a block isn't removable.
    w.testSetEditorText(w.testEditorText() + QStringLiteral("if fine:\n    settings['fpoint'] = [1e9]\n"));
    w.testTriggerSave();
    QVERIFY(w.testSettingsContextActions(QStringLiteral("fpoint")).contains(QStringLiteral("Remove from model|off")));
}

/*!*******************************************************************************************************************
 * \brief Topic context menu, and Add disabled (with the reason) for models without a settings dict.
 **********************************************************************************************************************/
void MainWindowPortsTest::addSetting_menuAndOldModels()
{
    KeywordFileBackup palace(QStringLiteral("palace.csv"));
    QVERIFY(palace.write(kGridKeywords));
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    w.testRefreshKeywordTipsForCurrentTool();
    const QString script = QStringLiteral("settings = {}\nsettings['fstart'] = 1e9\n");
    w.testSetEditorText(script);
    w.testRebuildSettingsGrid(script);

    auto *btn = w.findChild<QPushButton *>(QStringLiteral("btnAddSetting"));
    QVERIFY(btn);
    QVERIFY(btn->isEnabled());
    const QStringList topicActions = w.testSettingsContextActions(QStringLiteral("Frequencies"));
    QVERIFY2(topicActions.contains(QStringLiteral("Add setting to Frequencies...|on")),
             qPrintable(topicActions.join(QStringLiteral(" / "))));
    QVERIFY(topicActions.contains(QStringLiteral("Collapse all topics|on")));

    // Old openEMS style: loose variables passed to the workflow functions.
    const QString loose = QStringLiteral("refined_cellsize = 1\nmargin = 50\n"
                                         "FDTD = simulation_setup.setupSimulation(a, b, F, m, d, me, p, mx, refined_cellsize, margin, unit)\n");
    w.testSetEditorText(loose);
    w.testRebuildSettingsGrid(loose);
    QVERIFY(!btn->isEnabled());
    QVERIFY(btn->toolTip().contains(QStringLiteral("plain variables")));
    QVERIFY(!w.testAddSetting(QStringLiteral("order"), QStringLiteral("2")));
    QCOMPARE(w.testEditorText(), loose);
}

/*!*******************************************************************************************************************
 * \brief Grid tooltips: the model's # @brief text, else the keyword description, always with
 *        "Required." and "Default: ..." from the keyword file.
 **********************************************************************************************************************/
void MainWindowPortsTest::settingsGrid_tooltipsCombineModelAndKeywordFile()
{
    KeywordFileBackup palace(QStringLiteral("palace.csv"));
    QVERIFY(palace.write(kGridKeywords));
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    w.testRefreshKeywordTipsForCurrentTool();
    const QString script = QStringLiteral(
        "settings = {}\n"
        "settings['fstart'] = 1e9  # @brief my start text\n"
        "settings['order'] = 2  # plain comment, not a tooltip\n"
        "settings['unit'] = 1e-6\n");
    w.testSetEditorText(script);
    w.testRebuildSettingsGrid(script);

    QCOMPARE(w.testSettingToolTip(QStringLiteral("fstart")), QStringLiteral("Required. my start text"));
    QCOMPARE(w.testSettingToolTip(QStringLiteral("order")), QStringLiteral("FEM order\nDefault: 2"));
    QCOMPARE(w.testSettingToolTip(QStringLiteral("unit")), QStringLiteral("Required. Unit\nDefault: 1e-6"));
}

/*!*******************************************************************************************************************
 * \brief Save leaves unchanged numbers and booleans as written (1e9 stays 1e9); an edited value
 *        changes only its own line.
 **********************************************************************************************************************/
void MainWindowPortsTest::saveAction_keepsUnchangedNumberSpelling()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QVERIFY(w.testInitDefaultPalaceModel());
    w.testSetEditorText(w.testEditorText()
                        + QStringLiteral("\nsettings['my_freq'] = 1e9  # keep me\n"
                                         "settings['my_unit'] = 1e-6\n"
                                         "settings['my_half'] = .5\n"
                                         "settings['my_flag'] = True\n"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savePath = dir.filePath(QStringLiteral("numbers.py"));
    w.testSetRunPythonScriptLinePath(savePath);
    auto saved = [&]() {
        QFile f(savePath);
        return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    };
    w.testTriggerSave();
    const QString first = saved();
    QVERIFY2(first.contains(QStringLiteral("settings['my_freq'] = 1e9  # keep me\n")), qPrintable(first));
    QVERIFY(first.contains(QStringLiteral("settings['my_unit'] = 1e-6\n")));
    QVERIFY(first.contains(QStringLiteral("settings['my_half'] = .5\n")));
    QVERIFY(first.contains(QStringLiteral("settings['my_flag'] = True\n")));
    w.testTriggerSave();
    QCOMPARE(saved(), first);

    // Editing one value rewrites only that line.
    QVERIFY(w.testSetGridSettingValue(QStringLiteral("my_freq"), 2e9));
    w.testTriggerSave();
    const QString second = saved();
    QVERIFY(!second.contains(QStringLiteral("settings['my_freq'] = 1e9")));
    QVERIFY(second.contains(QStringLiteral("settings['my_unit'] = 1e-6\n")));
    QStringList a = first.split(QLatin1Char('\n'));
    QStringList b = second.split(QLatin1Char('\n'));
    QCOMPARE(a.size(), b.size());
    int changed = 0;
    for (int i = 0; i < a.size(); ++i)
        changed += (a.at(i) != b.at(i)) ? 1 : 0;
    QCOMPARE(changed, 1);
}

/*!*******************************************************************************************************************
 * \brief A sweep model (settings and ports inside loops, relative paths, comments; like
 *        openEMS L6n2_sweep) survives Save: a plain Save changes nothing, a grid edit changes one
 *        line, a changed port is rewritten with the block's indentation and its comments.
 **********************************************************************************************************************/
void MainWindowPortsTest::saveAction_keepsIndentedSweepModel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QFile::copy(QFINDTESTDATA("golden/line_simple_viaport.gds"), dir.filePath(QStringLiteral("line.gds"))));
    QVERIFY(QFile::copy(QFINDTESTDATA("golden/SG13G2_200um.xml"), dir.filePath(QStringLiteral("stack.xml"))));
    const QString model = dir.filePath(QStringLiteral("sweep.py"));
    const QByteArray original =
        "import os\n"
        "from gds2openEMS import *\n"
        "from openEMS import openEMS\n"
        "import numpy as np\n"
        "\n"
        "settings = {}\n"
        "settings['preview_only'] = False  # preview model/mesh only?\n"
        "\n"
        "gds_filename = \"line.gds\"   # geometries\n"
        "XML_filename = \"stack.xml\"          # stackup\n"
        "\n"
        "settings['merge_polygon_size'] = 2\n"
        "\n"
        "for cellsize in [0.5, 1, 2]:\n"
        "    for energylimit in [-40, -50]:\n"
        "        settings['refined_cellsize'] = cellsize # mesh cell size in conductor region\n"
        "        settings['unit']   = 1e-6  # geometry is in microns\n"
        "        settings['margin'] = 250    # distance in microns\n"
        "        settings['fstart']  = 0e9\n"
        "        settings['fstop']   = 14e9\n"
        "        settings['numfreq'] = 401\n"
        "        # choices for boundary: 'PEC', 'PMC', 'MUR', 'PML_8'\n"
        "        settings['Boundaries'] = ['PEC', 'PEC', 'PEC', 'PEC', 'PEC', 'PEC']  # all metal\n"
        "        settings['energy_limit'] = energylimit          # end criteria\n"
        "\n"
        "        simulation_ports = simulation_setup.all_simulation_ports()\n"
        "\n"
        "        # via port is specified with from_layername= and to_layername= and direction z\n"
        "        simulation_ports.add_port(simulation_setup.simulation_port(portnumber=1, \n"
        "                                                                voltage=1, \n"
        "                                                                port_Z0=50, \n"
        "                                                                source_layernum=201, \n"
        "                                                                from_layername='Metal1', \n"
        "                                                                to_layername='TopMetal2', \n"
        "                                                                direction='z'))\n"
        "\n"
        "        simulation_ports.add_port(simulation_setup.simulation_port(portnumber=2, \n"
        "                                                                voltage=1, \n"
        "                                                                port_Z0=50, \n"
        "                                                                source_layernum=202, \n"
        "                                                                from_layername='Metal1', \n"
        "                                                                to_layername='TopMetal2', \n"
        "                                                                direction='z'))\n"
        "\n"
        "        # ======================== simulation ================================\n"
        "\n"
        "        # get technology stackup data\n"
        "        materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate (XML_filename)\n"
        "        allpolygons = gds_reader.read_gds(gds_filename, [], purposelist=[0], metals_list=metals_list,\n"
        "                                          merge_polygon_size=settings['merge_polygon_size'])\n"
        "        FDTD = openEMS(EndCriteria=np.exp(settings['energy_limit']/10 * np.log(10)))\n"
        "        FDTD.SetBoundaryCond( settings['Boundaries'] )\n";
    {
        QFile f(model);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(original);
    }
    auto saved = [&]() {
        QFile f(model);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    auto compiles = [&](QString *err) {
        const QString py = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (py.isEmpty())
            return true;
        QProcess p;
        p.start(py, {QStringLiteral("-m"), QStringLiteral("py_compile"), model});
        p.waitForFinished(20000);
        *err = QString::fromUtf8(p.readAllStandardError());
        return p.exitCode() == 0;
    };

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.loadPythonModel(model);
    QCOMPARE(w.testCurrentSimToolKey(), QStringLiteral("openems"));

    // Plain Save: the file is unchanged.
    w.testTriggerSave();
    QCOMPARE(QString::fromUtf8(saved()), QString::fromUtf8(original));

    // A grid edit changes only its own line (indentation and comment kept).
    QVERIFY(w.testSetGridSettingValue(QStringLiteral("margin"), 300.0));
    w.testTriggerSave();
    const QString edited = QString::fromUtf8(saved());
    QVERIFY2(edited.contains(QStringLiteral("        settings['margin'] = 300    # distance in microns\n")),
             qPrintable(edited));
    QCOMPARE(QString(edited).replace(QStringLiteral("settings['margin'] = 300"), QStringLiteral("settings['margin'] = 250")),
             QString::fromUtf8(original));

    // A changed port: the block is rewritten, indented like before, with its comments kept.
    auto *ports = w.findChild<QTableWidget *>(QStringLiteral("tblPorts"));
    QVERIFY(ports);
    QCOMPARE(ports->rowCount(), 2);
    ports->item(1, 2)->setText(QStringLiteral("75"));
    w.testTriggerSave();
    const QString rewritten = QString::fromUtf8(saved());
    QVERIFY2(rewritten.contains(QStringLiteral("\n        simulation_ports = simulation_setup.all_simulation_ports()\n"
                                               "        # via port is specified with from_layername= and to_layername= and direction z\n"
                                               "        simulation_ports.add_port(")), qPrintable(rewritten));
    QVERIFY(rewritten.contains(QStringLiteral("port_Z0=75")));
    QVERIFY(rewritten.contains(QStringLiteral("\n        # ======================== simulation ================================\n")));
    QVERIFY(rewritten.contains(QStringLiteral("\n        # get technology stackup data\n")));
    QVERIFY(rewritten.contains(QStringLiteral("gds_filename = \"line.gds\"   # geometries\n")));
    QVERIFY(rewritten.contains(QStringLiteral(
        "        settings['Boundaries'] = ['PEC', 'PEC', 'PEC', 'PEC', 'PEC', 'PEC']  # all metal\n")));
    QString err;
    QVERIFY2(compiles(&err), qPrintable(err + QLatin1Char('\n') + rewritten));
}

namespace {
/*! Copies the converter test fixture (loose openEMS model + stand-in modules) into \a dir. */
QString copyLooseFixture(const QTemporaryDir &dir, const QByteArray &append = QByteArray())
{
    const QString fixture = QFINDTESTDATA("python/fixtures/convert_loose");
    if (fixture.isEmpty())
        return QString();
    QDir().mkpath(dir.filePath(QStringLiteral("modules")));
    const QDir mods(fixture + QStringLiteral("/modules"));
    for (const QString &f : mods.entryList({QStringLiteral("*.py")}, QDir::Files))
        QFile::copy(mods.filePath(f), dir.filePath(QStringLiteral("modules/") + f));
    QFile in(fixture + QStringLiteral("/model_loose.py"));
    if (!in.open(QIODevice::ReadOnly))
        return QString();
    const QString model = dir.filePath(QStringLiteral("model_loose.py"));
    QFile out(model);
    if (!out.open(QIODevice::WriteOnly))
        return QString();
    out.write(in.readAll() + append);
    return model;
}

QString testPython()
{
    const QString py = QString::fromLocal8Bit(qgetenv("EMSTUDIO_TEST_PYTHON"));
    return py.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("python3")) : py;
}

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

/*! File → Convert to settings dictionary: verified backup, converted model on disk, reloaded, and
 *  Add setting works afterwards. */
void MainWindowPortsTest::convertLooseModel_backsUpConvertsAndReloads()
{
    if (testPython().isEmpty())
        QSKIP("no Python for the converter");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString model = copyLooseFixture(dir);
    QVERIFY(!model.isEmpty());
    const QByteArray original = fileBytes(model);

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.testSetPreference(QStringLiteral("Python Path"), testPython());
    w.loadPythonModel(model);
    QString why;
    QVERIFY2(w.testCanConvertLooseModel(&why), qPrintable(why));

    w.testSetConvertAnswer(true, false);
    QSignalSpy spy(&w, &MainWindow::looseConversionFinished);
    w.testStartLooseConversion();
    // Polls with processEvents: an earlier suite's runHeadless() called QCoreApplication::exit,
    // after which QSignalSpy::wait()'s nested event loop returns at once.
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 30000);
    const QList<QVariant> args = spy.takeFirst();
    QVERIFY2(args.at(0).toBool(), qPrintable(args.at(1).toString()));

    const QString backup = args.at(1).toString();
    QVERIFY(QFileInfo(backup).fileName().startsWith(QStringLiteral("model_loose_backup_")));
    QCOMPARE(fileBytes(backup), original);

    const QString converted = QString::fromUtf8(fileBytes(model));
    QVERIFY(converted.contains(QStringLiteral("simulation_setup.setupSimulation(FDTD=FDTD, settings=settings)")));
    QVERIFY(converted.contains(QStringLiteral("settings['margin'] = 50")));
    QCOMPARE(w.testEditorText(), converted);
    QCOMPARE(w.testLastConversionReport().value(QStringLiteral("ok")).toBool(), true);

    // Now a settings[] model: no second conversion, and Add setting writes into the dict.
    QVERIFY(!w.testCanConvertLooseModel(&why));
    QVERIFY(why.contains(QStringLiteral("settings dictionary")));
    QVERIFY(w.testAddSetting(QStringLiteral("numThreads"), QStringLiteral("4")));
    QVERIFY(w.testEditorText().contains(QStringLiteral("settings['numThreads'] = 4")));
}

/*! A model the converter refuses stays byte-identical, without a backup. */
void MainWindowPortsTest::convertLooseModel_refusalLeavesModelUntouched()
{
    if (testPython().isEmpty())
        QSKIP("no Python for the converter");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString model = copyLooseFixture(dir, "print(sorted(globals()))\n");
    QVERIFY(!model.isEmpty());
    const QByteArray original = fileBytes(model);

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.testSetPreference(QStringLiteral("Python Path"), testPython());
    w.loadPythonModel(model);
    w.testSetConvertAnswer(true, false);
    QSignalSpy spy(&w, &MainWindow::looseConversionFinished);
    w.testStartLooseConversion();
    // Polls with processEvents: an earlier suite's runHeadless() called QCoreApplication::exit,
    // after which QSignalSpy::wait()'s nested event loop returns at once.
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 30000);
    const QList<QVariant> args = spy.takeFirst();
    QVERIFY(!args.at(0).toBool());
    QVERIFY2(args.at(1).toString().contains(QStringLiteral("globals")), qPrintable(args.at(1).toString()));
    QCOMPARE(fileBytes(model), original);
    QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("*_backup_*")}, QDir::Files).size(), 0);
}

/*! Fields page messages ("no field dump") go to the Log window, not into the simulation log that is
 *  saved with the run. */
void MainWindowPortsTest::fieldsPage_noDumpMessageGoesToLog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString model = copyLooseFixture(dir);
    QVERIFY(!model.isEmpty());

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.loadPythonModel(model);
    QShortcut *fields = nullptr;
    for (QShortcut *sc : w.findChildren<QShortcut *>())
        if (sc->key() == QKeySequence(QStringLiteral("Ctrl+7")))
            fields = sc;
    QVERIFY(fields);
    emit fields->activated();
    QTRY_VERIFY_WITH_TIMEOUT(w.testMainLogText().contains(QStringLiteral("No field dump found")), 5000);
    QVERIFY(!w.testSimulationLogText().contains(QStringLiteral("[Field]")));
}
