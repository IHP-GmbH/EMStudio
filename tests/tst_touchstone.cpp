/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_touchstone.h"

#include <QtTest/QtTest>
#include <QtMath>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "touchstone.h"

#include <cmath>

void TouchstoneTest::load_s1p_ri_hz()
{
    const QString path = QFINDTESTDATA("testdata/sample.s1p");
    QVERIFY2(!path.isEmpty(), "testdata/sample.s1p not found");

    TouchstoneNetwork net;
    QString err;
    QVERIFY2(net.load(path, &err), qPrintable(err));
    QVERIFY(net.isValid());
    QCOMPARE(net.nports(), 1);
    QCOMPARE(net.frequencyCount(), 2);
    QCOMPARE(net.referenceImpedance(), 50.0);
    QCOMPARE(net.fileName(), QStringLiteral("sample.s1p"));
    QCOMPARE(net.path(), path);

    QCOMPARE(net.frequencyHz().at(0), 1e9);
    QCOMPARE(net.frequencyHz().at(1), 2e9);
    QCOMPARE(net.s(0, 0, 0).real(), 0.1);
    QCOMPARE(net.s(0, 0, 0).imag(), 0.2);
    QCOMPARE(net.s(1, 0, 0).real(), 0.05);
    QCOMPARE(net.s(1, 0, 0).imag(), -0.1);
}

void TouchstoneTest::load_s2p_ma_ghz()
{
    const QString path = QFINDTESTDATA("testdata/sample.s2p");
    QVERIFY2(!path.isEmpty(), "testdata/sample.s2p not found");

    TouchstoneNetwork net;
    QVERIFY(net.load(path));
    QCOMPARE(net.nports(), 2);
    QCOMPARE(net.referenceImpedance(), 75.0);
    QCOMPARE(net.frequencyHz().at(0), 1e9);

    // S11 = 0.9 ∠ 10°
    const auto s11 = net.s(0, 0, 0);
    QCOMPARE(std::abs(s11), 0.9);
    QCOMPARE(qRadiansToDegrees(std::arg(s11)), 10.0);

    // S21 = 0.1 ∠ 90°
    const auto s21 = net.s(0, 1, 0);
    QVERIFY(qFuzzyCompare(s21.real() + 1.0, 1.0)); // ~0
    QCOMPARE(s21.imag(), 0.1);

    // S12 = 0.2 ∠ -45°
    const auto s12 = net.s(0, 0, 1);
    QCOMPARE(std::abs(s12), 0.2);

    // S22 = 0.8 ∠ 0°
    QCOMPARE(net.s(0, 1, 1).real(), 0.8);
    QVERIFY(qFuzzyIsNull(net.s(0, 1, 1).imag()));
}

void TouchstoneTest::load_s2p_db_mhz()
{
    const QString path = QFINDTESTDATA("testdata/sample_db.s2p");
    QVERIFY2(!path.isEmpty(), "testdata/sample_db.s2p not found");

    TouchstoneNetwork net;
    QVERIFY(net.load(path));
    QCOMPARE(net.frequencyHz().at(0), 1e9); // 1000 MHz
    const double mag = std::abs(net.s(0, 0, 0));
    QCOMPARE(20.0 * std::log10(mag), -1.0);
}

void TouchstoneTest::load_rejectsMissingFile()
{
    TouchstoneNetwork net;
    QString err;
    QVERIFY(!net.load(QStringLiteral("Z:/no/such/file.s2p"), &err));
    QVERIFY(err.contains(QStringLiteral("Cannot open")));
    QVERIFY(!net.isValid());
}

void TouchstoneTest::load_rejectsBadToken()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bad.s1p"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "# HZ S RI R 50\n1e9 not_a_number 0\n";
    }
    TouchstoneNetwork net;
    QString err;
    QVERIFY(!net.load(path, &err));
    QVERIFY(err.contains(QStringLiteral("Invalid numeric")));
}

void TouchstoneTest::load_infersPortsWithoutSuffix()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ports_data.txt"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        // 1-port RI block: freq + re + im
        QTextStream(&f) << "# HZ S RI R 50\n3e9 0.3 0.4\n";
    }
    TouchstoneNetwork net;
    QVERIFY(net.load(path));
    QCOMPARE(net.nports(), 1);
    QCOMPARE(net.s(0, 0, 0).real(), 0.3);
}

void TouchstoneTest::accessors_boundsAndSParam()
{
    const QString path = QFINDTESTDATA("testdata/sample.s1p");
    TouchstoneNetwork net;
    QVERIFY(net.load(path));

    QVERIFY(net.s(-1, 0, 0) == std::complex<double>{});
    QVERIFY(net.s(0, 9, 0) == std::complex<double>{});
    QVERIFY(net.sParam(3, 0).isEmpty());

    const auto series = net.sParam(0, 0);
    QCOMPARE(series.size(), 2);
    QCOMPARE(series.at(0).real(), 0.1);
}

void TouchstoneTest::load_khzAndThzUnits()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString khz = dir.filePath(QStringLiteral("u.s1p"));
    {
        QFile f(khz);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "# KHZ S RI R 50\n1000 0.1 0\n";
    }
    TouchstoneNetwork netK;
    QVERIFY(netK.load(khz));
    QCOMPARE(netK.frequencyHz().at(0), 1e6);

    const QString thz = dir.filePath(QStringLiteral("t.s1p"));
    {
        QFile f(thz);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "# THZ S RI R 50\n0.001 0.2 0\n";
    }
    TouchstoneNetwork netT;
    QVERIFY(netT.load(thz));
    QCOMPARE(netT.frequencyHz().at(0), 1e9);
}

void TouchstoneTest::load_rejectsIncompleteBlock()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("short.s2p"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        // One frequency needs 1 + 8 values for 2-port; provide too few.
        QTextStream(&f) << "# HZ S RI R 50\n1e9 0.1 0.0 0.2\n";
    }
    TouchstoneNetwork net;
    QString err;
    QVERIFY(!net.load(path, &err));
    QVERIFY(err.contains(QStringLiteral("Incomplete")));
}

void TouchstoneTest::load_acceptsDataWithoutOptionLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("nohash.s1p"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "! comment only\n1.0 0.5 0.0\n";
    }
    TouchstoneNetwork net;
    QVERIFY(net.load(path));
    QCOMPARE(net.frequencyHz().at(0), 1e9); // default GHz scale
}
