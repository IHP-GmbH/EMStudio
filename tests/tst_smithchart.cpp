/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_smithchart.h"

#include <QtTest/QtTest>
#include <QApplication>
#include <cmath>

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

/*!*******************************************************************************************************************
 * \brief Regression for GitHub #24: multi-point Smith traces must be stroked, not brush-filled polygons.
 *
 * A long Γ arc that would paint a large red wedge if drawPath used a brush is expected to leave only a
 * thin stroke. Count strongly red pixels; a filled polygon is far denser than a 1.6 px line.
 **********************************************************************************************************************/
void SmithChartTest::multiPointTrace_isStrokedNotFilled()
{
    SmithChartWidget w;
    w.resize(320, 320);
    w.setChartTitle(QStringLiteral("S11"));
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    QVector<std::complex<double>> gamma;
    // Arc through the lower half of the unit disc — fills a large wedge if brush is set.
    for (int i = 0; i <= 80; ++i) {
        const double a = -0.15 - 0.55 * (double(i) / 80.0);
        const double b = -0.55 * std::sin(3.141592653589793 * double(i) / 80.0);
        gamma << std::complex<double>(a, b);
    }
    w.addTrace(gamma, QColor(220, 20, 20), Qt::SolidLine, QStringLiteral("dut"));

    QTest::qWait(30);
    const QImage img = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(!img.isNull());

    int redish = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 180 && c.green() < 90 && c.blue() < 90)
                ++redish;
        }
    }

    // Plot area is ~300²; a filled lower wedge is easily >10k red pixels. A stroke is a few hundred.
    const int plotArea = 300 * 300;
    QVERIFY2(redish > 50, "Expected a visible red stroke on the Smith chart");
    QVERIFY2(redish < plotArea / 20,
             qPrintable(QStringLiteral(
                 "Trace looks brush-filled (%1 redish px); expected stroked line only")
                            .arg(redish)));
}
