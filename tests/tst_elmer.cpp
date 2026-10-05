/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_elmer.h"

#include <QtTest/QtTest>
#include <QTabWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTextEdit>

#include "mainwindow.h"
#include "layoutview.h"
#include "substrate.h"
#include "pythonparser.h"

/*!*******************************************************************************************************************
 * \brief Resolves the ElmerSolver stub used to enable Elmer tools in the combo box.
 **********************************************************************************************************************/
static QString ensureTestElmerSolverStub()
{
#ifdef Q_OS_WIN
    return QFINDTESTDATA("tools/elmer_solver_stub.cmd");
#else
    return QFINDTESTDATA("tools/elmer_solver_stub.sh");
#endif
}

/*!*******************************************************************************************************************
 * \brief Absolute path to the repository scripts/ folder (templates).
 **********************************************************************************************************************/
static QString repoScriptsDir()
{
    const QString stub = ensureTestElmerSolverStub();
    if (stub.isEmpty())
        return {};
    // tests/tools -> ../../scripts
    return QFileInfo(stub).absoluteDir().absoluteFilePath(QStringLiteral("../../scripts"));
}

void ElmerTest::normalizeSimToolKey_mapsLegacyElmerToEm()
{
    MainWindow w;
    QCOMPARE(w.testNormalizeSimToolKey(QStringLiteral("elmer")), QStringLiteral("elmer_em"));
    QCOMPARE(w.testNormalizeSimToolKey(QStringLiteral("Elmer")), QStringLiteral("elmer_em"));
    QCOMPARE(w.testNormalizeSimToolKey(QStringLiteral("elmer_em")), QStringLiteral("elmer_em"));
    QCOMPARE(w.testNormalizeSimToolKey(QStringLiteral("elmer_thermal")), QStringLiteral("elmer_thermal"));
    QCOMPARE(w.testNormalizeSimToolKey(QStringLiteral("palace")), QStringLiteral("palace"));
}

void ElmerTest::isElmerKeyHelpers_classifyFamily()
{
    MainWindow w;
    QVERIFY(w.testIsElmerFamilyKey(QStringLiteral("elmer")));
    QVERIFY(w.testIsElmerFamilyKey(QStringLiteral("elmer_em")));
    QVERIFY(w.testIsElmerFamilyKey(QStringLiteral("elmer_thermal")));
    QVERIFY(!w.testIsElmerFamilyKey(QStringLiteral("palace")));

    QVERIFY(w.testIsElmerEmKey(QStringLiteral("elmer")));
    QVERIFY(w.testIsElmerEmKey(QStringLiteral("elmer_em")));
    QVERIFY(!w.testIsElmerEmKey(QStringLiteral("elmer_thermal")));

    QVERIFY(w.testIsElmerThermalKey(QStringLiteral("elmer_thermal")));
    QVERIFY(!w.testIsElmerThermalKey(QStringLiteral("elmer_em")));
}

void ElmerTest::detectPythonModelSimKey_elmerThermalMarkers()
{
    MainWindow w;

    const QString byCreate =
        QStringLiteral("config_name, data_dir = simulation_setup.create_elmer_thermal(settings)\n");
    QCOMPARE(w.testDetectPythonModelSimKey(byCreate), QStringLiteral("elmer_thermal"));

    const QString byObjects =
        QStringLiteral("thermal_objects = simulation_setup.all_thermal_objects()\n");
    QCOMPARE(w.testDetectPythonModelSimKey(byObjects), QStringLiteral("elmer_thermal"));

    const QString byFlag =
        QStringLiteral("settings['elmer_thermal'] = True\n");
    QCOMPARE(w.testDetectPythonModelSimKey(byFlag), QStringLiteral("elmer_thermal"));
}

void ElmerTest::detectPythonModelSimKey_elmerEmMarkers()
{
    MainWindow w;

    const QString byCreate =
        QStringLiteral("config_name, data_dir = simulation_setup.create_elmer(settings)\n");
    QCOMPARE(w.testDetectPythonModelSimKey(byCreate), QStringLiteral("elmer_em"));

    const QString byFlag =
        QStringLiteral("settings['elmer'] = True\n");
    QCOMPARE(w.testDetectPythonModelSimKey(byFlag), QStringLiteral("elmer_em"));
}

