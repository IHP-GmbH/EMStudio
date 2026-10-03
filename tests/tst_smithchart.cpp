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

/*!*******************************************************************************************************************
 * \brief The chart area (unit circle, or the square when zoomed) is white; the rest of the widget keeps the
 *        window color.
 **********************************************************************************************************************/
void SmithChartTest::chartArea_isWhiteOnWindowBackground()
{
    SmithChartWidget w;
    w.resize(320, 320);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    auto check = [&w](bool zoomed) {
        w.setZoomed(zoomed);
        const QImage img = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
        // Sample the inner part of the plot area (grid lines are thin; most pixels are background).
        const QPoint c(img.width() / 2, img.height() / 2);
        const int r = int(0.3 * qMin(img.width(), img.height()));
        int white = 0;
        int total = 0;
        for (int y = c.y() - r; y <= c.y() + r; y += 2)
            for (int x = c.x() - r; x <= c.x() + r; x += 2) {
                ++total;
                if (img.pixel(x, y) == qRgb(255, 255, 255))
                    ++white;
            }
        QVERIFY2(white > total / 2, qPrintable(QStringLiteral("%1 of %2 white").arg(white).arg(total)));
        // The corner (outside the circle / square) keeps the window color.
        QCOMPARE(QColor(img.pixel(1, img.height() - 2)).rgb(), w.palette().window().color().rgb());
    };
    check(false);
    check(true);
}

/*!*******************************************************************************************************************
 * \brief Grid labels (as setupEM) and a click marker with a readout of frequency, |Γ| / angle and Z;
 *        ←/→ step through the points, Esc and a click away clear it. No legend in the chart (the Results
 *        viewer shows one for all charts); in a wide chart the readout sits left of the circle.
 **********************************************************************************************************************/
void SmithChartTest::labelsLegendAndMarker()
{
    SmithChartWidget w;
    w.resize(360, 360);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    // Grid labels: dark grey text inside the chart (the grid itself is light grey).
    auto labelPixels = [&w]() {
        const QImage img = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
        const QPoint c(img.width() / 2, img.height() / 2);
        const int r = int(0.35 * qMin(img.width(), img.height()));
        int n = 0;
        for (int y = c.y() - r; y <= c.y() + r; ++y)
            for (int x = c.x() - r; x <= c.x() + r; ++x) {
                const QColor col(img.pixel(x, y));
                if (std::abs(col.red() - col.green()) < 6 && std::abs(col.green() - col.blue()) < 6
                    && col.red() < 140)
                    ++n;
            }
        return n;
    };
    QVERIFY2(labelPixels() > 40, qPrintable(QString::number(labelPixels())));

    // Γ = 0 (Z = Z0), Γ = 1/3 (Z = 2 Z0), Γ = j (Z = j Z0), at 1, 2, 3 GHz.
    QVector<std::complex<double>> gamma = {{0.0, 0.0}, {1.0 / 3.0, 0.0}, {0.0, 1.0}};
    w.addTrace(gamma, QColor(220, 20, 20), Qt::SolidLine, QStringLiteral("dut"),
               {1e9, 2e9, 3e9}, 50.0);
    QTest::qWait(20);

    QVERIFY(w.markerReadout().isEmpty());
    w.setMarker(0, 1);
    QStringList lines = w.markerReadout();
    QCOMPARE(lines.value(0), QStringLiteral("dut"));
    QCOMPARE(lines.value(1), QStringLiteral("f = 2 GHz"));
    QVERIFY2(lines.value(2).startsWith(QStringLiteral("|Γ| = 0.3333")), qPrintable(lines.value(2)));
    QCOMPARE(lines.value(3), QStringLiteral("Z = 100.00 + j0.00 Ω"));
    w.setMarker(0, 2);
    QCOMPARE(w.markerReadout().value(3), QStringLiteral("Z = 0.00 + j50.00 Ω"));

    // Keys: step, clamp at the ends, Esc clears.
    QTest::keyClick(&w, Qt::Key_Left);
    QCOMPARE(w.markerIndex(), 1);
    QTest::keyClick(&w, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(w.markerIndex(), 2);
    QTest::keyClick(&w, Qt::Key_Escape);
    QCOMPARE(w.markerTrace(), -1);

    // Click: the chart center is Γ = 0 (the first point); a click in the corner clears.
    // Layout: 8 px margins, no title; the chart is the centered square.
    const QPoint center(w.width() / 2, w.height() / 2);
    QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, center + QPoint(3, -2));
    QCOMPARE(w.markerTrace(), 0);
    QCOMPARE(w.markerIndex(), 0);
    QCOMPARE(w.markerReadout().value(3), QStringLiteral("Z = 50.00 + j0.00 Ω"));
    QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(3, 3));
    QCOMPARE(w.markerTrace(), -1);

    // Without frequencies: the point number.
    w.clearTraces();
    w.addTrace(gamma, Qt::blue, Qt::SolidLine, QStringLiteral("x"));
    w.setMarker(0, 0);
    QCOMPARE(w.markerReadout().value(1), QStringLiteral("Point 1 of 3"));

    // Wide chart: the readout box (white) is left of the circle, the chart's corner stays window colored.
    w.resize(900, 360);
    QTest::qWait(20);
    const QImage img = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
    const int chartLeft = w.width() / 2 - (w.height() - 16) / 2;
    int boxWhite = 0;
    for (int y = 12; y < 60; ++y)
        for (int x = chartLeft - 60; x < chartLeft - 12; ++x)
            boxWhite += (QColor(img.pixel(x, y)).lightness() > 245) ? 1 : 0;
    QVERIFY2(boxWhite > 500, qPrintable(QString::number(boxWhite)));
    QVERIFY(QColor(img.pixel(3, img.height() - 3)).rgb() == w.palette().window().color().rgb());
}
