/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "test_utils.h"
#include "tst_simcfg_import.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "mainwindow.h"
#include "simcfgimport.h"

using namespace GoldenTestUtils;

namespace {

QJsonObject viaPort(int number, int layer, const QString &from, const QString &to, const QString &direction)
{
    return QJsonObject{{"portnumber", number}, {"source_layernum", layer}, {"target_layername", QJsonValue()},
                       {"from_layername", from}, {"to_layername", to}, {"direction", direction},
                       {"port_Z0", 50.0}, {"voltage", 1.0}};
}

/*!*******************************************************************************************************************
 * \brief A .simcfg as klayout_em_export writes it after setupEM saved settings into it: files, cell, purpose, two
 *        via ports with opposite polarity, and setupEM's frequency (GHz), mesh and boundary settings.
 **********************************************************************************************************************/
QJsonObject lineSimcfg(const QString &gds, const QString &xml)
{
    const QJsonObject values{
        {"unit", 1e-06}, {"purpose", QJsonArray{0}}, {"GdsFile", gds}, {"SubstrateFile", xml}, {"cellname", "t1"},
        {"fstart", 0.0}, {"fstop", 50.0}, {"fstep", 1.0}, {"fpoint", QJsonArray{10.0, 20.5}},
        {"refined_cellsize", 1.5}, {"order", 2}, {"margin", 20.0},
        {"boundary", QJsonArray{"ABC", "ABC", "ABC", "ABC", "ABC", "PEC"}},
        {"model_basename", "line_em"}, {"sim_path", "/somewhere"}, {"fdump_enabled", false}};
    return QJsonObject{{"application", "setupEM"}, {"data_format", "1.0"}, {"saved_values", values},
                       {"ports", QJsonArray{viaPort(1, 201, "Metal1", "TopMetal2", "-Z"),
                                            viaPort(2, 202, "Metal1", "TopMetal2", "Z")}}};
}

void writeJson(const QString &path, const QJsonObject &o)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(o).toJson());
}

QString setting(const SimcfgImport::Data &d, const QString &key)
{
    for (const auto &kv : d.settings)
        if (kv.first == key)
            return kv.second;
    return QStringLiteral("<missing>");
}

/*! Does the script set settings['key'] to value (the template aligns the "=" with spaces)? */
bool hasSetting(const QString &script, const QString &key, const QString &value)
{
    const QRegularExpression re(QStringLiteral("settings\\['%1'\\]\\s*=\\s*%2(\\s|$)")
                                    .arg(QRegularExpression::escape(key), QRegularExpression::escape(value)));
    return re.match(script).hasMatch();
}

/*! A MainWindow with Palace selectable (stub launcher), as the Palace golden test sets it up. */
void preparePalace(MainWindow &w)
{
#ifdef Q_OS_WIN
    const QString launcher = QFINDTESTDATA("tools/palace_launcher_stub.cmd");
#else
    const QString launcher = QFINDTESTDATA("tools/palace_launcher_stub.sh");
    QFile::setPermissions(launcher, QFile::permissions(launcher) | QFileDevice::ExeUser);
#endif
    w.testSetPreference("PALACE_RUN_MODE", 1);
    w.testSetPreference("PALACE_RUN_SCRIPT", launcher);
    w.testSetPreference("PALACE_INSTALL_PATH", QString());
    w.refreshSimToolOptionsForTests();
}

/*! A MainWindow with OpenEMS selectable too (stub Python), as the OpenEMS golden test sets it up. */
void prepareOpenems(MainWindow &w)
{
#ifdef Q_OS_WIN
    const QString py = QFINDTESTDATA("tools/openems_python_stub.cmd");
#else
    const QString py = QFINDTESTDATA("tools/openems_python_stub.sh");
    QFile::setPermissions(py, QFile::permissions(py) | QFileDevice::ExeUser);
#endif
    w.testSetPreference("Python Path", py);
    w.refreshSimToolOptionsForTests();
}

/*! The golden GDS and stackup copied into dir: the .simcfg and the model are written next to them. */
void copyLineInputs(const QTemporaryDir &dir, QString *gds, QString *xml)
{
    *gds = dir.filePath("line_simple_viaport.gds");
    *xml = dir.filePath("SG13G2_200um.xml");
    QVERIFY(QFile::copy(QFINDTESTDATA("golden/line_simple_viaport.gds"), *gds));
    QVERIFY(QFile::copy(QFINDTESTDATA("golden/SG13G2_200um.xml"), *xml));
}

} // namespace