void ElmerTest::refreshSimToolOptions_enablesElmerWhenSolverStubConfigured()
{
    MainWindow w;

    const QString stub = ensureTestElmerSolverStub();
    QVERIFY2(!stub.isEmpty(), "Elmer solver stub not found via QFINDTESTDATA");

#ifndef Q_OS_WIN
    QFile::setPermissions(stub,
                          QFile::permissions(stub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference(QStringLiteral("ELMER_SOLVER_PATH"), stub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_em"), &err), qPrintable(err));
    QCOMPARE(w.testCurrentSimToolKey(), QStringLiteral("elmer_em"));

    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_thermal"), &err), qPrintable(err));
    QCOMPARE(w.testCurrentSimToolKey(), QStringLiteral("elmer_thermal"));
}

void ElmerTest::refreshSimToolOptions_listsElmerThermalEvenWithoutSolverPath()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);

    // No usable ElmerSolver — UI modes must still be selectable for import/detection.
    w.testSetPreference(QStringLiteral("ELMER_SOLVER_PATH"), QString());
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_thermal"), &err), qPrintable(err));
    QCOMPARE(w.testCurrentSimToolKey(), QStringLiteral("elmer_thermal"));

    const QString snippet =
        QStringLiteral(
            "settings['elmer_thermal'] = True\n"
            "thermal_objects = simulation_setup.all_thermal_objects()\n"
            "thermal_objects.add_heatsource(simulation_setup.heatsource("
            "power=0.1, source_layernum=201, target_layername='TFR'))\n");
    w.testEnsureThermalTableFromScript(snippet);
    QCOMPARE(w.testThermalRowCount(), 1);
}

void ElmerTest::defaultElmerThermalTemplate_containsThermalWorkflow()
{
    MainWindow w;

    const QString scripts = repoScriptsDir();
    QVERIFY2(QDir(scripts).exists(), qPrintable(QStringLiteral("scripts dir missing: %1").arg(scripts)));
    w.testSetPreference(QStringLiteral("MODEL_TEMPLATES_DIR"), scripts);

    const QString stub = ensureTestElmerSolverStub();
    QVERIFY2(!stub.isEmpty(), "Elmer solver stub not found");
#ifndef Q_OS_WIN
    QFile::setPermissions(stub,
                          QFile::permissions(stub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif
    w.testSetPreference(QStringLiteral("ELMER_SOLVER_PATH"), stub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_thermal"), &err), qPrintable(err));
    QVERIFY2(w.testInitDefaultElmerThermalModel(), "createDefaultElmerThermalScript failed");

    const QString script = w.testEditorText();
    QVERIFY(script.contains(QStringLiteral("elmer_thermal")));
    QVERIFY(script.contains(QStringLiteral("create_elmer_thermal"))
            || script.contains(QStringLiteral("all_thermal_objects")));
    QCOMPARE(w.testDetectPythonModelSimKey(script), QStringLiteral("elmer_thermal"));
}

void ElmerTest::thermalTable_roundTripFromScript()
{
    MainWindow w;

    const QString stub = ensureTestElmerSolverStub();
    QVERIFY2(!stub.isEmpty(), "Elmer solver stub not found");
#ifndef Q_OS_WIN
    QFile::setPermissions(stub,
                          QFile::permissions(stub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif
    w.testSetPreference(QStringLiteral("ELMER_SOLVER_PATH"), stub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_thermal"), &err), qPrintable(err));

    const QString snippet =
        QStringLiteral(
            "thermal_objects = simulation_setup.all_thermal_objects()\n"
            "thermal_objects.add_heatsource(simulation_setup.heatsource("
            "power=0.65, source_layernum=201, target_layername='TFR'))\n"
            "thermal_objects.add_consttemp(simulation_setup.constanttemp("
            "temp=298, source_layernum=202, target_layername='BACKSIDEGND'))\n");

    w.testEnsureThermalTableFromScript(snippet);
    QCOMPARE(w.testThermalRowCount(), 2);

    const QString rebuilt = w.testBuildThermalCodeFromGui();
    QVERIFY(rebuilt.contains(QStringLiteral("add_heatsource")));
    QVERIFY(rebuilt.contains(QStringLiteral("power=0.65")));
    QVERIFY(rebuilt.contains(QStringLiteral("source_layernum=201")));
    QVERIFY(rebuilt.contains(QStringLiteral("TFR")));
    QVERIFY(rebuilt.contains(QStringLiteral("add_consttemp")));
    QVERIFY(rebuilt.contains(QStringLiteral("temp=298")));
    QVERIFY(rebuilt.contains(QStringLiteral("BACKSIDEGND")));
}

void ElmerTest::findThermalResultsVtu_prefersThermalResultsPrefix()
{
    MainWindow w;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString other = dir.filePath(QStringLiteral("mesh_case.vtu"));
    const QString preferred = dir.filePath(QStringLiteral("thermal_results_t0001.vtu"));
    {
        QFile f(other);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }
    {
        QFile f(preferred);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("y");
        f.close();
    }

    const QString found = w.testFindThermalResultsVtu(dir.path());
    QCOMPARE(QFileInfo(found).fileName(), QStringLiteral("thermal_results_t0001.vtu"));
}

void ElmerTest::findThermalResultsVtu_prefersPvtuOverVtu()
{
    MainWindow w;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString piece = dir.filePath(QStringLiteral("thermal_results_t0001.vtu"));
    const QString combined = dir.filePath(QStringLiteral("thermal_results.pvtu"));
    {
        QFile f(piece);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("piece");
        f.close();
    }
    {
        QFile f(combined);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("combined");
        f.close();
    }

    const QString found = w.testFindThermalResultsVtu(dir.path());
    QCOMPARE(QFileInfo(found).suffix().toLower(), QStringLiteral("pvtu"));
    QCOMPARE(QFileInfo(found).fileName(), QStringLiteral("thermal_results.pvtu"));
}

void ElmerTest::substrateOffset_expressionResolvesWithVariables()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString xmlPath = dir.filePath(QStringLiteral("thermal_offset.xml"));
    {
        QFile f(xmlPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "<?xml version='1.0' encoding='UTF-8'?>\n"
            << "<Stackup schemaVersion=\"3.1\">\n"
            << "  <Variables>\n"
            << "    <Variable Name=\"total_thickness\" Value=\"250.0\" />\n"
            << "    <Variable Name=\"substrate_thickness\" Value=\"=total_thickness-12.4704\" />\n"
            << "  </Variables>\n"
            << "  <Materials>\n"
            << "    <Material Name=\"Si\" Type=\"Semiconductor\" Color=\"01e0ff\" />\n"
            << "    <Material Name=\"LOWLOSS\" Type=\"Conductor\" Color=\"ff0000\" />\n"
            << "  </Materials>\n"
            << "  <ELayers LengthUnit=\"um\">\n"
            << "    <Dielectrics>\n"
            << "      <Dielectric Name=\"Passive\" Material=\"Si\" Thickness=\"0.4\" />\n"
            << "      <Dielectric Name=\"SiO2\" Material=\"Si\" Thickness=\"12.2704\" />\n"
            << "      <Dielectric Name=\"Substrate\" Material=\"Si\" Thickness=\"=substrate_thickness\" />\n"
            << "    </Dielectrics>\n"
            << "    <Layers>\n"
            << "      <Substrate Offset=\"=substrate_thickness\" />\n"
            << "      <Layer Name=\"M1\" Type=\"conductor\" Zmin=\"0.6\" Zmax=\"1.0\" "
               "Material=\"LOWLOSS\" Layer=\"1\" />\n"
            << "      <Layer Name=\"BACKSIDEGND\" Type=\"conductor\" "
               "Zmin=\"=-substrate_thickness\" Zmax=\"=-substrate_thickness - 1\" "
               "Material=\"LOWLOSS\" Layer=\"251\" />\n"
            << "    </Layers>\n"
            << "  </ELayers>\n"
            << "</Stackup>\n";
    }

    Substrate sub;
    QVERIFY2(sub.parseXmlFile(xmlPath), "Failed to parse thermal_offset.xml");

    const double expected = 250.0 - 12.4704;
    QCOMPARE(sub.substrateOffset(), expected);
    QCOMPARE(sub.substrateOffsetRaw(), QStringLiteral("=substrate_thickness"));

    // With Offset applied, Substrate dielectric sits at negative Z.
    bool foundSub = false;
    for (const Dielectric &d : sub.dielectrics()) {
        if (d.name() != QLatin1String("Substrate"))
            continue;
        foundSub = true;
        QCOMPARE(d.resolvedZmin(), -expected);
        QCOMPARE(d.resolvedZmax(), 0.0);
    }
    QVERIFY(foundSub);

    bool foundBg = false;
    for (const Layer &lay : sub.layers()) {
        if (lay.name() != QLatin1String("BACKSIDEGND"))
            continue;
        foundBg = true;
        QCOMPARE(lay.zmin(), -expected);
        QCOMPARE(lay.zmax(), -expected - 1.0);
    }
    QVERIFY(foundBg);
}

void ElmerTest::thermalRows_addRemoveAndWorkflowHelpers()
{
    MainWindow w;

    const QString stub = ensureTestElmerSolverStub();
    QVERIFY2(!stub.isEmpty(), "Elmer solver stub not found");
#ifndef Q_OS_WIN
    QFile::setPermissions(stub,
                          QFile::permissions(stub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif
    w.testSetPreference(QStringLiteral("ELMER_SOLVER_PATH"), stub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_thermal"), &err), qPrintable(err));

    w.testRemoveAllThermalObjects();
    QCOMPARE(w.testThermalRowCount(), 0);

    w.testEnsureThermalTableFromScript(
        QStringLiteral(
            "thermal_objects = simulation_setup.all_thermal_objects()\n"
            "thermal_objects.add_heatsource(simulation_setup.heatsource("
            "power=0.2, source_layernum=201, target_layername='M1'))\n"));
    QCOMPARE(w.testThermalRowCount(), 1);

    w.testClickAddThermalObject();
    QCOMPARE(w.testThermalRowCount(), 2);

    w.testSetThermalCurrentRow(1);
    w.testClickRemoveSelectedThermalObject();
    QCOMPARE(w.testThermalRowCount(), 1);

    const QString rebuilt = w.testBuildThermalCodeFromGui();
    QVERIFY(rebuilt.contains(QStringLiteral("add_heatsource")));
    QVERIFY(rebuilt.contains(QStringLiteral("M1")));

    QString palaceish =
        QStringLiteral("settings['palace'] = True\n"
                       "config_name, data_dir = simulation_setup.create_palace(settings)\n"
                       "path = utilities.create_sim_path(script_path, model_basename)\n");
    const QString thermalized = w.testApplyElmerThermalWorkflow(palaceish);
    QVERIFY(thermalized.contains(QStringLiteral("elmer_thermal")));
    QVERIFY(thermalized.contains(QStringLiteral("create_elmer_thermal")));
    QVERIFY(thermalized.contains(QStringLiteral("dirname='elmer_model'")));

    const QString withSection = w.testReplaceOrInsertThermalSection(
        QStringLiteral("# ==== run simulation ====\nprint('x')\n"),
        QStringLiteral("thermal_objects = simulation_setup.all_thermal_objects()\n"));
    QVERIFY(withSection.contains(QStringLiteral("all_thermal_objects")));
    QVERIFY(withSection.contains(QStringLiteral("run simulation")));

    const QString replaced = w.testReplaceOrInsertThermalSection(
        QStringLiteral("thermal_objects = simulation_setup.all_thermal_objects()\n"
                       "thermal_objects.add_heatsource(simulation_setup.heatsource("
                       "power=1, source_layernum=1, target_layername='A'))\n"
                       "print('done')\n"),
        QStringLiteral("thermal_objects = simulation_setup.all_thermal_objects()\n"
                       "thermal_objects.add_consttemp(simulation_setup.constanttemp("
                       "temp=300, source_layernum=2, target_layername='B'))\n"));
    QVERIFY(replaced.contains(QStringLiteral("add_consttemp")));
    QVERIFY(!replaced.contains(QStringLiteral("add_heatsource")));
}

void ElmerTest::openThermalResults_switchesToFieldView()
{
    MainWindow w;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString vtu = dir.filePath(QStringLiteral("thermal_results.vtu"));
    {
        QFile f(vtu);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<VTKFile/>");
        f.close();
    }

    // No dump tooling needed: should open the Fields page (Field mode on) without hanging.
    w.testOpenThermalResultsInFieldView(dir.path());
    QVERIFY(w.testIsFieldMode());
    auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabSettings"));
    QVERIFY(tabs);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Fields"));
}

void ElmerTest::fieldChoices_parseListingAndViewerArgs()
{
    MainWindow w;

    // field_io.py --list output: volume + boundary dump, each 6 GHz + a
    // geometry-only cycle, plus an AMR iteration copy that must be skipped.
    const QByteArray palace = R"json({"source":"palace","files":[
        {"path":"/r/driven/driven.pvd","label":"V driven.pvd","amr":false,
         "cycles":[{"label":"6 GHz (cycle 1)","geometry":false},{"label":"geometry (cycle 2)","geometry":true}]},
        {"path":"/r/driven_boundary/driven_boundary.pvd","label":"B driven_boundary.pvd","amr":false,
         "cycles":[{"label":"6 GHz (cycle 1)","geometry":false}]},
        {"path":"/r/iteration1/driven/driven.pvd","label":"iteration1/driven.pvd","amr":true,
         "cycles":[{"label":"6 GHz (cycle 1)","geometry":false}]}]})json";
    QCOMPARE(w.testParseFieldChoices(palace),
             QStringList({QStringLiteral("V driven.pvd"), QStringLiteral("B driven_boundary.pvd")}));

    const QStringList args = w.testFieldViewerArguments(QStringLiteral("/s/field_viewer.py"),
                                                        QStringLiteral("/r"));
    QCOMPARE(args.first(), QStringLiteral("/s/field_viewer.py"));
    QVERIFY(args.contains(QStringLiteral("--stdin-control")));
    const int sel = args.indexOf(QStringLiteral("--select-file"));
    QVERIFY(sel > 0);
    QCOMPARE(args.at(sel + 1), QStringLiteral("/r/driven/driven.pvd"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--cycle")) + 1), QStringLiteral("1"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--run-path")) + 1), QStringLiteral("/r"));

    // One file with several frequencies: entries are the cycle labels.
    const QByteArray multi = R"json({"files":[{"path":"/r/a.pvd","label":"a.pvd","amr":false,
        "cycles":[{"label":"5 GHz (cycle 1)","geometry":false},{"label":"7.5 GHz (cycle 2)","geometry":null}]}]})json";
    QCOMPARE(w.testParseFieldChoices(multi),
             QStringList({QStringLiteral("5 GHz (cycle 1)"), QStringLiteral("7.5 GHz (cycle 2)")}));

    QVERIFY(w.testParseFieldChoices(QByteArrayLiteral("{\"files\":[]}")).isEmpty());
    QVERIFY(w.testParseFieldChoices(QByteArrayLiteral("not json")).isEmpty());
}

void ElmerTest::generateScript_elmerThermalFromGui()
{
    MainWindow w;

    const QString scripts = repoScriptsDir();
    QVERIFY2(QDir(scripts).exists(), qPrintable(scripts));
    w.testSetPreference(QStringLiteral("MODEL_TEMPLATES_DIR"), scripts);

    const QString stub = ensureTestElmerSolverStub();
    QVERIFY(!stub.isEmpty());
#ifndef Q_OS_WIN
    QFile::setPermissions(stub,
                          QFile::permissions(stub) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif
    w.testSetPreference(QStringLiteral("ELMER_SOLVER_PATH"), stub);
    w.refreshSimToolOptionsForTests();

    QString err;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_thermal"), &err), qPrintable(err));
    QVERIFY(w.testInitDefaultElmerThermalModel());

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY(!xmlPath.isEmpty());
    w.setSubstrateFile(xmlPath);
    w.setTopCell(QStringLiteral("TOP"));
    w.setGdsFile(QStringLiteral("dummy.gds"));

    w.testEnsureThermalTableFromScript(
        QStringLiteral(
            "thermal_objects = simulation_setup.all_thermal_objects()\n"
            "thermal_objects.add_heatsource(simulation_setup.heatsource("
            "power=0.1, source_layernum=201, target_layername='Metal1'))\n"));

    QString genErr;
    const QString script = w.testGenerateScriptFromGuiState(&genErr);
    QVERIFY2(!script.isEmpty(), qPrintable(genErr));
    QVERIFY(script.contains(QStringLiteral("elmer_thermal"))
            || script.contains(QStringLiteral("create_elmer_thermal"))
            || script.contains(QStringLiteral("all_thermal_objects")));
}

void ElmerTest::forceStartSimulationOff_clearsTrueFlags()
{
    MainWindow w;
    const QString in =
        QStringLiteral(
            "start_simulation = True  # run solver from script\n"
            "settings['start_simulation'] = True\n"
            "settings[\"other\"] = 1\n");
    const QString out = w.testForceStartSimulationOff(in);
    QVERIFY(out.contains(QStringLiteral("start_simulation = False")));
    QVERIFY(out.contains(QStringLiteral("settings['start_simulation'] = False")));
    QVERIFY(!out.contains(QStringLiteral("start_simulation = True")));
    QVERIFY(out.contains(QStringLiteral("settings[\"other\"] = 1")));
}

void ElmerTest::applyGdsAndXmlPaths_updatesCellnameAndGdsCellname()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.show();

    auto *cbx = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    QVERIFY(cbx);
    {
        QSignalBlocker b(cbx);
        cbx->clear();
        cbx->addItem(QStringLiteral("NewCell_A"));
        cbx->setCurrentIndex(0);
    }

    const QString in = QStringLiteral(
        "gds_filename = \"x.gds\"\n"
        "gds_cellname = \"OldGdsCell\"\n"
        "cellname = \"OldCell\"\n"
        "settings['cellname'] = \"OldSettingsCell\"\n"
        "XML_filename = \"x.xml\"\n");

    const QString out = w.testApplyGdsAndXmlPaths(in, QStringLiteral("palace"));
    QVERIFY(out.contains(QStringLiteral("gds_cellname = \"NewCell_A\"")));
    // optional OpenEMS-style cellname left alone when gds_cellname exists
    QVERIFY(out.contains(QStringLiteral("cellname = \"OldCell\"")));
    QVERIFY(out.contains(QStringLiteral("settings['cellname'] = \"NewCell_A\"")));
    QVERIFY(!out.contains(QStringLiteral("OldGdsCell")));
    QVERIFY(!out.contains(QStringLiteral("OldSettingsCell")));
    // comment on gds_cellname line is dropped (stable golden style)
    QVERIFY(!out.contains(QStringLiteral("gds_cellname = \"NewCell_A\" #")));
}

void ElmerTest::applyGdsAndXmlPaths_doesNotTouchReadGdsKwarg()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.show();

    auto *cbx = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    QVERIFY(cbx);
    {
        QSignalBlocker b(cbx);
        cbx->clear();
        cbx->addItem(QStringLiteral("t1"));
        cbx->setCurrentIndex(0);
    }

    const QString in = QStringLiteral(
        "gds_filename = \"x.gds\"\n"
        "gds_cellname = \"old\"\n"
        "allpolygons = gds_reader.read_gds(gds_filename,\n"
        "                                  layernumbers,\n"
        "                                  cellname=gds_cellname)\n");

    const QString out = w.testApplyGdsAndXmlPaths(in, QStringLiteral("palace"));
    QVERIFY(out.contains(QStringLiteral("gds_cellname = \"t1\"")));
    QVERIFY(out.contains(QStringLiteral("cellname=gds_cellname)")));
    QVERIFY(!out.contains(QStringLiteral("cellname=\"t1\"")));
}

