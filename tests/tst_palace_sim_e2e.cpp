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
#include <QtMath>
#include <cmath>

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

bool compareTouchstone(const TouchstoneNetwork &got, const TouchstoneNetwork &ref, QString *why)
{
    const double magTol = 0.05;      // |Δ|S||
    const double phaseTolDeg = 15.0; // unwrap-insensitive via complex distance
    const double freqTolHz = 1.0;

    if (got.nports() != ref.nports()) {
        *why = QStringLiteral("nports %1 vs %2").arg(got.nports()).arg(ref.nports());
        return false;
    }
    if (got.frequencyCount() != ref.frequencyCount()) {
        *why = QStringLiteral("nfreq %1 vs %2").arg(got.frequencyCount()).arg(ref.frequencyCount());
        return false;
    }
    for (int fi = 0; fi < ref.frequencyCount(); ++fi) {
        if (qAbs(got.frequencyHz().at(fi) - ref.frequencyHz().at(fi)) > freqTolHz) {
            *why = QStringLiteral("freq[%1] %2 vs %3")
                       .arg(fi)
                       .arg(got.frequencyHz().at(fi), 0, 'g', 12)
                       .arg(ref.frequencyHz().at(fi), 0, 'g', 12);
            return false;
        }
        for (int m = 0; m < ref.nports(); ++m) {
            for (int n = 0; n < ref.nports(); ++n) {
                const auto a = got.s(fi, m, n);
                const auto b = ref.s(fi, m, n);
                const double dMag = std::abs(std::abs(a) - std::abs(b));
                const double dComplex = std::abs(a - b);
                if (dMag > magTol || dComplex > magTol + qDegreesToRadians(phaseTolDeg)) {
                    *why = QStringLiteral("S%1%2 @f%3: got (%4,%5) ref (%6,%7) dMag=%8 dC=%9")
                               .arg(m + 1)
                               .arg(n + 1)
                               .arg(fi)
                               .arg(a.real())
                               .arg(a.imag())
                               .arg(b.real())
                               .arg(b.imag())
                               .arg(dMag)
                               .arg(dComplex);
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Runs the line_viaPort Palace stack and compares Touchstone to the checked-in golden.
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
    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!gdsPath.isEmpty() && !xmlPath.isEmpty(), "golden GDS/XML missing");

    const QString goldenPath = QFINDTESTDATA("golden/palace_e2e/line_viaport.s2p");
    QVERIFY2(!goldenPath.isEmpty(),
             "golden/palace_e2e/line_viaport.s2p missing — regenerate per README.md");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QFile::copy(gdsPath, dir.filePath(QStringLiteral("line_simple_viaport.gds"))));
    QVERIFY(QFile::copy(xmlPath, dir.filePath(QStringLiteral("SG13G2_200um.xml"))));

    MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    w.resize(1200, 800);
    w.show();
    QTest::qWait(30);

    w.setGdsFile(dir.filePath(QStringLiteral("line_simple_viaport.gds")));
    w.setTopCell(QStringLiteral("t1"));
    w.setSubstrateFile(dir.filePath(QStringLiteral("SG13G2_200um.xml")));

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

    // Two via ports on the golden GDS layers 201 / 202 (same as examples/palace/line_viaport).
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
    }

    // Smoke geometry (line + two via-port rectangles): one frequency, coarse mesh.
    // Whole E2E must finish within ~10 minutes on CI.
    w.testSetSimSetting(QStringLiteral("fstart"), 5e9);
    w.testSetSimSetting(QStringLiteral("fstop"), 5e9);
    w.testSetSimSetting(QStringLiteral("fstep"), 5e9);
    w.testSetSimSetting(QStringLiteral("margin"), 10);
    w.testSetSimSetting(QStringLiteral("refined_cellsize"), 20);
    w.testSetSimSetting(QStringLiteral("cells_per_wavelength"), 10);
    w.testSetSimSetting(QStringLiteral("meshsize_max"), 200);
    w.testSetSimSetting(QStringLiteral("adaptive_mesh_iterations"), 0);
    w.testSetSimSetting(QStringLiteral("no_gui"), true);

    QString genErr;
    const QString script = w.testGenerateScriptFromGuiState(&genErr);
    QVERIFY2(!script.isEmpty(), qPrintable(genErr));
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

    const bool finished = QTest::qWaitFor([&w]() { return !w.testIsSimulationRunning(); },
                                          10 * 60 * 1000);
    QVERIFY2(finished, qPrintable(QStringLiteral("Palace E2E timed out.\n%1").arg(w.testSimulationLogText())));

    const QString log = w.testSimulationLogText();
    QVERIFY2(log.contains(QStringLiteral("Palace solver finished with exit code 0"))
                 || log.contains(QStringLiteral("Palace launcher finished with exit code 0"))
                 || log.contains(QStringLiteral("finished with exit code 0")),
             qPrintable(QStringLiteral("Solver did not finish OK:\n%1").arg(log)));

    QString runDir;
    {
        const QRegularExpression re(QStringLiteral(R"(Simulation data directory:\s*(\S+))"));
        const auto m = re.match(log);
        if (m.hasMatch())
            runDir = m.captured(1).trimmed();
    }
    if (runDir.isEmpty())
        runDir = dir.filePath(QStringLiteral("palace_model/palace_e2e_line_data"));

    // Headless-ish path still has m_headless=false for testRunPalace, so CSV→Touchstone
    // should already have run; ensure a .s2p exists either way.
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
    QVERIFY2(!snp.isEmpty(), qPrintable(QStringLiteral("No Touchstone under %1\n%2").arg(runDir, log)));

    TouchstoneNetwork got;
    QString loadErr;
    QVERIFY2(got.load(snp, &loadErr), qPrintable(loadErr));
    TouchstoneNetwork ref;
    QVERIFY2(ref.load(goldenPath, &loadErr), qPrintable(loadErr));

    QString why;
    QVERIFY2(compareTouchstone(got, ref, &why),
             qPrintable(QStringLiteral("%1\nproduced: %2\ngolden: %3").arg(why, snp, goldenPath)));
}
