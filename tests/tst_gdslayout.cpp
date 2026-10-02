/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_gdslayout.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <cmath>

#include "gdslayout.h"
#include "test_utils.h"

void GdsLayoutTest::flattenTopCell_goldenGds_returnsPolygons()
{
    const QString gds = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY2(!gds.isEmpty(), "golden/line_simple_viaport.gds missing");

    QVector<GdsFlatPolygon> polys;
    QString err;
    QVERIFY2(GdsLayout::flattenTopCell(gds, QStringLiteral("t1"), &polys, &err),
             qPrintable(err));
    QVERIFY(polys.size() >= 1);
    for (const GdsFlatPolygon &p : polys) {
        QVERIFY(p.layer >= 0);
        QVERIFY(p.pointsUm.size() >= 3);
    }

    // Second call with null error pointer and reuse of out vector.
    QVERIFY(GdsLayout::flattenTopCell(gds, QStringLiteral("t1"), &polys, nullptr));
}

void GdsLayoutTest::flattenTopCell_missingFileOrCell_fails()
{
    QVector<GdsFlatPolygon> polys;
    QString err;
    QVERIFY(!GdsLayout::flattenTopCell(QStringLiteral("C:/no/such/file.gds"),
                                       QStringLiteral("t1"), &polys, &err));
    QVERIFY(!err.isEmpty());

    const QString gds = QFINDTESTDATA("golden/line_simple_viaport.gds");
    QVERIFY(!gds.isEmpty());
    err.clear();
    QVERIFY(!GdsLayout::flattenTopCell(gds, QStringLiteral("MISSING_CELL"), &polys, &err));
    QVERIFY(!err.isEmpty());

    // null out vector is a soft failure / no crash
    QVERIFY(!GdsLayout::flattenTopCell(gds, QStringLiteral("t1"), nullptr, &err));
}

namespace {

QRectF boundsOf(const GdsFlatPolygon &p) { return p.pointsUm.boundingRect(); }

} // namespace

/*!*******************************************************************************************************************
 * \brief STRANS reflection is about the X axis (applied before the rotation), AREF corner points are
 *        the origin plus cols / rows pitches, and a TEXT's MAG does not leak into the next reference.
 *        (KLayout shows these layouts the same way.)
 **********************************************************************************************************************/