void ElmerTest::applyGdsAndXmlPaths_doesNotPrependGdsCellnameWhenSettingsCellnameExists()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.show();

    auto *cbx = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    QVERIFY(cbx);
    {
        QSignalBlocker b(cbx);
        cbx->clear();
        cbx->addItem(QStringLiteral("0_INT_T595_HeatSpreader"));
        cbx->setCurrentIndex(0);
    }

    // Volker-style Elmer Thermal: cellname lives only in settings[], not as top-level gds_cellname.
    const QString in = QStringLiteral(
        "from gds2palace import *\n"
        "settings = {}\n"
        "settings['elmer_thermal'] = True\n"
        "settings['cellname'] = 'OldCell'\n"
        "settings['GdsFile'] = '/tmp/x.gds'\n"
        "thermal_objects = simulation_setup.all_thermal_objects()\n"
        "config_name, data_dir = simulation_setup.create_elmer_thermal(settings)\n");

    const QString out = w.testApplyGdsAndXmlPaths(in, QStringLiteral("elmer_thermal"));
    QVERIFY(out.contains(QStringLiteral("settings['cellname'] = \"0_INT_T595_HeatSpreader\"")));
    QVERIFY(!out.contains(QStringLiteral("gds_cellname")));
    QVERIFY(out.trimmed().startsWith(QStringLiteral("from gds2palace")));
    QVERIFY(out.contains(QStringLiteral("create_elmer_thermal")));
    QVERIFY(out.contains(QStringLiteral("all_thermal_objects")));
}

void ElmerTest::applyGdsAndXmlPaths_updatesSettingsGdsAndSubstrateFile()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.show();

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!gdsPath.isEmpty(), "golden GDS missing");
    QVERIFY2(!xmlPath.isEmpty(), "golden XML missing");

    w.setGdsFile(gdsPath);
    auto *sub = w.findChild<QLineEdit *>(QStringLiteral("txtSubstrate"));
    QVERIFY(sub);
    sub->setText(xmlPath); // triggers on_txtSubstrate_textChanged → m_simSettings[SubstrateFile]

    const QString in = QStringLiteral(
        "from gds2palace import *\n"
        "settings = {}\n"
        "settings['GdsFile'] = 'old.gds'\n"
        "settings['SubstrateFile'] = 'old.xml'\n"
        "gds_filename = \"legacy.gds\"\n"
        "XML_filename = \"legacy.xml\"\n");

    const QString out = w.testApplyGdsAndXmlPaths(in, QStringLiteral("openems"));
    const QString gdsNorm = QDir::fromNativeSeparators(gdsPath);
    const QString xmlNorm = QDir::fromNativeSeparators(xmlPath);
    QVERIFY2(out.contains(QStringLiteral("settings['GdsFile'] = \"%1\"").arg(gdsNorm)),
             qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("settings['SubstrateFile'] = \"%1\"").arg(xmlNorm)),
             qPrintable(out));
    QVERIFY(out.contains(QStringLiteral("gds_filename = \"%1\"").arg(gdsNorm)));
    QVERIFY(out.contains(QStringLiteral("XML_filename = \"%1\"").arg(xmlNorm)));
    QVERIFY(!out.contains(QStringLiteral("old.gds")));
    QVERIFY(!out.contains(QStringLiteral("legacy.gds")));
}

