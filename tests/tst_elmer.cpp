/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_elmer.h"

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "mainwindow.h"
#include "substrate.h"

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

void ElmerTest::resolveParaViewExecutable_usesPreferenceWhenPresent()
{
    MainWindow w;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

#ifdef Q_OS_WIN
    const QString fake = dir.filePath(QStringLiteral("paraview.exe"));
#else
    const QString fake = dir.filePath(QStringLiteral("paraview"));
#endif
    {
        QFile f(fake);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("stub");
        f.close();
    }
#ifndef Q_OS_WIN
    QFile::setPermissions(fake,
                          QFile::permissions(fake) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference(QStringLiteral("PARAVIEW_EXE"), fake);
    QCOMPARE(QDir::fromNativeSeparators(w.testResolveParaViewExecutable()),
             QDir::fromNativeSeparators(fake));
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

void ElmerTest::openThermalResults_noVtuIsNoop()
{
    MainWindow w;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // No VTU → early return; should not throw / hang.
    w.testOpenThermalResultsInParaView(dir.path());
}

void ElmerTest::openThermalResults_withVtuAndParaViewStub()
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

#ifdef Q_OS_WIN
    const QString pv = dir.filePath(QStringLiteral("paraview_stub.cmd"));
    {
        QFile f(pv);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "@echo off\r\nexit /b 0\r\n";
    }
#else
    const QString pv = dir.filePath(QStringLiteral("paraview_stub.sh"));
    {
        QFile f(pv);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "#!/bin/sh\nexit 0\n";
    }
    QFile::setPermissions(pv,
                          QFile::permissions(pv) |
                              QFileDevice::ExeUser |
                              QFileDevice::ExeGroup |
                              QFileDevice::ExeOther);
#endif

    w.testSetPreference(QStringLiteral("PARAVIEW_EXE"), pv);
    w.testOpenThermalResultsInParaView(dir.path());

    // Helper script should be written next to the VTU.
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("_emstudio_open_thermal_paraview.py"))));
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
