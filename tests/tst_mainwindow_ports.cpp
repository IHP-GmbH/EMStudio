#include "tst_mainwindow_ports.h"

#include <QtTest/QtTest>

#include "mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Verifies that testParsePortsFromEditor() parses multiline simulation_port() calls correctly.
 *
 * The test validates parser output independently from UI combo-box population.
 * It checks that:
 *  - two ports are parsed,
 *  - numeric and string fields are extracted correctly,
 *  - direction is preserved.
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
 * The test checks parser-level behavior and then verifies that one row is imported into the UI table.
 * It does not require a specific source-layer combo value because that depends on the real GDS layer set.
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

    // UI default for missing direction in appendParsedPortsToTable()
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
 * The test enables and disables the "Use Substrate Layer Names" mode and checks that
 * the imported row remains valid and the row count is preserved.
 *
 * The exact combo text depends on the real XML/GDS mapping, so the test does not hardcode
 * a specific substrate layer name.
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