void ElmerTest::thermalWorkflow_keepsSingleElmerThermalFlag()
{
    MainWindow w;
    const QString createLine =
        QStringLiteral("config_name, data_dir = simulation_setup.create_elmer_thermal (settings)\n");

    // Key not on the last line, with a comment: value kept, comment kept, nothing inserted.
    const QString script =
        QStringLiteral("settings['elmer_thermal'] = False # metals as volumes\n"
                       "settings['refined_cellsize'] = 2\n") + createLine;
    const QString once = w.testApplyElmerThermalWorkflow(script);
    QCOMPARE(once.count(QStringLiteral("['elmer_thermal']")), 1);
    QVERIFY(once.contains(QStringLiteral("settings['elmer_thermal'] = True # metals as volumes\n")));
    // Saving again must not change the script.
    QCOMPARE(w.testApplyElmerThermalWorkflow(once), once);

    // Copies inserted by older versions (one per Save) are removed.
    QString broken = QStringLiteral("settings['elmer_thermal'] = True # metals as volumes\n"
                                    "settings['refined_cellsize'] = 2\n");
    for (int i = 0; i < 10; ++i)
        broken += QStringLiteral("settings['elmer_thermal'] = True\n");
    broken += createLine;
    const QString repaired = w.testApplyElmerThermalWorkflow(broken);
    QCOMPARE(repaired.count(QStringLiteral("['elmer_thermal']")), 1);
    QVERIFY(repaired.contains(QStringLiteral("settings['refined_cellsize'] = 2\n") + createLine));

    // Missing key: inserted once, before create_elmer_thermal.
    const QString added = w.testApplyElmerThermalWorkflow(createLine);
    QCOMPARE(added, QStringLiteral("settings['elmer_thermal'] = True\n") + createLine);
    QCOMPARE(w.testApplyElmerThermalWorkflow(added), added);
}

void ElmerTest::elmerSolverStage_runsSolverWithoutRunElmerScript()
{
#ifdef Q_OS_WIN
    QSKIP("Windows always starts ELMER_SOLVER_PATH directly.");
#else
    MainWindow w;
    const QString stub = ensureTestElmerSolverStub();
    QVERIFY2(!stub.isEmpty(), "Elmer solver stub not found");
    QFile::setPermissions(stub, QFile::permissions(stub) | QFileDevice::ExeUser
                                    | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    w.testSetPreference(QStringLiteral("ELMER_SOLVER_PATH"), stub);

    // Thermal run folder as gds2palace leaves it: case.sif + STARTINFO, no run_elmer.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString &name : {QStringLiteral("case.sif"), QStringLiteral("ELMERSOLVER_STARTINFO")}) {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("case.sif\n");
    }
    const QStringList started = w.testStartElmerSolverStage(dir.path());
    QCOMPARE(started, QStringList({stub, QStringLiteral("case.sif")}));
    QVERIFY(w.testSimulationLogText().contains(QStringLiteral("No run_elmer script: starting")));

    // A run_elmer script (Elmer EM template) is still preferred.
    QFile script(dir.filePath(QStringLiteral("run_elmer")));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/bash\nexit 0\n");
    script.close();
    const QStringList viaScript = w.testStartElmerSolverStage(dir.path());
    QCOMPARE(viaScript.value(0), QStringLiteral("bash"));
    QVERIFY(viaScript.contains(QStringLiteral("./run_elmer")));

    // Neither a script nor case.sif: clear error, nothing started.
    QTemporaryDir empty;
    QVERIFY(w.testStartElmerSolverStage(empty.path()).isEmpty());
    QVERIFY(w.testMainLogText().contains(QStringLiteral("No run_elmer script and no case.sif")));
#endif
}

void ElmerTest::loadModel_selectsSettingsCellnameAndKeepsItOnSave()
{
    const QString gds = QDir(repoScriptsDir())
            .absoluteFilePath(QStringLiteral("../examples/elmer/thermal_simplest/simplest_with_source.gds"));
    QVERIFY2(QFileInfo::exists(gds), qPrintable(gds)); // 11 cells

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto writeModel = [&](const QString &name, const QString &cell) {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return QString();
        f.write(QStringLiteral(
                    "from gds2palace import *\n"
                    "settings = {}\n"
                    "settings['GdsFile'] = \"%1\"\n"
                    "settings['cellname'] = \"%2\"\n"
                    "settings['elmer_thermal'] = True\n"
                    "thermal_objects = simulation_setup.all_thermal_objects()\n"
                    "config_name, data_dir = simulation_setup.create_elmer_thermal (settings)\n")
                    .arg(gds, cell).toUtf8());
        return path;
    };
    auto readFile = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    };

    const QString modelA = writeModel(QStringLiteral("a.py"), QStringLiteral("TM1_M5_CDNS_759845918921"));
    const QString modelB = writeModel(QStringLiteral("b.py"), QStringLiteral("HeatSpreader01B_M"));

    // Parser: settings['cellname'] is the model's top cell (gds2palace style).
    QCOMPARE(PythonParser::parseSettings(modelB).getCellName(), QStringLiteral("HeatSpreader01B_M"));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *cbx = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    QVERIFY(cbx);

    // Another model first, so the dropdown holds a different cell when B loads.
    w.loadPythonModel(modelA);
    QCOMPARE(cbx->currentText(), QStringLiteral("TM1_M5_CDNS_759845918921"));
    w.loadPythonModel(modelB);
    QCOMPARE(cbx->currentText(), QStringLiteral("HeatSpreader01B_M"));

    // Save keeps the model's cell; so does a reload.
    w.testTriggerSave();
    QVERIFY2(readFile(modelB).contains(QStringLiteral("settings['cellname'] = \"HeatSpreader01B_M\"")),
             qPrintable(readFile(modelB)));
    w.loadPythonModel(modelA);
    w.loadPythonModel(modelB);
    QCOMPARE(cbx->currentText(), QStringLiteral("HeatSpreader01B_M"));
}

/*!*******************************************************************************************************************
 * \brief The purposelist of read_gds is resolved from a literal, a settings key or a variable, as keyword
 *        or third positional argument; anything else stays unknown (the preview then shows all).
 **********************************************************************************************************************/
void ElmerTest::readGdsPurposes_resolvesPurposelist()
{
    using P = PythonParser::GdsPurposes;
    P p = PythonParser::readGdsPurposes(QStringLiteral(
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0], metals_list=m)\n"));
    QVERIFY(p.known);
    QCOMPARE(p.purposes, QSet<int>({0}));

    // Template style: settings['purpose'] with a trailing comment; multi-line call.
    p = PythonParser::readGdsPurposes(QStringLiteral(
        "settings['purpose'] = [0, 2] # Which GDSII data type is evaluated?\n"
        "allpolygons = gds_reader.read_gds(settings['GdsFile'],\n"
        "    layernumbers,\n"
        "    purposelist=settings['purpose'])\n"));
    QVERIFY(p.known);
    QCOMPARE(p.settingsKey, QStringLiteral("purpose"));
    QCOMPARE(p.purposes, QSet<int>({0, 2}));

    // Positional (third argument) variable.
    p = PythonParser::readGdsPurposes(QStringLiteral(
        "purpose = [28]\nallpolygons = gds_reader.read_gds(f, layers, purpose, metals_list=m)\n"));
    QVERIFY(p.known);
    QCOMPARE(p.variable, QStringLiteral("purpose"));
    QCOMPARE(p.purposes, QSet<int>({28}));

    // Not resolvable: reassigned variable, computed list, no argument, no call.
    QVERIFY(!PythonParser::readGdsPurposes(QStringLiteral(
        "purpose = [0]\npurpose = [2]\nx = read_gds(f, l, purposelist=purpose)\n")).known);
    QVERIFY(!PythonParser::readGdsPurposes(QStringLiteral(
        "x = read_gds(f, l, purposelist=list(range(3)))\n")).known);
    QVERIFY(!PythonParser::readGdsPurposes(QStringLiteral("x = read_gds(f, l)\n")).known);
    QVERIFY(!PythonParser::readGdsPurposes(QStringLiteral("settings['purpose'] = [0]\n")).known);

    QSet<int> list;
    QVERIFY(PythonParser::parseIntList(QStringLiteral(" [ 0 , 2, ] "), &list));
    QCOMPARE(list, QSet<int>({0, 2}));
    QVERIFY(!PythonParser::parseIntList(QStringLiteral("[0.5]"), &list));
}

