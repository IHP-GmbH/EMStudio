/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_tips.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "mainwindow.h"
#include "pythonparser.h"
#include "addsettingdialog.h"
#include "test_utils.h"


void TipsTest::resolveKeywordsPath_mapsElmerToPalace()
{
    MainWindow w;
    KeywordFileBackup em(QStringLiteral("elmer_em.csv"));
    KeywordFileBackup th(QStringLiteral("elmer_thermal.csv"));
    QVERIFY(QDir().mkpath(QFileInfo(em.path()).absolutePath()));
    for (const QString &p : {em.path(), th.path()}) {
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    }

    // Elmer tools have their own files ...
    const QString palace = w.testResolveKeywordsPath(QStringLiteral("palace"));
    QVERIFY(palace.endsWith(QStringLiteral("keywords/palace.csv"))
            || palace.endsWith(QStringLiteral("keywords\\palace.csv")));
    QVERIFY(w.testResolveKeywordsPath(QStringLiteral("elmer")).endsWith(QStringLiteral("elmer_em.csv")));
    QVERIFY(w.testResolveKeywordsPath(QStringLiteral("elmer_em")).endsWith(QStringLiteral("elmer_em.csv")));
    QVERIFY(w.testResolveKeywordsPath(QStringLiteral("elmer_thermal")).endsWith(QStringLiteral("elmer_thermal.csv")));

    // ... and fall back to palace.csv in an older keywords folder.
    QFile::remove(em.path());
    QFile::remove(th.path());
    QCOMPARE(w.testResolveKeywordsPath(QStringLiteral("elmer_em")), palace);
    QCOMPARE(w.testResolveKeywordsPath(QStringLiteral("elmer_thermal")), palace);

    const QString openems = w.testResolveKeywordsPath(QStringLiteral("openems"));
    QVERIFY(openems.contains(QStringLiteral("openems")));
}

/*! Topic and default columns: the tooltip shows the description and the default, not the topic. */
void TipsTest::loadKeywordTable_readsTopicsAndDefaults()
{
    MainWindow w;
    KeywordFileBackup palace(QStringLiteral("palace.csv"));
    {
        QFile f(palace.path());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        f.write("unit\tUnit of values\tInput files\t1e-6\tyes\n"
                "fstart\tStart frequency\tFrequencies\t\tyes\n"
                "order\tFEM order\tMesh size and accuracy\t2\n"
                "legacy\tOld line\n");
    }
    const QVector<MainWindow::KeywordEntry> table = w.testLoadKeywordTable(QStringLiteral("palace"));
    QCOMPARE(table.size(), 4);
    QCOMPARE(table.at(0).topic, QStringLiteral("Input files"));
    QCOMPARE(table.at(0).defaultValue, QStringLiteral("1e-6"));
    QVERIFY(table.at(0).required);
    QVERIFY(table.at(1).required);
    QVERIFY(!table.at(2).required);
    QCOMPARE(table.at(2).topic, QStringLiteral("Mesh size and accuracy"));
    QCOMPARE(table.at(3).topic, QString());

    const QMap<QString, QString> tips = w.testLoadKeywordTipsCsv(QStringLiteral("palace"));
    QCOMPARE(tips.value(QStringLiteral("unit")), QStringLiteral("Required. Unit of values\nDefault: 1e-6"));
    QCOMPARE(tips.value(QStringLiteral("fstart")), QStringLiteral("Required. Start frequency"));
    QCOMPARE(tips.value(QStringLiteral("order")), QStringLiteral("FEM order\nDefault: 2"));
    QVERIFY(!tips.value(QStringLiteral("order")).contains(QStringLiteral("Mesh")));
}

