/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_gdslayout.h"

#include <QtTest/QtTest>

#include "gdslayout.h"

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