/*!*******************************************************************************************************************
 * \brief GHz → Hz for the frequency keys, setupEM's GUI keys and the file inputs left out, ports read as written.
 **********************************************************************************************************************/
void SimcfgImportTest::read_convertsFrequenciesAndSkipsGuiKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("line_em.simcfg");
    QJsonObject o = lineSimcfg("C:\\work\\line.gds", "/opt/SG13G2_200um.xml");
    QJsonArray ports = o["ports"].toArray();
    ports.append(QJsonObject{{"portnumber", 3}, {"source_layernum", 203}, {"target_layername", "Metal2"},
                             {"from_layername", QJsonValue()}, {"to_layername", QJsonValue()},
                             {"direction", "-X"}, {"port_Z0", 25.0}, {"voltage", 0.0}});
    o["ports"] = ports;
    writeJson(path, o);

    const SimcfgImport::Data d = SimcfgImport::read(path);
    QVERIFY2(d.ok, qPrintable(d.error));
    QCOMPARE(d.gdsFile, QStringLiteral("C:\\work\\line.gds"));
    QCOMPARE(d.substrateFile, QStringLiteral("/opt/SG13G2_200um.xml"));
    QCOMPARE(d.cellname, QStringLiteral("t1"));
    QCOMPARE(setting(d, "fstart"), QStringLiteral("0e9"));
    QCOMPARE(setting(d, "fstop"), QStringLiteral("50e9"));
    QCOMPARE(setting(d, "fstep"), QStringLiteral("1e9"));
    QCOMPARE(setting(d, "fpoint"), QStringLiteral("[10e9, 20.5e9]"));
    QCOMPARE(setting(d, "refined_cellsize"), QStringLiteral("1.5"));
    QCOMPARE(setting(d, "unit"), QStringLiteral("1e-06"));
    QCOMPARE(setting(d, "purpose"), QStringLiteral("[0]"));
    QCOMPARE(setting(d, "boundary"), QStringLiteral("['ABC', 'ABC', 'ABC', 'ABC', 'ABC', 'PEC']"));
    for (const QString &key : {"GdsFile", "SubstrateFile", "cellname", "model_basename", "sim_path", "fdump_enabled"})
        QCOMPARE(setting(d, key), QStringLiteral("<missing>"));

    QCOMPARE(d.ports.size(), 3);
    QCOMPARE(d.ports[0].direction, QStringLiteral("-Z"));
    QCOMPARE(d.ports[0].fromLayer, QStringLiteral("Metal1"));
    QCOMPARE(d.ports[0].toLayer, QStringLiteral("TopMetal2"));
    QCOMPARE(d.ports[2].targetLayer, QStringLiteral("Metal2"));
    QCOMPARE(d.ports[2].sourceLayer, 203);
    QCOMPARE(d.ports[2].z0, 25.0);
    QCOMPARE(d.ports[2].voltage, 0.0);
}

/*!*******************************************************************************************************************
 * \brief Files that are not setupEM settings are refused with a reason.
 **********************************************************************************************************************/
void SimcfgImportTest::read_rejectsOtherFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString other = dir.filePath("other.simcfg");
    writeJson(other, QJsonObject{{"application", "setupThermal"}, {"saved_values", QJsonObject{}}});
    SimcfgImport::Data d = SimcfgImport::read(other);
    QVERIFY(!d.ok);
    QVERIFY(d.error.contains("setupEM"));

    const QString broken = dir.filePath("broken.simcfg");
    QFile f(broken);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{ not json");
    f.close();
    d = SimcfgImport::read(broken);
    QVERIFY(!d.ok);

    d = SimcfgImport::read(dir.filePath("missing.simcfg"));
    QVERIFY(!d.ok);
}

/*!*******************************************************************************************************************
 * \brief Without <name>.py: a new Palace model from the template, with files, cell, ports (polarity kept) and the
 *        converted settings, path set to <name>.py next to the .simcfg. Compared with a golden script.
 **********************************************************************************************************************/
