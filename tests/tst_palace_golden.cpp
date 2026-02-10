#include "tst_palace_golden.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QCoreApplication>

#include "mainwindow.h"

static QString readUtf8(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

static QString normalize(QString s)
{
    s.replace("\r\n", "\n");
    s.replace(QRegularExpression(R"([ \t]+(?=\n))"), "");

    // Mask absolute paths (Windows + Linux)
    s.replace(QRegularExpression(R"(([A-Za-z]:[\\/][^ \n'"]+\.gds))"), "<GDS_PATH>");
    s.replace(QRegularExpression(R"(([A-Za-z]:[\\/][^ \n'"]+\.xml))"), "<XML_PATH>");
    s.replace(QRegularExpression(R"((/[^ \n'"]+\.gds))"), "<GDS_PATH>");
    s.replace(QRegularExpression(R"((/[^ \n'"]+\.xml))"), "<XML_PATH>");
    s.replace(QRegularExpression(R"(e([+-])0+(\d+))"), "e\\1\\2");

    return s.trimmed() + "\n";
}

static QString diffText(const QString& expected,
                        const QString& actual,
                        int contextLines = 2)
{
    const QStringList exp = expected.split('\n');
    const QStringList act = actual.split('\n');

    const int maxLines = qMax(exp.size(), act.size());

    for (int i = 0; i < maxLines; ++i) {
        const QString e = (i < exp.size()) ? exp[i] : "<EOF>";
        const QString a = (i < act.size()) ? act[i] : "<EOF>";

        if (e != a) {
            QString out;
            out += QString("Difference at line %1:\n").arg(i + 1);

            const int from = qMax(0, i - contextLines);
            const int to   = qMin(maxLines - 1, i + contextLines);

            for (int j = from; j <= to; ++j) {
                const QString ee = (j < exp.size()) ? exp[j] : "<EOF>";
                const QString aa = (j < act.size()) ? act[j] : "<EOF>";

                if (j == i) {
                    out += QString(">> EXPECTED: %1\n").arg(ee);
                    out += QString(">> ACTUAL  : %1\n").arg(aa);
                } else {
                    out += QString("   exp: %1\n").arg(ee);
                    out += QString("   act: %1\n").arg(aa);
                }
            }
            return out;
        }
    }

    return QString();
}

static bool writeUtf8Atomic(const QString& path, const QString& text, QString* outErr = nullptr)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        if (outErr) *outErr = QString("Golden file does not exist: %1").arg(path);
        return false;
    }

    // ensure dir exists and is writable
    const QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (outErr) *outErr = QString("Directory does not exist: %1").arg(dir.absolutePath());
        return false;
    }

    // atomic write: write to temp then replace
    const QString tmpPath = fi.absoluteFilePath() + ".tmp";

    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (outErr) *outErr = QString("Cannot open temp for write: %1 (%2)")
                          .arg(tmpPath, tmp.errorString());
        return false;
    }

    tmp.write(text.toUtf8());
    if (!tmp.flush()) {
        if (outErr) *outErr = QString("Flush failed for: %1 (%2)")
                          .arg(tmpPath, tmp.errorString());
        return false;
    }
    tmp.close();

    // Replace destination
    QFile::remove(fi.absoluteFilePath());
    if (!QFile::rename(tmpPath, fi.absoluteFilePath())) {
        if (outErr) *outErr = QString("Cannot replace golden file: %1").arg(fi.absoluteFilePath());
        QFile::remove(tmpPath);
        return false;
    }

    return true;
}

void PalaceGolden::defaultPalace_changeSettings_ports_and_compare()
{
    MainWindow w;

    // ------------------------------------------------------------------
    // Set GDS and XML from tests/golden first (so default script uses them)
    // ------------------------------------------------------------------
    const QString gdsPath = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gdsPath.isEmpty(), "Golden GDS file not found via QFINDTESTDATA");

    const QString xmlPath = QFINDTESTDATA("golden/SG13G2_200um.xml");
    QVERIFY2(!xmlPath.isEmpty(), "Golden XML file not found via QFINDTESTDATA");

    w.setGdsFile(gdsPath);
    w.setTopCell("t1");
    w.setSubstrateFile(xmlPath);

    w.testSetPreference("PALACE_INSTALL_PATH", "/tmp");
    w.refreshSimToolOptionsForTests();

    QString terr;
    QVERIFY2(w.testSetSimToolKey("palace", &terr), qPrintable(terr));

    // ------------------------------------------------------------------
    // Generate the default Palace model (deterministic, no dialogs)
    // This MUST populate the editor
    // ------------------------------------------------------------------
    QVERIFY2(w.testInitDefaultPalaceModel(),
             "testInitDefaultPalaceModel() failed (default Palace script is empty?)");

    // ------------------------------------------------------------------
    // Ports must be parsed from the script currently in the editor
    // ------------------------------------------------------------------
    {
        const auto ports = w.testParsePortsFromEditor();
        QVERIFY2(ports.isEmpty(), "No ports shall be parsed from editor after default model init");
    }

    // ------------------------------------------------------------------
    // Change a GUI setting (direct write into m_simSettings)
    // ------------------------------------------------------------------
    w.testSetSimSetting("margin", 51);

    // ------------------------------------------------------------------
    // Regenerate script from GUI state and write it back to editor
    // (so we compare editor content)
    // ------------------------------------------------------------------
    QString err;
    const QString regenerated = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!regenerated.isEmpty(), qPrintable(err));
    w.testSetEditorText(regenerated);

    // ------------------------------------------------------------------
    // Load golden and compare EDITOR CONTENT
    // ------------------------------------------------------------------
    const QString goldenPath = QFINDTESTDATA("golden/tst_palace_golden.py");
    QVERIFY2(!goldenPath.isEmpty(), "Golden python file not found via QFINDTESTDATA");

    const QString golden = readUtf8(goldenPath);
    QVERIFY2(!golden.isEmpty(),
             qPrintable(QString("Golden file empty: %1").arg(goldenPath)));

    const QString ng = normalize(golden);
    const QString na = normalize(w.testEditorText());

    if (ng != na) {

        /*QString werr;
        const bool ok = writeUtf8Atomic(goldenPath, na, &werr);
        QVERIFY2(ok, qPrintable(QString("Failed to write golden: %1").arg(werr)));
        return; // make test PASS after golden update*/

        const QString diff = diffText(ng, na);
        QFAIL(qPrintable(QString("Mismatch vs golden:\n\n%1").arg(diff)));
    }
}

QTEST_MAIN(PalaceGolden)