void GdsLayoutTest::flattenTopCell_mirrorArraysAndTextMag_matchGdsSpec()
{
    // CHILD: one rectangle x 1..4, y 2..3 µm (1 nm database unit, the reader's default).
    const QVector<QPoint> rect = {QPoint(1000, 2000), QPoint(4000, 2000), QPoint(4000, 3000),
                                  QPoint(1000, 3000), QPoint(1000, 2000)};
    QByteArray g;
    g += GdsTestWriter::record(0x00, 0x02, GdsTestWriter::int16(600));                         // HEADER
    g += GdsTestWriter::record(0x01, 0x02, QByteArray(24, '\0'));                  // BGNLIB
    g += GdsTestWriter::record(0x02, 0x06, "LIB");                                 // LIBNAME
    g += GdsTestWriter::record(0x05, 0x02, QByteArray(24, '\0'));                  // BGNSTR
    g += GdsTestWriter::record(0x06, 0x06, "CHILD");                               // STRNAME
    g += GdsTestWriter::record(0x08, 0x00) + GdsTestWriter::record(0x0D, 0x02, GdsTestWriter::int16(1)) + GdsTestWriter::record(0x0E, 0x02, GdsTestWriter::int16(0))
         + GdsTestWriter::xy(rect) + GdsTestWriter::record(0x11, 0x00);                        // BOUNDARY … ENDEL
    g += GdsTestWriter::record(0x07, 0x00);                                        // ENDSTR

    g += GdsTestWriter::record(0x05, 0x02, QByteArray(24, '\0'));
    g += GdsTestWriter::record(0x06, 0x06, "TOP");
    // TEXT with MAG 5 (text size), then a plain SREF at x = 100 µm.
    g += GdsTestWriter::record(0x0C, 0x00) + GdsTestWriter::record(0x0D, 0x02, GdsTestWriter::int16(63)) + GdsTestWriter::record(0x16, 0x02, GdsTestWriter::int16(0))
         + GdsTestWriter::record(0x1A, 0x01, GdsTestWriter::int16(0)) + GdsTestWriter::record(0x1B, 0x05, GdsTestWriter::real8(5.0))
         + GdsTestWriter::xy({QPoint(0, 0)}) + GdsTestWriter::record(0x19, 0x06, "label") + GdsTestWriter::record(0x11, 0x00);
    g += GdsTestWriter::record(0x0A, 0x00) + GdsTestWriter::record(0x12, 0x06, "CHILD") + GdsTestWriter::xy({QPoint(100000, 0)})
         + GdsTestWriter::record(0x11, 0x00);
    // Mirrored SREF (m0) at x = 200 µm.
    g += GdsTestWriter::record(0x0A, 0x00) + GdsTestWriter::record(0x12, 0x06, "CHILD") + GdsTestWriter::record(0x1A, 0x01, GdsTestWriter::int16(0x8000))
         + GdsTestWriter::xy({QPoint(200000, 0)}) + GdsTestWriter::record(0x11, 0x00);
    // Mirrored and rotated by 180° (m90) at x = 300 µm.
    g += GdsTestWriter::record(0x0A, 0x00) + GdsTestWriter::record(0x12, 0x06, "CHILD") + GdsTestWriter::record(0x1A, 0x01, GdsTestWriter::int16(0x8000))
         + GdsTestWriter::record(0x1C, 0x05, GdsTestWriter::real8(180.0)) + GdsTestWriter::xy({QPoint(300000, 0)}) + GdsTestWriter::record(0x11, 0x00);
    // AREF 3 columns x 2 rows at (0, 100) µm: pitch 20 µm in x, 10 µm in y.
    g += GdsTestWriter::record(0x0B, 0x00) + GdsTestWriter::record(0x12, 0x06, "CHILD")
         + GdsTestWriter::record(0x13, 0x02, GdsTestWriter::int16(3) + GdsTestWriter::int16(2))
         + GdsTestWriter::xy({QPoint(0, 100000), QPoint(60000, 100000), QPoint(0, 120000)}) + GdsTestWriter::record(0x11, 0x00);
    g += GdsTestWriter::record(0x07, 0x00);
    g += GdsTestWriter::record(0x04, 0x00);                                        // ENDLIB

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("spec.gds"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(g);
    f.close();

    QVector<GdsFlatPolygon> polys;
    QString err;
    QVERIFY2(GdsLayout::flattenTopCell(path, QStringLiteral("TOP"), &polys, &err), qPrintable(err));
    QCOMPARE(polys.size(), 3 + 6);

    QVector<QRectF> got;
    for (const GdsFlatPolygon &p : polys)
        got << boundsOf(p);
    auto has = [&got](qreal x0, qreal y0, qreal x1, qreal y1) {
        for (const QRectF &r : got)
            if (std::abs(r.left() - x0) < 1e-6 && std::abs(r.top() - y0) < 1e-6
                && std::abs(r.right() - x1) < 1e-6 && std::abs(r.bottom() - y1) < 1e-6)
                return true;
        return false;
    };
    QVERIFY2(has(101, 2, 104, 3), "SREF after a magnified TEXT keeps magnification 1");
    QVERIFY2(has(201, -3, 204, -2), "m0: reflection about the X axis negates y");
    QVERIFY2(has(296, 2, 299, 3), "m90: reflect, then rotate by 180 degrees");
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col)
            QVERIFY2(has(1 + 20 * col, 102 + 10 * row, 4 + 20 * col, 103 + 10 * row),
                     qPrintable(QStringLiteral("AREF element col %1 row %2").arg(col).arg(row)));
}
