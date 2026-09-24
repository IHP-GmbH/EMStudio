/************************************************************************
 *  EMStudio – About Linux / WSL probe golden test.
 ************************************************************************/

#include "tst_about_dialog.h"

#include "test_utils.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLabel>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QVariant>

#include "about.h"

using namespace GoldenTestUtils;

/*!*******************************************************************************************************************
 * \brief Returns a QLabel child of the given widget by object name.
 **********************************************************************************************************************/
static QLabel *findLabel(QWidget *parent, const QString &name)
{
    return parent ? parent->findChild<QLabel *>(name) : nullptr;
}

/*!*******************************************************************************************************************
 * \brief Verifies whether a QLabel contains a valid pixmap without deprecated Qt API warnings.
 **********************************************************************************************************************/
static bool pixmapIsValid(const QLabel *label)
{
    if (!label)
        return false;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QPixmap pix = label->pixmap(Qt::ReturnByValue);
    return !pix.isNull();
#else
    const QPixmap *pix = label->pixmap();
    return pix && !pix->isNull();
#endif
}

/*!*******************************************************************************************************************
 * \brief Verifies that AboutDialog initializes version, Qt version, build text and logo.
 **********************************************************************************************************************/
void AboutDialogTest::initUi_setsExpectedLabels()
{
    QCoreApplication::setApplicationVersion(QStringLiteral(EMSTUDIO_VERSION_STR));

    const QMap<QString, QVariant> prefs;
    AboutDialog dlg(prefs);

    QLabel *lblVersion = findLabel(&dlg, "lblVersion");
    QLabel *lblQt = findLabel(&dlg, "lblQt");
    QLabel *lblBuild = findLabel(&dlg, "lblBuild");
    QLabel *lblLogo = findLabel(&dlg, "lblLogo");

    QVERIFY2(lblVersion, "lblVersion not found");
    QVERIFY2(lblQt, "lblQt not found");
    QVERIFY2(lblBuild, "lblBuild not found");
    QVERIFY2(lblLogo, "lblLogo not found");

    QCOMPARE(lblVersion->text(), QStringLiteral(EMSTUDIO_VERSION_STR));
    QCOMPARE(lblQt->text(), QString::fromLatin1(qVersion()));

#ifdef QT_DEBUG
    const QString expectedBuildType = QStringLiteral("Debug");
#else
    const QString expectedBuildType = QStringLiteral("Release");
#endif

    QVERIFY2(!lblBuild->text().isEmpty(), "lblBuild shall not be empty");
    QVERIFY2(lblBuild->text().contains(expectedBuildType),
             qPrintable(QString("lblBuild does not contain build type '%1': %2")
                            .arg(expectedBuildType, lblBuild->text())));
    QVERIFY2(lblBuild->text().contains(" | "),
             qPrintable(QString("lblBuild does not contain expected separator: %1")
                            .arg(lblBuild->text())));

    const QStringList buildParts = lblBuild->text().split(" | ");
    QVERIFY2(buildParts.size() == 2,
             qPrintable(QString("lblBuild has unexpected format: %1").arg(lblBuild->text())));
    QVERIFY2(!buildParts.at(1).trimmed().isEmpty(),
             qPrintable(QString("lblBuild date part is empty: %1").arg(lblBuild->text())));

    QVERIFY2(pixmapIsValid(lblLogo), "Logo label has no valid pixmap");
}

/*!*******************************************************************************************************************
 * \brief Extracts a named SECTION block from the About Linux probe golden file.
 **********************************************************************************************************************/
static QString goldenSection(const QString &all, const QString &name)
{
    const QString header = QStringLiteral("### SECTION %1\n").arg(name);
    const int start = all.indexOf(header);
    if (start < 0)
        return {};
    const int body = start + header.size();
    const int next = all.indexOf(QStringLiteral("### SECTION "), body);
    QString chunk = (next < 0) ? all.mid(body) : all.mid(body, next - body);
    return normalize(chunk);
}

/*!*******************************************************************************************************************
 * \brief Strips flaky "(latest …)" / "(up to date)" suffixes from About tool lines.
 **********************************************************************************************************************/
static QString stripLatestNoise(QString s)
{
    s.replace(QRegularExpression(QStringLiteral(" \\(latest [^\\)]+\\)")), QString());
    s.replace(QStringLiteral(" (up to date)"), QString());
    return s;
}

/*!*******************************************************************************************************************
 * \brief Golden: probeViaWsl contract (Windows marks Linux paths for WSL; Linux never does).
 **********************************************************************************************************************/