void ElmerTest::readGdsCellRef_findsTheCellArgument()
{
    using Ref = PythonParser::ReadGdsCellRef;
    // Multi-line call, settings key (gds2palace style).
    Ref r = PythonParser::readGdsCellRef(QStringLiteral(
        "allpolygons = gds_reader.read_gds(settings['GdsFile'],\n"
        "    layernumbers,\n"
        "    cellname=settings['cellname'],\n"
        "    purposelist=settings['purpose'])\n"));
    QVERIFY(r.found);
    QCOMPARE(r.settingsKey, QStringLiteral("cellname"));

    // Variable with spaces (gds2openEMS example style).
    r = PythonParser::readGdsCellRef(QStringLiteral("x = read_gds(f, l,\n  cellname = gds_cellname)\n"));
    QCOMPARE(r.variable, QStringLiteral("gds_cellname"));

    // Literal.
    const QString lit = QStringLiteral("x = read_gds(f, cellname=\"TOP\", purposelist=[0])\n");
    r = PythonParser::readGdsCellRef(lit);
    QVERIFY(r.hasLiteral);
    QCOMPARE(r.literal, QStringLiteral("TOP"));
    QCOMPARE(lit.mid(r.literalStart, r.literalLength), QStringLiteral("\"TOP\""));

    // No cellname argument (IHP workflow scripts): top cell. Commented calls don't count.
    r = PythonParser::readGdsCellRef(QStringLiteral(
        "# read_gds(f, cellname=old)\n"
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0], metals_list=m)\n"));
    QVERIFY(r.found);
    QVERIFY(r.variable.isEmpty() && r.settingsKey.isEmpty() && !r.hasLiteral);
    QVERIFY(!PythonParser::readGdsCellRef(QStringLiteral("settings['cellname'] = 'A'\n")).found);

    // The parser takes the read_gds target, not a stale gds_cellname.
    const PythonParser::Result res = PythonParser::parseSettingsFromText(QStringLiteral(
        "gds_cellname = \"Stale\"\n"
        "cellname = \"Used\"\n"
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, cellname=cellname)\n"));
    QCOMPARE(res.getCellName(), QStringLiteral("Used"));
    // read_gds without the argument: no cell name (= GDS top cell), even with gds_cellname set.
    QVERIFY(PythonParser::parseSettingsFromText(QStringLiteral(
        "gds_cellname = \"TOP\"\n"
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers)\n")).getCellName().isEmpty());
}

void ElmerTest::applyTopCell_writesOnlyTheReadGdsVariable()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *cbx = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    QVERIFY(cbx);
    auto select = [&](const QString &cell) {
        QSignalBlocker b(cbx);
        cbx->clear();
        cbx->addItem(cell);
        cbx->setCurrentIndex(0);
    };
    select(QStringLiteral("NewCell"));

    // Top-level variable used by read_gds: updated, comment kept; other cell lines untouched.
    QString out = w.testApplyGdsAndXmlPaths(QStringLiteral(
        "gds_filename = \"x.gds\"\n"
        "gds_cellname = \"Other\"\n"
        "cellname = \"\"  # optional, set empty string \"\" to use top cell\n"
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, cellname=cellname)\n"),
        QStringLiteral("openems"));
    QVERIFY2(out.contains(QStringLiteral("cellname = \"NewCell\"  # optional, set empty string \"\" to use top cell")),
             qPrintable(out));
    QVERIFY(out.contains(QStringLiteral("gds_cellname = \"Other\"")));

    // read_gds without cellname: nothing is written, nothing invented.
    const QString noArg = QStringLiteral(
        "gds_filename = \"x.gds\"\n"
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0])\n");
    QCOMPARE(w.testApplyGdsAndXmlPaths(noArg, QStringLiteral("palace")), noArg);

    // settings key used by read_gds but missing: added after settings = {}.
    out = w.testApplyGdsAndXmlPaths(QStringLiteral(
        "settings = {}\n"
        "allpolygons = gds_reader.read_gds(f, l, cellname=settings['cellname'])\n"),
        QStringLiteral("elmer_thermal"));
    QVERIFY2(out.startsWith(QStringLiteral("settings = {}\nsettings['cellname'] = \"NewCell\"\n")), qPrintable(out));

    // Undefined variable used by read_gds: defined before gds_filename.
    out = w.testApplyGdsAndXmlPaths(QStringLiteral(
        "gds_filename = \"x.gds\"\n"
        "allpolygons = gds_reader.read_gds(gds_filename, l, cellname=gds_cellname)\n"),
        QStringLiteral("palace"));
    QVERIFY2(out.startsWith(QStringLiteral("gds_cellname = \"NewCell\"\ngds_filename")), qPrintable(out));

    // Literal argument: replaced in place.
    out = w.testApplyGdsAndXmlPaths(QStringLiteral("x = read_gds(f, cellname='Old', p=[0])\n"),
                                    QStringLiteral("palace"));
    QCOMPARE(out, QStringLiteral("x = read_gds(f, cellname=\"NewCell\", p=[0])\n"));
}

void ElmerTest::loadModel_withoutCellSelectsGdsTopCell()
{
    const QString gds = QDir(repoScriptsDir())
            .absoluteFilePath(QStringLiteral("../examples/elmer/thermal_simplest/simplest_with_source.gds"));
    QVERIFY2(QFileInfo::exists(gds), qPrintable(gds));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto write = [&](const QString &name, const QString &body) {
        QFile f(dir.filePath(name));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(body.arg(gds).toUtf8());
        return dir.filePath(name);
    };
    // A model with a sub-cell first, so a stale selection would show.
    const QString withCell = write(QStringLiteral("cell.py"), QStringLiteral(
        "settings = {}\n"
        "settings['GdsFile'] = \"%1\"\n"
        "settings['cellname'] = \"TM1_M5_CDNS_759845918921\"\n"
        "allpolygons = gds_reader.read_gds(settings['GdsFile'], l, cellname=settings['cellname'])\n"));
    // IHP workflow style: read_gds without cellname, plus a stale gds_cellname.
    const QString noCell = write(QStringLiteral("nocell.py"), QStringLiteral(
        "gds_cellname = \"TM1_M5_CDNS_759845918921\"\n"
        "gds_filename = \"%1\"\n"
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0])\n"));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *cbx = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    QVERIFY(cbx);
    w.loadPythonModel(withCell);
    QCOMPARE(cbx->currentText(), QStringLiteral("TM1_M5_CDNS_759845918921"));
    w.loadPythonModel(noCell);
    // gdstk top_level()[0] of this GDS, the cell gds2palace loads without a cell name.
    QCOMPARE(cbx->currentText(), QStringLiteral("0_INT_T595_HeatSpreader"));
    // The cell list is unchanged (all cells, file order).
    QCOMPARE(cbx->count(), 11);

    // Next model, other GDS, no cell name: the previous model's cell is not looked up there,
    // so no "not found in GDS" message.
    w.loadPythonModel(withCell);
    const QString otherGds = QDir(repoScriptsDir())
            .absoluteFilePath(QStringLiteral("../examples/palace/resistors_rsil/resistors_with_ports.gds"));
    QVERIFY2(QFileInfo::exists(otherGds), qPrintable(otherGds));
    const QString other = dir.filePath(QStringLiteral("other.py"));
    {
        QFile f(other);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(QStringLiteral("settings = {}\n"
                               "settings['GdsFile'] = \"%1\"\n"
                               "settings['cellname'] = ''\n"
                               "allpolygons = gds_reader.read_gds(settings['GdsFile'], l, cellname=settings['cellname'])\n")
                    .arg(otherGds).toUtf8());
    }
    const int logBefore = w.testMainLogText().size();
    w.loadPythonModel(other);
    const QString newLog = w.testMainLogText().mid(logBefore);
    QVERIFY2(!newLog.contains(QStringLiteral("not found in GDS"))
             && !newLog.contains(QStringLiteral("not in the GDS")), qPrintable(newLog));

    // A model naming a cell its GDS doesn't have: the dropdown shows the top cell (what the
    // script loads), without a log note.
    const QString missing = dir.filePath(QStringLiteral("missing.py"));
    {
        QFile f(missing);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(QStringLiteral("settings = {}\n"
                               "settings['GdsFile'] = \"%1\"\n"
                               "settings['cellname'] = '50_ghz_mpa_core'\n"
                               "allpolygons = gds_reader.read_gds(settings['GdsFile'], l, cellname=settings['cellname'])\n")
                    .arg(otherGds).toUtf8());
    }
    const int logBefore2 = w.testMainLogText().size();
    w.loadPythonModel(missing);
    QCOMPARE(cbx->currentText(), QStringLiteral("resistors"));
    const QString log2 = w.testMainLogText().mid(logBefore2);
    QVERIFY2(!log2.contains(QStringLiteral("50_ghz_mpa_core")), qPrintable(log2));
}

void ElmerTest::loadModel_findsMissingInputFilesNextToModel()
{
    const QString examples = QDir(repoScriptsDir()).absoluteFilePath(QStringLiteral("../examples/elmer/thermal_simplest"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // The model's folder holds the inputs; the script still points to another machine.
    QVERIFY(QFile::copy(examples + QStringLiteral("/simplest_with_source.gds"),
                        dir.filePath(QStringLiteral("simplest_with_source.gds"))));
    QVERIFY(QFile::copy(examples + QStringLiteral("/SG13_interposer_thermal_typicalvalues.xml"),
                        dir.filePath(QStringLiteral("stack.xml"))));
    const QString model = dir.filePath(QStringLiteral("moved.py"));
    {
        QFile f(model);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("from gds2palace import *\n"
                "settings = {}\n"
                "settings['GdsFile'] = \"C:/Users/anton/Documents/EMStudio/simplest_with_source.gds\"\n"
                "settings['SubstrateFile'] = \"C:\\\\Users\\\\anton\\\\stack.xml\"\n"
                "settings['cellname'] = \"HeatSpreader01B_M\"\n"
                "settings['elmer_thermal'] = True\n"
                "allpolygons = gds_reader.read_gds(settings['GdsFile'], l, cellname=settings['cellname'])\n"
                "config_name, data_dir = simulation_setup.create_elmer_thermal (settings)\n");
    }

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.loadPythonModel(model);
    auto *gdsEdit = w.findChild<QLineEdit *>(QStringLiteral("txtGdsFile"));
    auto *xmlEdit = w.findChild<QLineEdit *>(QStringLiteral("txtSubstrate"));
    QVERIFY(gdsEdit && xmlEdit);
    QCOMPARE(QFileInfo(gdsEdit->text()), QFileInfo(dir.filePath(QStringLiteral("simplest_with_source.gds"))));
    QCOMPARE(QFileInfo(xmlEdit->text()), QFileInfo(dir.filePath(QStringLiteral("stack.xml"))));
    QVERIFY(w.testMainLogText().contains(QStringLiteral("using")));
    // The top cell resolves against the found GDS.
    QCOMPARE(w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"))->currentText(),
             QStringLiteral("HeatSpreader01B_M"));

    // Save writes the local paths into the script.
    w.testTriggerSave();
    QFile f(model);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString saved = QString::fromUtf8(f.readAll());
    QVERIFY2(saved.contains(QStringLiteral("settings['GdsFile'] = \"%1\"")
                                .arg(QDir::fromNativeSeparators(dir.filePath(QStringLiteral("simplest_with_source.gds"))))),
             qPrintable(saved));
    QVERIFY2(saved.contains(QStringLiteral("settings['SubstrateFile'] = \"%1\"")
                                .arg(QDir::fromNativeSeparators(dir.filePath(QStringLiteral("stack.xml"))))),
             qPrintable(saved));
    QVERIFY(!saved.contains(QStringLiteral("anton")));
}

/*! Stackup Variable overrides are read from and written to whatever read_substrate() passes:
 *  a settings['variable_overrides'] entry (gds2palace examples) or a top-level variable (templates). */
void ElmerTest::stackupOverrides_followReadSubstrateArgument()
{
    const QString examples = QDir(repoScriptsDir()).absoluteFilePath(QStringLiteral("../examples/palace/resistors_rsil"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QFile::copy(examples + QStringLiteral("/resistors_with_ports.gds"), dir.filePath(QStringLiteral("r.gds"))));
    QVERIFY(QFile::copy(examples + QStringLiteral("/SG13G2_resistors_200um.xml"), dir.filePath(QStringLiteral("stack.xml"))));

    auto runCase = [&](const QString &name, const QString &head, const QString &readCall,
                       const QString &savedDictPrefix) {
        const QString model = dir.filePath(name);
        {
            QFile f(model);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write((QStringLiteral("from gds2palace import *\n"
                                    "settings = {}\n"
                                    "settings['unit'] = 1e-06\n"
                                    "settings['GdsFile'] = \"%1\"\n"
                                    "settings['SubstrateFile'] = \"%2\"\n")
                         .arg(QDir::fromNativeSeparators(dir.filePath(QStringLiteral("r.gds"))),
                              QDir::fromNativeSeparators(dir.filePath(QStringLiteral("stack.xml"))))
                     + head
                     + QStringLiteral("simulation_ports = simulation_setup.all_simulation_ports()\n")
                     + readCall
                     + QStringLiteral("allpolygons = gds_reader.read_gds(settings['GdsFile'], l, purposelist=[0])\n"
                                      "config_name, data_dir = simulation_setup.create_palace (excite_ports, settings)\n"))
                        .toUtf8());
        }

        MainWindow w;
        w.setAttribute(Qt::WA_DontShowOnScreen, true);
        w.loadPythonModel(model);

        // Loaded into the Substrate tab's override table.
        auto *tbl = w.findChild<QTableWidget *>(QStringLiteral("tblStackupOverrides"));
        QVERIFY(tbl);
        QHash<QString, QString> shown;
        for (int r = 0; r < tbl->rowCount(); ++r)
            if (tbl->item(r, 0) && tbl->item(r, 2))
                shown.insert(tbl->item(r, 0)->text(), tbl->item(r, 2)->text());
        QCOMPARE(shown.value(QStringLiteral("air_thickness")), QStringLiteral("80.0"));
        QCOMPARE(shown.value(QStringLiteral("total_thickness")), QStringLiteral("100.0"));

        // Edit one override, then Save.
        for (int r = 0; r < tbl->rowCount(); ++r)
            if (tbl->item(r, 0) && tbl->item(r, 0)->text() == QLatin1String("total_thickness"))
                tbl->item(r, 2)->setText(QStringLiteral("120"));
        w.testTriggerSave();

        QFile f(model);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString saved = QString::fromUtf8(f.readAll());
        QVERIFY2(saved.contains(savedDictPrefix + QStringLiteral("{'air_thickness': 80.0, 'total_thickness': 120}")),
                 qPrintable(saved));
        QVERIFY2(saved.contains(readCall.trimmed()), qPrintable(saved));
        QCOMPARE(saved.count(QStringLiteral("variable_overrides =")) + saved.count(QStringLiteral("'variable_overrides'] =")), 1);
    };

    // gds2palace example style (more_examples/core_transistor_3port_bce).
    runCase(QStringLiteral("dict.py"),
            QStringLiteral("settings['variable_overrides'] = {'air_thickness': 80.0, 'total_thickness': 100.0}\n"),
            QStringLiteral("materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate "
                           "(settings['SubstrateFile'], variable_overrides=settings['variable_overrides'])\n"),
            QStringLiteral("settings['variable_overrides'] = "));
    // EMStudio template style.
    runCase(QStringLiteral("toplevel.py"),
            QStringLiteral("variable_overrides = {'air_thickness': 80.0, 'total_thickness': 100.0}\n"),
            QStringLiteral("materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate"
                           "(settings['SubstrateFile'], variable_overrides=variable_overrides)\n"),
            QStringLiteral("variable_overrides = "));
}

/*! An override given as a name or expression (e.g. a loop variable) stays code on Save instead of
 *  becoming a quoted string; only an edited value is rewritten. */
void ElmerTest::stackupOverrides_keepUnquotedExpressions()
{
    const QString examples = QDir(repoScriptsDir()).absoluteFilePath(QStringLiteral("../examples/palace/resistors_rsil"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QFile::copy(examples + QStringLiteral("/resistors_with_ports.gds"), dir.filePath(QStringLiteral("r.gds"))));
    QVERIFY(QFile::copy(examples + QStringLiteral("/SG13G2_resistors_200um.xml"), dir.filePath(QStringLiteral("stack.xml"))));

    const QString model = dir.filePath(QStringLiteral("loop.py"));
    {
        QFile f(model);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(QStringLiteral("from gds2palace import *\n"
                               "settings = {}\n"
                               "settings['unit'] = 1e-06\n"
                               "settings['GdsFile'] = \"%1\"\n"
                               "settings['SubstrateFile'] = \"%2\"\n"
                               "for total in [100.0, 120.0]:\n"
                               "    variable_overrides = {'air_thickness': air, 'total_thickness': max(total, 100.0)}\n"
                               "    simulation_ports = simulation_setup.all_simulation_ports()\n"
                               "    materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate"
                               "(settings['SubstrateFile'], variable_overrides=variable_overrides)\n"
                               "    allpolygons = gds_reader.read_gds(settings['GdsFile'], l, purposelist=[0])\n"
                               "    config_name, data_dir = simulation_setup.create_palace (excite_ports, settings)\n")
                    .arg(QDir::fromNativeSeparators(dir.filePath(QStringLiteral("r.gds"))),
                         QDir::fromNativeSeparators(dir.filePath(QStringLiteral("stack.xml"))))
                    .toUtf8());
    }

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.loadPythonModel(model);

    auto *tbl = w.findChild<QTableWidget *>(QStringLiteral("tblStackupOverrides"));
    QVERIFY(tbl);
    auto overrideItem = [&](const QString &name) -> QTableWidgetItem * {
        for (int r = 0; r < tbl->rowCount(); ++r)
            if (tbl->item(r, 0) && tbl->item(r, 0)->text() == name)
                return tbl->item(r, 2);
        return nullptr;
    };
    QVERIFY(overrideItem(QStringLiteral("air_thickness")));
    QVERIFY(overrideItem(QStringLiteral("total_thickness")));
    QCOMPARE(overrideItem(QStringLiteral("air_thickness"))->text(), QStringLiteral("air"));
    QCOMPARE(overrideItem(QStringLiteral("total_thickness"))->text(), QStringLiteral("max(total, 100.0)"));

    auto savedText = [&]() {
        QFile f(model);
        return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    };

    // Unchanged table: the dict keeps its expressions.
    w.testTriggerSave();
    QString saved = savedText();
    QVERIFY2(saved.contains(QStringLiteral(
                 "    variable_overrides = {'air_thickness': air, 'total_thickness': max(total, 100.0)}\n")),
             qPrintable(saved));

    // A value typed in the table is a string; the untouched expression stays code.
    overrideItem(QStringLiteral("air_thickness"))->setText(QStringLiteral("thick"));
    w.testTriggerSave();
    saved = savedText();
    QVERIFY2(saved.contains(QStringLiteral(
                 "    variable_overrides = {'air_thickness': 'thick', 'total_thickness': max(total, 100.0)}\n")),
             qPrintable(saved));
}

/*! Thermal objects and the cell variable inside a loop keep their indentation; an unchanged
 *  thermal block (other formatting) is left alone, comments after it stay. */
void ElmerTest::indentedThermalBlockAndCell_keepIndentation()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    const QString script = QStringLiteral(
        "for power in [0.1, 0.2]:\n"
        "    thermal_objects = simulation_setup.all_thermal_objects()\n"
        "    # heat source on layer 201\n"
        "    thermal_objects.add_heatsource(simulation_setup.heatsource(power=0.65,\n"
        "                                   source_layernum=201, target_layername='TFR'))\n"
        "\n"
        "    # ======== simulation ========\n"
        "    x = 1\n");
    const QString same = QStringLiteral(
        "thermal_objects = simulation_setup.all_thermal_objects()\n"
        "thermal_objects.add_heatsource(simulation_setup.heatsource(power=0.65, source_layernum=201, target_layername='TFR'))\n");
    QCOMPARE(w.testReplaceThermalSection(script, same), script);

    const QString changed = QString(same).replace(QStringLiteral("0.65"), QStringLiteral("0.7"));
    const QString out = w.testReplaceThermalSection(script, changed);
    QVERIFY2(out.contains(QStringLiteral("\n    thermal_objects = simulation_setup.all_thermal_objects()\n"
                                         "    thermal_objects.add_heatsource(simulation_setup.heatsource(power=0.7,")),
             qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("\n    # ======== simulation ========\n    x = 1\n")), qPrintable(out));

    // An indented cell variable keeps its indentation.
    auto *cbx = w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"));
    QVERIFY(cbx);
    {
        QSignalBlocker b(cbx);
        cbx->clear();
        cbx->addItem(QStringLiteral("NewCell"));
    }
    const QString cellOut = w.testApplyGdsAndXmlPaths(QStringLiteral(
        "for i in range(2):\n"
        "    gds_cellname = \"Old\"\n"
        "    allpolygons = gds_reader.read_gds(f, l, cellname=gds_cellname)\n"), QStringLiteral("palace"));
    QVERIFY2(cellOut.contains(QStringLiteral("\n    gds_cellname = \"NewCell\"\n")), qPrintable(cellOut));
}

/*!*******************************************************************************************************************
 * \brief The layout preview of an Elmer Thermal model draws the Thermal table objects (heat source
 *        volume, constant-temperature faces on the target layers), not EM ports with directions.
 **********************************************************************************************************************/
void ElmerTest::layoutPreview_drawsThermalObjectsNotPorts()
{
    const QString examples = QDir(repoScriptsDir()).absoluteFilePath(QStringLiteral("../examples/elmer/thermal_simplest"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString &name : {QStringLiteral("simplest_with_source.gds"),
                                QStringLiteral("SG13_interposer_thermal_typicalvalues.xml"),
                                QStringLiteral("elmer_thermal_simplest_typicalvalues.py")})
        QVERIFY(QFile::copy(examples + QLatin1Char('/') + name, dir.filePath(name)));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.loadPythonModel(dir.filePath(QStringLiteral("elmer_thermal_simplest_typicalvalues.py")));
    auto *view = w.findChild<LayoutView *>();
    QVERIFY(view && view->scene());
    view->setViewMode(LayoutView::ViewMode::Iso3D);
    // The view mode is persisted; later suites expect the 2D default.
    struct Restore2d {
        LayoutView *v;
        ~Restore2d() { v->setViewMode(LayoutView::ViewMode::Top2D); }
    } restore2d{view};

    QMap<int, QStringList> tips;  // GDS marker layer -> tooltips of its items
    for (QGraphicsItem *item : view->scene()->items()) {
        const int gds = item->data(3).toInt();
        if (gds == 201 || gds == 202)
            tips[gds] << item->toolTip();
    }
    QVERIFY2(!tips.value(201).isEmpty() && !tips.value(202).isEmpty(), "thermal markers not drawn");
    const QString heat = tips.value(201).join(QLatin1Char('\n'));
    const QString temp = tips.value(202).join(QLatin1Char('\n'));
    QVERIFY2(heat.contains(QStringLiteral("Heat 0.65 W: heat source on GDS 201, volume in TFR")), qPrintable(heat));
    QVERIFY2(temp.contains(QStringLiteral("T 298 K: constant temperature on GDS 202, faces of BACKSIDEGND")),
             qPrintable(temp));
    // No port arrows or port surfaces.
    QVERIFY2(!heat.contains(QStringLiteral("port")) && !temp.contains(QStringLiteral("port"))
                 && !heat.contains(QStringLiteral("direction")) && !temp.contains(QStringLiteral("direction")),
             qPrintable(heat + QLatin1Char('\n') + temp));
}

/*!*******************************************************************************************************************
 * \brief Thermal target layers are a dropdown of stackup layers: heat sources offer conductors, constant
 *        temperatures sheets then conductors. A model value that isn't offered stays selected (marked) and
 *        is written back unchanged.
 **********************************************************************************************************************/
void ElmerTest::thermalTargets_offerStackupLayersByType()
{
    const QString examples = QDir(repoScriptsDir()).absoluteFilePath(QStringLiteral("../examples/elmer/thermal_simplest"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString &name : {QStringLiteral("simplest_with_source.gds"),
                                QStringLiteral("SG13_interposer_thermal_typicalvalues.xml")})
        QVERIFY(QFile::copy(examples + QLatin1Char('/') + name, dir.filePath(name)));
    QFile in(examples + QStringLiteral("/elmer_thermal_simplest_typicalvalues.py"));
    QVERIFY(in.open(QIODevice::ReadOnly | QIODevice::Text));
    QString script = QString::fromUtf8(in.readAll());
    // A third object whose target is not in the stackup.
    script.replace(QStringLiteral("thermal_objects.add_consttemp("),
                   QStringLiteral("thermal_objects.add_heatsource(simulation_setup.heatsource(power=0.1, "
                                  "source_layernum=203, target_layername='NOPE'))\nthermal_objects.add_consttemp("));
    const QString model = dir.filePath(QStringLiteral("thermal.py"));
    {
        QFile out(model);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Text));
        out.write(script.toUtf8());
    }

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.loadPythonModel(model);
    auto *table = w.findChild<QTableWidget *>(QStringLiteral("tblThermalObjects"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 3);
    auto typeBox = [table](int r) { return qobject_cast<QComboBox *>(table->cellWidget(r, 0)); };
    auto targetBox = [table](int r) { return qobject_cast<QComboBox *>(table->cellWidget(r, 3)); };
    auto offered = [](QComboBox *b) {
        QStringList names;
        for (int i = 0; i < b->count(); ++i)
            if (b->itemData(i, Qt::UserRole + 1).toString() != QLatin1String("invalid"))
                names << b->itemData(i).toString();
        return names;
    };
    QVERIFY(typeBox(0) && targetBox(0) && targetBox(1) && targetBox(2));
    QVERIFY(!targetBox(0)->isEditable());

    // Heat source on TFR: conductors only (no vias such as TopVia1, no LBE).
    QCOMPARE(typeBox(0)->currentText(), QStringLiteral("heatsource"));
    QCOMPARE(targetBox(0)->currentData().toString(), QStringLiteral("TFR"));
    const QStringList heat = offered(targetBox(0));
    QVERIFY(heat.contains(QStringLiteral("TFR")) && heat.contains(QStringLiteral("Metal5")));
    QVERIFY(!heat.contains(QStringLiteral("TopVia1")) && !heat.contains(QStringLiteral("LBE")));

    // Missing target: kept, marked, written back unchanged.
    QCOMPARE(targetBox(1)->currentData().toString(), QStringLiteral("NOPE"));
    QVERIFY(targetBox(1)->currentText().contains(QStringLiteral("not in stackup")));
    QVERIFY(!targetBox(1)->styleSheet().isEmpty());

    // Constant temperature on BACKSIDEGND (a conductor in this stackup): sheets then conductors.
    QCOMPARE(typeBox(2)->currentText(), QStringLiteral("consttemp"));
    QCOMPARE(targetBox(2)->currentData().toString(), QStringLiteral("BACKSIDEGND"));
    QVERIFY(targetBox(2)->styleSheet().isEmpty());

    const QString code = w.testBuildThermalCodeFromGui();
    QVERIFY2(code.contains(QStringLiteral("target_layername='TFR'")), qPrintable(code));
    QVERIFY2(code.contains(QStringLiteral("target_layername='NOPE'")), qPrintable(code));
    QVERIFY2(code.contains(QStringLiteral("target_layername='BACKSIDEGND'")), qPrintable(code));
    QVERIFY(!code.contains(QStringLiteral("not in stackup")));

    // Switching the type refills the list and keeps the layer (TFR is a conductor: valid for both).
    typeBox(0)->setCurrentText(QStringLiteral("consttemp"));
    QCOMPARE(targetBox(0)->currentData().toString(), QStringLiteral("TFR"));
    QVERIFY(targetBox(0)->styleSheet().isEmpty());

    // A new row targets the first offered conductor.
    w.testClickAddThermalObject();
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(targetBox(3)->currentData().toString(), offered(targetBox(3)).first());
}

/*!*******************************************************************************************************************
 * \brief fdump is a checkbox for Elmer EM and a list otherwise. A tool switch re-types the grid row, and Save
 *        never writes fdump = True / False (gds2palace crashed on it after Elmer EM → Palace).
 **********************************************************************************************************************/
void ElmerTest::fdump_neverWrittenAsBoolAfterToolSwitch()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    QString err;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("fdump_model.py"));

    auto saved = [&path]() {
        QFile f(path);
        return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    };
    auto fdumpLine = [&saved]() {
        const QRegularExpressionMatch m =
            QRegularExpression(QStringLiteral(R"((?m)^settings\['fdump'\]\s*=\s*([^#\n]*))")).match(saved());
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };

    // Elmer EM template: fdump = [] is an unchecked checkbox.
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_em"), &err), qPrintable(err));
    QVERIFY(w.testInitDefaultElmerEmModel());
    w.testSetRunPythonScriptLinePath(path);
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[]"));
    QCOMPARE(w.testSettingPropertyType(QStringLiteral("fdump")), int(QVariant::Bool));

    // Switch to Palace: the row becomes a list, Save keeps [] (wrote False before).
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QCOMPARE(w.testSettingPropertyType(QStringLiteral("fdump")), int(QVariant::String));
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[]"));

    // Checked under Elmer EM, then switched to Palace: a list, never True.
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_em"), &err), qPrintable(err));
    QCOMPARE(w.testSettingPropertyType(QStringLiteral("fdump")), int(QVariant::Bool));
    w.testSetSimSetting(QStringLiteral("fdump"), true);
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QCOMPARE(w.testSettingPropertyType(QStringLiteral("fdump")), int(QVariant::String));
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[settings['fstop']]"));

    // Writer alone (a bool left in the settings under Palace): still a list.
    w.testSetSimSetting(QStringLiteral("fdump"), false);
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[]"));
    w.testSetSimSetting(QStringLiteral("fdump"), true);
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[settings['fstop']]"));

    // Palace list → Elmer EM: a checked checkbox; an untouched checkbox keeps the list as written.
    w.testSetSimSetting(QStringLiteral("fdump"), QStringLiteral("[2e9, 5e9]"));
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[2e9, 5e9]"));
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("elmer_em"), &err), qPrintable(err));
    QCOMPARE(w.testSettingPropertyType(QStringLiteral("fdump")), int(QVariant::Bool));
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[2e9, 5e9]"));
    w.testSetSimSetting(QStringLiteral("fdump"), false);
    w.testTriggerSave();
    QCOMPARE(fdumpLine(), QStringLiteral("[]"));

    // A checkbox value never replaces a list of another setting.
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QVERIFY(saved().contains(QStringLiteral("settings['fpoint']")));
    const QString fpointBefore =
        QRegularExpression(QStringLiteral(R"((?m)^settings\['fpoint'\].*$)")).match(saved()).captured(0);
    w.testSetSimSetting(QStringLiteral("fpoint"), true);
    w.testTriggerSave();
    QCOMPARE(QRegularExpression(QStringLiteral(R"((?m)^settings\['fpoint'\].*$)")).match(saved()).captured(0),
             fpointBefore);

    // Run check: a script that already has fdump = True is flagged.
    w.testSetEditorText(w.testEditorText().replace(QRegularExpression(QStringLiteral(R"(settings\['fdump'\]\s*=\s*\[\])")),
                                                   QStringLiteral("settings['fdump'] = True")));
    QVERIFY(w.testEditorText().contains(QStringLiteral("settings['fdump'] = True")));
    bool flagged = false;
    for (const SanityFinding &f : w.testCollectSanityFindings())
        flagged |= f.code == QLatin1String("fdump_bool") && f.severity == SanityFinding::Error;
    QVERIFY(flagged);
}