void SimcfgImportTest::importNewModel_matchesGolden()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString gds, xml;
    copyLineInputs(dir, &gds, &xml);
    const QString simcfg = dir.filePath("line_em.simcfg");
    writeJson(simcfg, lineSimcfg(gds, xml));

    MainWindow w;
    preparePalace(w);
    QVERIFY(w.importSimcfg(simcfg));

    const QString script = w.testEditorText();
    QVERIFY2(hasSetting(script, "fstop", "50e9"), qPrintable(script));
    QVERIFY(hasSetting(script, "refined_cellsize", "1.5"));
    QVERIFY(hasSetting(script, "order", "2"));
    QVERIFY(script.contains("'PEC'"));
    QVERIFY(!script.contains("fdump_enabled"));

    const auto ports = w.testParsePortsFromEditor();
    QCOMPARE(ports.size(), 2);
    QCOMPARE(ports[0].direction, QStringLiteral("-z"));
    QCOMPARE(ports[1].direction, QStringLiteral("z"));
    QCOMPARE(ports[0].sourceLayer, QStringLiteral("201"));

    // a later import of the same .simcfg must find the model: its path is <name>.py next to it
    const QString modelPath = MainWindow::simcfgModelPath(simcfg);
    QCOMPARE(QFileInfo(modelPath).fileName(), QStringLiteral("line_em.py"));

    const QString goldenPath = QFINDTESTDATA("golden/tst_palace_from_simcfg.py");
    QVERIFY2(!goldenPath.isEmpty(), "golden/tst_palace_from_simcfg.py not found");
    // updateGoldenOnce(goldenPath, normalize(script));
    const QString expected = normalize(readUtf8(goldenPath));
    const QString actual = normalize(script);
    if (expected != actual)
        QFAIL(qPrintable(QString("Mismatch vs golden:\n\n%1").arg(diffText(expected, actual))));
}

/*!*******************************************************************************************************************
 * \brief With <name>.py: the re-export updates ports, layout file and purpose silently and keeps the settings the
 *        user edited in EMStudio (here refined_cellsize), even when the .simcfg carries other values.
 **********************************************************************************************************************/
void SimcfgImportTest::importExistingModel_updatesOnlyLayoutAndPorts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString gds, xml;
    copyLineInputs(dir, &gds, &xml);
    const QString simcfg = dir.filePath("line_em.simcfg");
    writeJson(simcfg, lineSimcfg(gds, xml));
    const QString modelPath = MainWindow::simcfgModelPath(simcfg);

    {
        MainWindow w;
        preparePalace(w);
        QVERIFY(w.importSimcfg(simcfg));
        QVERIFY(w.saveModelAs(modelPath));
    }
    QVERIFY(QFileInfo::exists(modelPath));

    // the user tunes the mesh in EMStudio
    QString model = readUtf8(modelPath);
    QVERIFY(model.contains("settings['refined_cellsize'] = 1.5"));
    model.replace("settings['refined_cellsize'] = 1.5", "settings['refined_cellsize'] = 3  # tuned");
    {
        QFile f(modelPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(model.toUtf8());
    }

    // re-export from KLayout: port 1 reversed, port 2 now in-plane, another purpose, other setupEM settings
    QJsonObject o = lineSimcfg(gds, xml);
    QJsonObject values = o["saved_values"].toObject();
    values["purpose"] = QJsonArray{0, 5};
    values["refined_cellsize"] = 9.0;
    values["fstop"] = 99.0;
    o["saved_values"] = values;
    o["ports"] = QJsonArray{viaPort(1, 201, "Metal1", "TopMetal2", "Z"),
                            QJsonObject{{"portnumber", 2}, {"source_layernum", 202}, {"target_layername", "TopMetal2"},
                                        {"from_layername", QJsonValue()}, {"to_layername", QJsonValue()},
                                        {"direction", "-X"}, {"port_Z0", 50.0}, {"voltage", 1.0}}};
    writeJson(simcfg, o);

    MainWindow w;
    preparePalace(w);
    QVERIFY(w.importSimcfg(simcfg));
    const QString script = w.testEditorText();
    QVERIFY2(hasSetting(script, "refined_cellsize", "3  # tuned"), qPrintable(script));
    QVERIFY(hasSetting(script, "fstop", "50e9"));
    QVERIFY(hasSetting(script, "purpose", "[0, 5]"));
    const auto ports = w.testParsePortsFromEditor();
    QCOMPARE(ports.size(), 2);
    QCOMPARE(ports[0].direction, QStringLiteral("z"));
    // in-plane: gds2palace needs target_layername (to_layername alone is refused)
    QVERIFY2(script.contains("source_layernum=202, target_layername='TopMetal2', direction='-x'"), qPrintable(script));

    QVERIFY(w.saveModelAs(modelPath));
    const QString saved = readUtf8(modelPath);
    QVERIFY(hasSetting(saved, "refined_cellsize", "3  # tuned"));
    QVERIFY(hasSetting(saved, "purpose", "[0, 5]"));
}