void AboutDialogTest::linuxProbe_viaWslContract_matchesGolden()
{
    const QString goldenPath = QFINDTESTDATA("golden/tst_about_linux_probe.txt");
    QVERIFY2(!goldenPath.isEmpty(), "golden/tst_about_linux_probe.txt missing");
    const QString all = readUtf8(goldenPath);
    QVERIFY2(!all.isEmpty(), "golden file empty");

#ifdef Q_OS_WIN
    const QString section = QStringLiteral("probeViaWsl_windows");
#else
    const QString section = QStringLiteral("probeViaWsl_linux");
#endif
    const QString expected = goldenSection(all, section);
    QVERIFY2(!expected.isEmpty(), qPrintable(QString("missing section %1").arg(section)));

    const QString actual = normalize(AboutDialog::testProbeViaWslReport());
    if (expected != actual) {
        QFAIL(qPrintable(QString("probeViaWsl mismatch:\n%1").arg(diffText(expected, actual))));
    }
}

/*!*******************************************************************************************************************
 * \brief Golden: tool plan with only Palace Python set to a Linux path (no OpenEMS/Palace/Elmer).
 *
 * On Windows this verifies Linux paths are queued with viaWsl=true (WSL theme).
 * On Linux the same prefs must use viaWsl=false (native probes).
 **********************************************************************************************************************/
void AboutDialogTest::linuxProbe_toolPlan_matchesGolden()
{
    const QString goldenPath = QFINDTESTDATA("golden/tst_about_linux_probe.txt");
    QVERIFY2(!goldenPath.isEmpty(), "golden/tst_about_linux_probe.txt missing");
    const QString all = readUtf8(goldenPath);

#ifdef Q_OS_WIN
    const QString section = QStringLiteral("tool_plan_windows");
#else
    const QString section = QStringLiteral("tool_plan_linux");
#endif
    const QString expected = goldenSection(all, section);
    QVERIFY2(!expected.isEmpty(), qPrintable(QString("missing section %1").arg(section)));

    QMap<QString, QVariant> prefs;
    // Sentinel Linux path — not executed on Windows (plan only); on Linux CI it is real python3.
    prefs.insert(QStringLiteral("PALACE_PYTHON"), QStringLiteral("/usr/bin/python3"));

    AboutDialog dlg(prefs);
    const QString actual = normalize(dlg.testToolProbePlanReport());
    if (expected != actual) {
        QFAIL(qPrintable(QString("tool plan mismatch:\n%1").arg(diffText(expected, actual))));
    }
}

/*!*******************************************************************************************************************
 * \brief Linux-only live probe: system python3 + gds2palace (installed in CI), no OpenEMS/Palace.
 **********************************************************************************************************************/
void AboutDialogTest::linuxProbe_liveGds2palace_matchesGolden()
{
#ifndef Q_OS_LINUX
    QSKIP("Live gds2palace About probe runs on Linux CI only");
#else
    const QString goldenPath = QFINDTESTDATA("golden/tst_about_linux_probe.txt");
    QVERIFY2(!goldenPath.isEmpty(), "golden/tst_about_linux_probe.txt missing");
    const QString expected = goldenSection(readUtf8(goldenPath), QStringLiteral("live_gds2palace_linux"));
    QVERIFY2(!expected.isEmpty(), "missing live_gds2palace_linux section");

    // Prefer explicit CI python; fall back to python3 on PATH.
    QString py = QString::fromLocal8Bit(qgetenv("EMSTUDIO_TEST_PYTHON"));
    if (py.isEmpty())
        py = QStringLiteral("/usr/bin/python3");
    QVERIFY2(QFileInfo::exists(py), qPrintable(QString("python missing: %1").arg(py)));

    // Sanity: gds2palace importable in that interpreter.
    {
        QProcess check;
        check.start(py,
                    {QStringLiteral("-c"),
                     QStringLiteral("import gds2palace; print(gds2palace.__version__)")});
        QVERIFY2(check.waitForFinished(15000), "gds2palace version check timed out");
        QVERIFY2(check.exitCode() == 0,
                 qPrintable(QString("gds2palace not importable:\n%1")
                                .arg(QString::fromUtf8(check.readAllStandardOutput()
                                                       + check.readAllStandardError()))));
    }

    QMap<QString, QVariant> prefs;
    prefs.insert(QStringLiteral("PALACE_PYTHON"), py);

    AboutDialog dlg(prefs);
    QVERIFY2(dlg.testWaitForProbesIdle(60000), "About tool probes did not finish in time");

    // Keep only the gds2palace status line for a stable golden.
    QString gdsLine;
    const QString status = dlg.testToolsStatusReport();
    for (const QString &line : status.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("gds2palace\t"))) {
            gdsLine = stripLatestNoise(line.trimmed());
            break;
        }
    }
    QVERIFY2(!gdsLine.isEmpty(), qPrintable(QString("gds2palace row missing:\n%1").arg(status)));

    const QString actual = normalize(QStringLiteral("=== tools_status ===\n") + gdsLine + QLatin1Char('\n'));
    if (expected != actual) {
        QFAIL(qPrintable(QString("live gds2palace mismatch:\n%1\nfull status:\n%2")
                             .arg(diffText(expected, actual), status)));
    }
#endif
}