void TipsTest::loadKeywordTipsCsv_parsesDelimiters()
{
    MainWindow w;
    KeywordFileBackup palaceBackup(QStringLiteral("palace.csv"));
    KeywordFileBackup openemsBackup(QStringLiteral("openems.csv"));
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString kwDir = QDir(appDir).filePath(QStringLiteral("keywords"));
    QVERIFY(QDir().mkpath(kwDir));

    const QString csvPath = QDir(kwDir).filePath(QStringLiteral("palace.csv"));
    {
        QFile f(csvPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QTextStream out(&f);
        out << "freq_start;Start frequency\n"
            << "freq_stop;Stop frequency\n"
            << "orphan_keyword\n"
            << "\n"
            << "freq_start;duplicate ignored\n";
    }

    const QMap<QString, QString> tips = w.testLoadKeywordTipsCsv(QStringLiteral("palace"));
    QCOMPARE(tips.value(QStringLiteral("freq_start")), QStringLiteral("Start frequency"));
    QCOMPARE(tips.value(QStringLiteral("freq_stop")), QStringLiteral("Stop frequency"));
    QVERIFY(tips.contains(QStringLiteral("orphan_keyword")));

    // Also cover comma-delimited openems file
    const QString oePath = QDir(kwDir).filePath(QStringLiteral("openems.csv"));
    {
        QFile f(oePath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QTextStream(&f) << "unit,Length unit\nmax_freq,Max freq\n";
    }
    const QMap<QString, QString> oe = w.testLoadKeywordTipsCsv(QStringLiteral("openems"));
    QCOMPARE(oe.value(QStringLiteral("unit")), QStringLiteral("Length unit"));

    // Tab-delimited tips
    {
        QFile f(QDir(kwDir).filePath(QStringLiteral("palace.csv")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QTextStream(&f) << "alpha\tAlpha tip\nbeta\tBeta tip\n";
    }
    const QMap<QString, QString> tabTips = w.testLoadKeywordTipsCsv(QStringLiteral("palace"));
    QCOMPARE(tabTips.value(QStringLiteral("alpha")), QStringLiteral("Alpha tip"));

    w.testRefreshKeywordTipsForCurrentTool();
}

void TipsTest::mergeTipsPreferModel_keepsModelOverrides()
{
    MainWindow w;
    QMap<QString, QString> model;
    model.insert(QStringLiteral("a"), QStringLiteral("from-model"));
    QMap<QString, QString> fallback;
    fallback.insert(QStringLiteral("a"), QStringLiteral("from-csv"));
    fallback.insert(QStringLiteral("b"), QStringLiteral("only-csv"));

    const QMap<QString, QString> merged = w.testMergeTipsPreferModel(model, fallback);
    QCOMPARE(merged.value(QStringLiteral("a")), QStringLiteral("from-model"));
    QCOMPARE(merged.value(QStringLiteral("b")), QStringLiteral("only-csv"));
}

/*! Loose variables get the keyword of the workflow parameter they are passed to, but only when the
 *  binding is safe: one top-level literal assignment, no other binding of the name. */
void TipsTest::bindWorkflowCalls_mapsLooseVariables()
{
    QVector<PythonParser::WorkflowParam> sig;
    auto add = [&](const char *f, int i, const char *p, const char *k) {
        PythonParser::WorkflowParam w;
        w.function = QString::fromLatin1(f);
        w.index = i;
        w.param = QString::fromLatin1(p);
        w.keyword = QString::fromLatin1(k);
        sig << w;
    };
    add("setupSimulation", 7, "max_cellsize", "max_cellsize");
    add("setupSimulation", 8, "refined_cellsize", "refined_cellsize");
    add("setupSimulation", 9, "margin", "margin");
    add("setupSimulation", 10, "unit", "unit");
    add("runSimulation", 4, "preview_only", "preview_only");
    add("read_gds", 4, "preprocess", "preprocess_gds");

    const QString base = QStringLiteral(
        "cs_fine = 1  # mesh at edges\n"
        "my_margin = 50\n"
        "unit = 1e-6\n"
        "max_cs = (3e8/fstop)/20\n"
        "pv = True\n"
        "prep = False\n"
        "# FDTD = simulation_setup.setupSimulation(a, b, c, d, e, f, g, commented, x, y, z)\n"
        "FDTD = simulation_setup.setupSimulation (excite_ports, simulation_ports, FDTD,\n"
        "                                         materials_list, dielectrics_list, metals_list, allpolygons,\n"
        "                                         max_cs, cs_fine, my_margin, unit,\n"
        "                                         xy_mesh_function=util_meshlines.create_xy_mesh_from_polygons)\n"
        "sub = simulation_setup.runSimulation (excite_ports, FDTD, sim_path, model_basename, pv)\n"
        "allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0], preprocess=prep)\n");
    QHash<QString, QString> alias = PythonParser::bindWorkflowCalls(base, sig);
    QCOMPARE(alias.value(QStringLiteral("cs_fine")), QStringLiteral("refined_cellsize"));   // positional
    QCOMPARE(alias.value(QStringLiteral("my_margin")), QStringLiteral("margin"));
    QCOMPARE(alias.value(QStringLiteral("pv")), QStringLiteral("preview_only"));
    QCOMPARE(alias.value(QStringLiteral("prep")), QStringLiteral("preprocess_gds"));        // keyword
    QVERIFY(!alias.contains(QStringLiteral("max_cs")));   // expression
    QVERIFY(!alias.contains(QStringLiteral("unit")));     // same name: no alias needed

    // Unsafe bindings are dropped.
    auto rejected = [&](const QString &extra, const char *name) {
        return !PythonParser::bindWorkflowCalls(base + extra, sig).contains(QString::fromLatin1(name));
    };
    QVERIFY(rejected(QStringLiteral("cs_fine = 2\n"), "cs_fine"));                 // reassigned
    QVERIFY(rejected(QStringLiteral("if fine:\n    my_margin = 20\n"), "my_margin"));  // in a block
    QVERIFY(rejected(QStringLiteral("a, pv = 1, False\n"), "pv"));                 // tuple target
    QVERIFY(rejected(QStringLiteral("for prep in range(2):\n    pass\n"), "prep"));   // loop variable
    QVERIFY(rejected(QStringLiteral("cs_fine += 1\n"), "cs_fine"));                // augmented
    QVERIFY(rejected(QStringLiteral("x = simulation_setup.runSimulation(e, F, s, m, my_margin)\n"),
                     "my_margin"));  // passed as two different parameters
}

/*! statementEnd spans brackets and continuations; the Add dialog turns typed text into literals. */
void TipsTest::statementEndAndValueLiterals()
{
    const QString s = QStringLiteral("a = [1,\n     2]  # c\nb = 1 + \\\n    2\nc = 'x#y'\n");
    QCOMPARE(PythonParser::statementEnd(s, 0), s.indexOf(QStringLiteral("b =")));
    QCOMPARE(PythonParser::statementEnd(s, s.indexOf(QStringLiteral("b ="))), s.indexOf(QStringLiteral("c =")));
    QCOMPARE(PythonParser::statementEnd(s, s.indexOf(QStringLiteral("c ="))), s.size());

    QString lit;
    QVERIFY(AddSettingDialog::toPythonLiteral(QStringLiteral(" 1e-6 "), &lit));
    QCOMPARE(lit, QStringLiteral("1e-6"));
    QVERIFY(AddSettingDialog::toPythonLiteral(QStringLiteral("True"), &lit));
    QCOMPARE(lit, QStringLiteral("True"));
    QVERIFY(AddSettingDialog::toPythonLiteral(QStringLiteral("[['Metal3', 2.0]]"), &lit));
    QCOMPARE(lit, QStringLiteral("[['Metal3', 2.0]]"));
    QVERIFY(AddSettingDialog::toPythonLiteral(QStringLiteral("_fine"), &lit));
    QCOMPARE(lit, QStringLiteral("'_fine'"));
    QVERIFY(AddSettingDialog::toPythonLiteral(QStringLiteral("it's"), &lit));
    QCOMPARE(lit, QStringLiteral("'it\\'s'"));
    QVERIFY(!AddSettingDialog::toPythonLiteral(QStringLiteral("[1, 2"), &lit));
    QVERIFY(!AddSettingDialog::toPythonLiteral(QStringLiteral("  "), &lit));
}