/*!*******************************************************************************************************************
 * \brief A new OpenEMS model gets only what means the same in gds2openEMS; setupEM's fstep becomes numfreq.
 **********************************************************************************************************************/
void SimcfgImportTest::settingsFor_openemsTakesSharedKeysOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("line_em.simcfg");
    writeJson(path, lineSimcfg("/w/line.gds", "/w/SG13G2_200um.xml"));
    const SimcfgImport::Data d = SimcfgImport::read(path);
    QVERIFY2(d.ok, qPrintable(d.error));

    QStringList skipped;
    const auto s = SimcfgImport::settingsFor(d, "openems", &skipped);
    QStringList keys;
    for (const auto &kv : s)
        keys << kv.first;
    keys.sort();
    QCOMPARE(keys, QStringList({"fstart", "fstop", "numfreq", "purpose", "unit"}));
    for (const auto &kv : s) {
        if (kv.first == "fstop")
            QCOMPARE(kv.second, QStringLiteral("50e9"));
        if (kv.first == "numfreq")
            QCOMPARE(kv.second, QStringLiteral("51"));     // 0..50 GHz in 1 GHz steps
    }
    for (const QString &key : {"refined_cellsize", "order", "margin", "boundary", "fpoint"})
        QVERIFY2(skipped.contains(key), qPrintable(key));
    QVERIFY(!skipped.contains("fstep"));                    // used for numfreq, not reported as left out

    QCOMPARE(SimcfgImport::settingsFor(d, "palace").size(), d.settings.size());
}

/*!*******************************************************************************************************************
 * \brief A new OpenEMS model (tool from the question's answer): files, cell, ports with their polarity, the shared
 *        settings; the template's mesh and boundaries stay. Compared with a golden script.
 **********************************************************************************************************************/
void SimcfgImportTest::importNewOpenemsModel_matchesGolden()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString gds, xml;
    copyLineInputs(dir, &gds, &xml);
    const QString simcfg = dir.filePath("line_em.simcfg");
    writeJson(simcfg, lineSimcfg(gds, xml));

    MainWindow w;
    preparePalace(w);
    prepareOpenems(w);
    w.testSetSimcfgImportTool("openems");                   // what the user answers when asked
    QVERIFY(w.importSimcfg(simcfg));

    const QString script = w.testEditorText();
    QVERIFY2(script.contains("from gds2openEMS import") || script.contains("openEMS"), qPrintable(script));
    QVERIFY(hasSetting(script, "fstop", "50e9"));
    QVERIFY(hasSetting(script, "numfreq", "51"));
    QVERIFY(!hasSetting(script, "refined_cellsize", "1.5"));   // the FEM mesh size is not taken
    QVERIFY(!script.contains("'order'"));
    const auto ports = w.testParsePortsFromEditor();
    QCOMPARE(ports.size(), 2);
    QCOMPARE(ports[0].direction, QStringLiteral("-z"));
    QCOMPARE(ports[1].direction, QStringLiteral("z"));

    const QString goldenPath = QFINDTESTDATA("golden/tst_openems_from_simcfg.py");
    QVERIFY2(!goldenPath.isEmpty(), "golden/tst_openems_from_simcfg.py not found");
    // updateGoldenOnce(goldenPath, normalize(script));
    const QString expected = normalize(readUtf8(goldenPath));
    const QString actual = normalize(script);
    if (expected != actual)
        QFAIL(qPrintable(QString("Mismatch vs golden:\n\n%1").arg(diffText(expected, actual))));
}

