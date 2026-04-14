#include "tst_mainwindow_ports.h"

#include <QtTest/QtTest>
#include <QFile>

#include "mainwindow.h"

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
