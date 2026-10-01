/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_elmer.h"

#include <QtTest/QtTest>
#include <QTabWidget>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QTextStream>

#include "mainwindow.h"
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
