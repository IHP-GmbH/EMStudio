/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_smithchart.h"

#include <QtTest/QtTest>
#include <QApplication>

#include "smithchartwidget.h"

void SmithChartTest::paintsEmptyAndWithTraces()
{
    SmithChartWidget w;
    w.resize(320, 320);
    w.setChartTitle(QStringLiteral("S11"));
    w.setChartTitle(QStringLiteral("S11")); // no-op path
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    // Empty chart still paints the grid.
    const QPixmap empty = w.grab();
    QVERIFY(!empty.isNull());
    QVERIFY(empty.width() >= 180);

    QVector<std::complex<double>> gamma;
    gamma << std::complex<double>(0.0, 0.0)
          << std::complex<double>(0.5, 0.1)
          << std::complex<double>(-0.2, 0.3)
          << std::complex<double>(0.9, -0.1);
    w.addTrace(gamma, QColor(Qt::red), Qt::SolidLine, QStringLiteral("dut"));
    w.addTrace(gamma, QColor(Qt::blue), Qt::DashLine, QStringLiteral("ref"));

    QVector<std::complex<double>> single;
    single << std::complex<double>(0.1, -0.2);
    w.addTrace(single, QColor(Qt::green), Qt::SolidLine, QStringLiteral("marker"));

    QTest::qWait(20);
    const QPixmap withTraces = w.grab();
    QVERIFY(!withTraces.isNull());

    w.clearTraces();
    QTest::qWait(20);
    QVERIFY(!w.grab().isNull());
}

void SmithChartTest::zoomToggle_repaints()
{
    SmithChartWidget w;
    w.resize(280, 280);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    QVERIFY(!w.isZoomed());
    w.setZoomed(true);
    QVERIFY(w.isZoomed());
    w.setZoomed(true); // no-op path
    QVERIFY(!w.grab().isNull());

    w.setZoomed(false);
    QVERIFY(!w.isZoomed());
    QCOMPARE(w.minimumSizeHint(), QSize(140, 140));
    QCOMPARE(w.sizeHint(), QSize(280, 280));
}