/*!*******************************************************************************************************************
 * \brief Choosing another tool in the list with a model open warns in the Log: between the gds2palace tools
 *        the workflow calls are adapted on Save (settings are not), to / from openEMS nothing is converted.
 *        Programmatic switches (File > New, model load, tests) and an empty editor don't warn.
 **********************************************************************************************************************/
void ElmerTest::toolSwitch_warnsOnlyForUserChoiceWithOpenModel()
{
    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *combo = w.findChild<QComboBox *>(QStringLiteral("cbxSimTool"));
    auto *log = w.findChild<QTextEdit *>(QStringLiteral("txtLog"));
    QVERIFY(combo && log);
    QString err;

    auto choose = [combo](const QString &key) {   // what a click in the list does
        const int idx = combo->findData(key);
        combo->setCurrentIndex(idx);
        emit combo->activated(idx);
    };
    auto logCount = [log](const QString &text) { return log->toPlainText().count(text); };

    // Empty editor: no warning.
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    w.testSetEditorText(QString());
    choose(QStringLiteral("elmer_em"));
    QCOMPARE(logCount(QStringLiteral("The open model")), 0);

    // Palace model → Elmer EM: same workflow family, settings not adapted.
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QVERIFY(w.testInitDefaultPalaceModel());
    choose(QStringLiteral("elmer_em"));
    QCOMPARE(logCount(QStringLiteral("adapts its workflow calls")), 1);

    // → openEMS: not converted at all.
    choose(QStringLiteral("openems"));
    QCOMPARE(logCount(QStringLiteral("will not run with")), 1);

    // Picking the current tool again, or a programmatic switch: nothing new.
    choose(QStringLiteral("openems"));
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &err), qPrintable(err));
    QCOMPARE(logCount(QStringLiteral("The open model")), 2);
}

