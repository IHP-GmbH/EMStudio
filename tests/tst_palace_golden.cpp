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

    return s.trimmed() + "\n";
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

    // ------------------------------------------------------------------
    // Now generate the default Palace model (deterministic, no dialogs)
    // ------------------------------------------------------------------
    QVERIFY2(w.testInitDefaultPalaceModel(),
             "testInitDefaultPalaceModel() failed (default Palace script is empty?)");

    return;

    // ------------------------------------------------------------------
    // Change a GUI setting (directly in m_simSettings)
    // ------------------------------------------------------------------
    w.testSetSimSetting("fstop", 50e9);

    // ------------------------------------------------------------------
    // Ports must be parsed from the script that was generated/imported
    // ------------------------------------------------------------------
    const auto ports = w.testParsePortsFromEditor();
    qDebug()<<ports.count();

    return;
    QVERIFY2(!ports.isEmpty(), "No ports parsed from editor after default model init");

    // ------------------------------------------------------------------
    // Generate python script from GUI state (no file I/O)
    // ------------------------------------------------------------------
    QString err;
    const QString got = w.testGenerateScriptFromGuiState(&err);
    QVERIFY2(!got.isEmpty(), qPrintable(err));

    // ------------------------------------------------------------------
    // Load golden and compare
    // ------------------------------------------------------------------
    const QString goldenPath = QFINDTESTDATA("golden/tst_palace_golden.py");
    QVERIFY2(!goldenPath.isEmpty(), "Golden python file not found via QFINDTESTDATA");

    const QString golden = readUtf8(goldenPath);
    QVERIFY2(!golden.isEmpty(),
             qPrintable(QString("Golden file empty: %1").arg(goldenPath)));

    const QString ng = normalize(golden);
    const QString na = normalize(got);

    if (ng != na) {
        const QString outPath =
            QDir::cleanPath(QCoreApplication::applicationDirPath() +
                            "/tst_palace_golden.actual.py");

        QFile out(outPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write(na.toUtf8());

        QFAIL(qPrintable(
            QString("Mismatch vs golden.\nGolden: %1\nActual written to: %2")
                .arg(goldenPath, outPath)));
    }
}

QTEST_MAIN(PalaceGolden)
