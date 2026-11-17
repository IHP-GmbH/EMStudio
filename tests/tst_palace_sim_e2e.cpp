/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_palace_sim_e2e.h"

#include <QtTest/QtTest>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>

#include "mainwindow.h"
#include "resultsviewer.h"
#include "touchstone.h"

namespace {

bool e2eEnabled()
{
    return qgetenv("EMSTUDIO_PALACE_E2E") == "1";
}

QString palaceInstallPath()
{
    const QByteArray env = qgetenv("PALACE_INSTALL_PATH");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env).trimmed();
    return QString();
}

QString palaceBinary(const QString &root)
{
    return QDir(root).filePath(QStringLiteral("bin/palace"));
}

QString findProducedTouchstone(const QString &runDir)
{
    if (runDir.isEmpty())
        return QString();
    QDirIterator it(runDir, {QStringLiteral("*.s1p"), QStringLiteral("*.s2p"), QStringLiteral("*.s3p"),
                             QStringLiteral("*.s4p"), QStringLiteral("*.snp"), QStringLiteral("*.S2P")},
                    QDir::Files, QDirIterator::Subdirectories);
    QString best;
    while (it.hasNext()) {
        const QString p = it.next();
        const QString name = QFileInfo(p).fileName().toLower();
        if (name.contains(QStringLiteral("deembed")) || name.contains(QStringLiteral("_dc")))
            continue;
        best = p;
        if (name.endsWith(QStringLiteral(".s2p")))
            return p;
    }
    return best;
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Full-flow smoke: line_viaPort rectangles through EMStudio → gds2palace → Palace → Touchstone.
 **********************************************************************************************************************/
void PalaceSimE2E::lineViaPort_runsPalace_andMatchesGoldenSparams()
{
#ifndef Q_OS_LINUX
    QSKIP("Palace E2E uses a native Linux Palace binary (see tests/golden/palace_e2e/README.md).");
#endif

    if (!e2eEnabled())
        QSKIP("Set EMSTUDIO_PALACE_E2E=1 to run the Palace simulation E2E test.");

    const QString dockerLauncher = QFINDTESTDATA("tools/palace_docker_launcher.sh");
    const QString root = palaceInstallPath();
    const QString exe = palaceBinary(root);
    const bool useDocker = QFileInfo::exists(dockerLauncher)
            && (qgetenv("EMSTUDIO_PALACE_DOCKER") == "1" || !QFileInfo::exists(exe));
    if (!useDocker && (root.isEmpty() || !QFileInfo::exists(exe))) {
        QSKIP(qPrintable(QStringLiteral(
                "Need Docker launcher or PALACE_INSTALL_PATH/bin/palace (%1).").arg(exe)));
    }

    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    // Thin stackup (no bulk Si): full SG13G2_200um stalls gmsh Fragments on CI.
    const QString xmlPath = QFINDTESTDATA("golden/palace_e2e/SG13G2_smoke.xml");
    QVERIFY2(!gdsPath.isEmpty() && !xmlPath.isEmpty(), "golden GDS / palace_e2e smoke XML missing");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QFile::copy(gdsPath, dir.filePath(QStringLiteral("line_simple_viaport.gds"))));
    QVERIFY(QFile::copy(xmlPath, dir.filePath(QStringLiteral("SG13G2_smoke.xml"))));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 800);
    w.show();
    QTest::qWait(30);

    w.setGdsFile(dir.filePath(QStringLiteral("line_simple_viaport.gds")));
    w.setTopCell(QStringLiteral("t1"));
    w.setSubstrateFile(dir.filePath(QStringLiteral("SG13G2_smoke.xml")));

    // Prefer Docker launcher on CI (MPI stack matches the image). Else native install.
    if (useDocker) {
        QFile::setPermissions(dockerLauncher,
                              QFile::permissions(dockerLauncher) | QFileDevice::ExeUser
                                      | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        w.testSetPreference(QStringLiteral("PALACE_RUN_MODE"), 1);
        w.testSetPreference(QStringLiteral("PALACE_RUN_SCRIPT"), dockerLauncher);
        w.testSetPreference(QStringLiteral("PALACE_INSTALL_PATH"), QString());
    } else {
        w.testSetPreference(QStringLiteral("PALACE_RUN_MODE"), 0);
        w.testSetPreference(QStringLiteral("PALACE_INSTALL_PATH"), root);
        w.testSetPreference(QStringLiteral("PALACE_RUN_SCRIPT"), QString());
    }
    const QByteArray py = qgetenv("EMSTUDIO_TEST_PYTHON");
    if (!py.isEmpty())
        w.testSetPreference(QStringLiteral("PALACE_PYTHON"), QString::fromLocal8Bit(py));
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey(QStringLiteral("palace"), &terr), qPrintable(terr));
    QVERIFY2(w.testInitDefaultPalaceModel(), "default Palace model failed");

    // Via ports need from/to metal *names* in the script (gds2palace z-port rules).
    auto *names = w.findChild<QCheckBox *>(QStringLiteral("cbSubLayerNames"));
    QVERIFY(names);
    QVERIFY2(names->isEnabled(), "stackup name map missing for Metal1/TopMetal2");
    names->setChecked(true);

    // Two via-port rectangles on golden GDS layers 201 / 202 (examples/palace/line_viaport).
    auto *tblPorts = w.findChild<QTableWidget *>(QStringLiteral("tblPorts"));
    QVERIFY(tblPorts);
    for (const char *layer : {"201", "202"}) {
        w.testClickAddPort();
        const int row = tblPorts->rowCount() - 1;
        QVERIFY(row >= 0);
        auto *src = qobject_cast<QComboBox *>(tblPorts->cellWidget(row, 3));
        auto *from = qobject_cast<QComboBox *>(tblPorts->cellWidget(row, 4));
        auto *to = qobject_cast<QComboBox *>(tblPorts->cellWidget(row, 5));
        auto *dir = qobject_cast<QComboBox *>(tblPorts->cellWidget(row, 6));
        QVERIFY(src && from && to && dir);
        src->setCurrentText(QString::fromLatin1(layer));
        from->setCurrentText(QStringLiteral("Metal1"));
        to->setCurrentText(QStringLiteral("TopMetal2"));
        dir->setCurrentText(QStringLiteral("z"));
        QCOMPARE(from->currentText(), QStringLiteral("Metal1"));
        QCOMPARE(to->currentText(), QStringLiteral("TopMetal2"));
    }

    // Smoke: line + two via-port rectangles — very coarse mesh so CI finishes in a few minutes.
    w.testSetSimSetting(QStringLiteral("fstart"), 1e9);
    w.testSetSimSetting(QStringLiteral("fstop"), 1e9);
    w.testSetSimSetting(QStringLiteral("fstep"), 1e9);
    w.testSetSimSetting(QStringLiteral("margin"), 5);
    w.testSetSimSetting(QStringLiteral("refined_cellsize"), 50);
    w.testSetSimSetting(QStringLiteral("cells_per_wavelength"), 10);
    w.testSetSimSetting(QStringLiteral("meshsize_max"), 500);
    w.testSetSimSetting(QStringLiteral("adaptive_mesh_iterations"), 0);
    w.testSetSimSetting(QStringLiteral("no_gui"), true);

    QString genErr;
    const QString script = w.testGenerateScriptFromGuiState(&genErr);
    QVERIFY2(!script.isEmpty(), qPrintable(genErr));
    QVERIFY2(script.contains(QStringLiteral("from_layername='Metal1'"))
                 && script.contains(QStringLiteral("to_layername='TopMetal2'"))
                 && script.contains(QStringLiteral("direction='z'")),
             qPrintable(QStringLiteral("via-port lines missing in generated script:\n%1").arg(script)));
    w.testSetEditorText(script);

    const QString scriptPath = dir.filePath(QStringLiteral("palace_e2e_line.py"));
    {
        QFile f(scriptPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(script.toUtf8());
    }
    w.testSetRunPythonScriptPath(scriptPath);
    w.testSetSimSetting(QStringLiteral("RunDir"), dir.path());

    w.testRunPalace(false);

    auto dumpLogs = [&w]() {
        return QStringLiteral("sim log:\n%1\nmain log:\n%2")
                .arg(w.testSimulationLogText(), w.testMainLogText());
    };

    // Nightly sets EMSTUDIO_PALACE_E2E_TIMEOUT_MS (default 25 min) below QTEST_FUNCTION_TIMEOUT.
    int waitMs = 25 * 60 * 1000;
    {
        bool ok = false;
        const int fromEnv = QString::fromLocal8Bit(qgetenv("EMSTUDIO_PALACE_E2E_TIMEOUT_MS")).toInt(&ok);
        if (ok && fromEnv > 0)
            waitMs = fromEnv;
    }
    const bool finished = QTest::qWaitFor([&w]() { return !w.testIsSimulationRunning(); }, waitMs);
    QVERIFY2(finished, qPrintable(QStringLiteral("Palace E2E timed out.\n%1").arg(dumpLogs())));

    const QString log = w.testSimulationLogText();
    QVERIFY2(!log.trimmed().isEmpty(),
             qPrintable(QStringLiteral("Simulation never started.\n%1").arg(dumpLogs())));
    QVERIFY2(log.contains(QStringLiteral("Palace solver finished with exit code 0"))
                 || log.contains(QStringLiteral("Palace launcher finished with exit code 0"))
                 || log.contains(QStringLiteral("finished with exit code 0")),
             qPrintable(QStringLiteral("Solver did not finish OK:\n%1").arg(dumpLogs())));

    QString runDir;
    {
        const QRegularExpression re(QStringLiteral(R"(Simulation data directory:\s*(\S+))"));
        const auto m = re.match(log);
        if (m.hasMatch())
            runDir = m.captured(1).trimmed();
    }
    if (runDir.isEmpty())
        runDir = dir.filePath(QStringLiteral("palace_model/palace_e2e_line_data"));

    // testRunPalace(false) leaves m_headless unset so CSV→Touchstone may already have run;
    // fall back to ResultsViewer if not.
    QString snp = findProducedTouchstone(runDir);
    if (snp.isEmpty()) {
        auto *rv = w.findChild<ResultsViewer *>();
        QVERIFY2(rv, "ResultsViewer missing");
        rv->setTargetDirectory(runDir);
        QString convertLog;
        QVERIFY2(rv->tryConvertPalaceCsv(&convertLog),
                 qPrintable(QStringLiteral("CSV→Touchstone failed: %1").arg(convertLog)));
        snp = findProducedTouchstone(runDir);
    }
    QVERIFY2(!snp.isEmpty(), qPrintable(QStringLiteral("No Touchstone under %1\n%2").arg(runDir, dumpLogs())));

    // Full-flow smoke: valid 2-port Touchstone at the single sweep point (no golden mag match —
    // mesh is intentionally too coarse for that).
    TouchstoneNetwork got;
    QString loadErr;
    QVERIFY2(got.load(snp, &loadErr), qPrintable(loadErr));
    QCOMPARE(got.nports(), 2);
    QVERIFY2(got.frequencyCount() >= 1,
             qPrintable(QStringLiteral("expected ≥1 frequency in %1").arg(snp)));
}