/*! Loading a model replaces the previous model's inputs: its GDS / XML path, stackup overrides and top cell must
 *  not be written into a model whose paths the parser can't resolve (os.path.join), and a file that can't be
 *  read leaves the loaded model as it was. */
void ElmerTest::loadModel_replacesPreviousModelInputs()
{
    const QString examples = QDir(repoScriptsDir()).absoluteFilePath(QStringLiteral("../examples/palace/resistors_rsil"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QFile::copy(examples + QStringLiteral("/resistors_with_ports.gds"), dir.filePath(QStringLiteral("r.gds"))));
    QVERIFY(QFile::copy(examples + QStringLiteral("/SG13G2_resistors_200um.xml"), dir.filePath(QStringLiteral("stack.xml"))));
    auto writeModel = [&](const QString &name, const QString &body) {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return QString();
        f.write((QStringLiteral("import os\nfrom gds2palace import *\nsettings = {}\nsettings['unit'] = 1e-06\n") + body
                 + QStringLiteral("simulation_ports = simulation_setup.all_simulation_ports()\n"
                                  "allpolygons = gds_reader.read_gds(settings['GdsFile'], l, purposelist=[0])\n"
                                  "config_name, data_dir = simulation_setup.create_palace (excite_ports, settings)\n"))
                    .toUtf8());
        return path;
    };
    auto readFile = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
    };

    const QString modelA = writeModel(QStringLiteral("a.py"), QStringLiteral(
        "settings['GdsFile'] = \"%1\"\n"
        "settings['SubstrateFile'] = \"%2\"\n"
        "variable_overrides = {'air_thickness': 80.0}\n"
        "materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate "
        "(settings['SubstrateFile'], variable_overrides=variable_overrides)\n")
        .arg(QDir::fromNativeSeparators(dir.filePath(QStringLiteral("r.gds"))),
             QDir::fromNativeSeparators(dir.filePath(QStringLiteral("stack.xml")))));
    const QString bodyB = QStringLiteral(
        "settings['GdsFile'] = os.path.join(os.path.dirname(__file__), 'layout_b.gds')\n"
        "settings['SubstrateFile'] = os.path.join(os.path.dirname(__file__), 'stackup_b.xml')\n"
        "materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate (settings['SubstrateFile'])\n");
    const QString modelB = writeModel(QStringLiteral("b.py"), bodyB);

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *scriptPath = w.findChild<QLineEdit *>(QStringLiteral("txtRunPythonScript"));
    QVERIFY(scriptPath);
    QVERIFY(w.loadPythonModel(modelA));
    QVERIFY(!w.findChild<QComboBox *>(QStringLiteral("cbxTopCell"))->currentText().isEmpty());

    QVERIFY(w.loadPythonModel(modelB));
    QCOMPARE(scriptPath->text(), modelB);
    QVERIFY(w.findChild<QLineEdit *>(QStringLiteral("txtGdsFile"))->text().isEmpty());
    QVERIFY(w.findChild<QLineEdit *>(QStringLiteral("txtSubstrate"))->text().isEmpty());
    QVERIFY2(!w.testEditorText().contains(QStringLiteral("r.gds")), qPrintable(w.testEditorText()));
    w.testTriggerSave();
    const QString saved = readFile(modelB);
    QVERIFY2(saved.contains(bodyB), qPrintable(saved));
    QVERIFY2(!saved.contains(QStringLiteral("air_thickness")) && !saved.contains(QStringLiteral("r.gds"))
             && !saved.contains(QStringLiteral("stack.xml")),
             qPrintable(saved));

    // A file that can't be read: B stays loaded (title / path, editor).
    const QString editorBefore = w.testEditorText();
    QVERIFY(!w.loadPythonModel(dir.filePath(QStringLiteral("missing.py"))));
    QCOMPARE(scriptPath->text(), modelB);
    QCOMPARE(w.testEditorText(), editorBefore);
    QVERIFY(w.windowTitle().contains(QFileInfo(modelB).absoluteFilePath()));
}
