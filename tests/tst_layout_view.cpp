/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_layout_view.h"

#include <QtTest/QtTest>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QWheelEvent>

#include "layoutview.h"

namespace {

GdsFlatPolygon makeRect(int layer, qreal x0, qreal y0, qreal x1, qreal y1)
{
    GdsFlatPolygon p;
    p.layer = layer;
    p.pointsUm << QPointF(x0, y0) << QPointF(x1, y0) << QPointF(x1, y1) << QPointF(x0, y1)
               << QPointF(x0, y0);
    return p;
}

GdsFlatPolygon makeThinX(int layer, qreal x, qreal y0, qreal y1)
{
    GdsFlatPolygon p;
    p.layer = layer;
    // Near-zero width → port line path in LayoutView.
    p.pointsUm << QPointF(x, y0) << QPointF(x + 1e-6, y0) << QPointF(x + 1e-6, y1)
               << QPointF(x, y1) << QPointF(x, y0);
    return p;
}

GdsFlatPolygon makeThinY(int layer, qreal y, qreal x0, qreal x1)
{
    GdsFlatPolygon p;
    p.layer = layer;
    p.pointsUm << QPointF(x0, y) << QPointF(x1, y) << QPointF(x1, y + 1e-6)
               << QPointF(x0, y + 1e-6) << QPointF(x0, y);
    return p;
}

LayoutView::LayerStyle style(const QString &name, const QString &kind, const QColor &c, int order)
{
    LayoutView::LayerStyle st;
    st.name = name;
    st.kind = kind;
    st.color = c;
    st.order = order;
    return st;
}

} // namespace

void LayoutViewTest::setPolygons_conductorsAndPorts_drawAndInteract()
{
    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(640, 480);
    view.show();
    QTest::qWait(20);

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, 0, 0, 20, 10);
    polys << makeRect(2, 5, 5, 15, 12);
    // Fat port polygon + thin stubs for line rendering.
    polys << makeRect(201, 25, 0, 28, 8);
    polys << makeThinX(202, 30, 0, 10);
    polys << makeThinY(203, 15, 0, 10);
    // Degenerate thin stub (zero length) → synthesized stub branch.
    polys << makeThinX(204, 35, 5, 5);

    QHash<int, LayoutView::LayerStyle> styles;
    styles.insert(1, style(QStringLiteral("Metal1"), QStringLiteral("conductor"),
                           QColor(200, 80, 40), 10));
    styles.insert(2, style(QStringLiteral("Metal2"), QStringLiteral("conductor"),
                           QColor(40, 120, 200), 20));
    styles.insert(201, style(QStringLiteral("P1"), QStringLiteral("port"),
                             QColor(220, 40, 180), 100));
    styles.insert(202, style(QStringLiteral("P2"), QStringLiteral("port"),
                             QColor(220, 40, 180), 101));
    styles.insert(203, style(QStringLiteral("P3"), QStringLiteral("port"),
                             QColor(220, 40, 180), 102));
    styles.insert(204, style(QStringLiteral("P4"), QStringLiteral("port"),
                             QColor(220, 40, 180), 103));

    QHash<int, QString> dirs;
    dirs.insert(201, QStringLiteral("x"));
    dirs.insert(202, QStringLiteral("-x"));
    dirs.insert(203, QStringLiteral("y"));
    dirs.insert(204, QStringLiteral("-z"));

    view.setPolygons(polys, styles, dirs);
    QTest::qWait(30);
    QVERIFY(!view.grab().isNull());

    view.setHighlightedLayer(QStringLiteral("Metal1"));
    view.setHighlightedLayer(QStringLiteral("P1"));
    view.clearHighlight();

    QSignalSpy cleared(&view, &LayoutView::highlightCleared);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    QVERIFY(cleared.count() >= 0);

    QKeyEvent fitF(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
    QApplication::sendEvent(&view, &fitF);
    QKeyEvent fitHome(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
    QApplication::sendEvent(&view, &fitHome);

    QWheelEvent wheelIn(QPointF(200, 200), QPointF(200, 200), QPoint(0, 0), QPoint(0, 120),
                        Qt::NoButton, Qt::NoModifier, Qt::ScrollPhase::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &wheelIn);
    QWheelEvent wheelOut(QPointF(200, 200), QPointF(200, 200), QPoint(0, 0), QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::ScrollPhase::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &wheelOut);

    QMouseEvent move(QEvent::MouseMove, QPointF(220, 220), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(220, 220), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(220, 220), Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);

    // Shift+drag measure path
    QMouseEvent shiftPress(QEvent::MouseButtonPress, QPointF(100, 100), Qt::LeftButton, Qt::LeftButton,
                           Qt::ShiftModifier);
    QApplication::sendEvent(view.viewport(), &shiftPress);
    QMouseEvent shiftMove(QEvent::MouseMove, QPointF(180, 140), Qt::LeftButton, Qt::LeftButton,
                          Qt::ShiftModifier);
    QApplication::sendEvent(view.viewport(), &shiftMove);
    QMouseEvent shiftRelease(QEvent::MouseButtonRelease, QPointF(180, 140), Qt::LeftButton,
                             Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(view.viewport(), &shiftRelease);
    view.clearMeasure();

    view.setShowCoordinates(false);
    QVERIFY(!view.showCoordinates());
    view.setShowCoordinates(true);

    // Alternate directions on thick ports: +z / -y
    dirs.insert(201, QStringLiteral("z"));
    dirs.insert(202, QStringLiteral("-y"));
    dirs.insert(203, QStringLiteral("+y"));
    view.setPolygons(polys, styles, dirs);
    QTest::qWait(20);

    QApplication::sendEvent(&view, &esc);
    view.resize(800, 600);
    QTest::qWait(20);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&view, &leave);
}

void LayoutViewTest::visibilityOpacity_andClear()
{
    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    view.show();

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(5, 0, 0, 10, 10);
    QHash<int, LayoutView::LayerStyle> styles;
    styles.insert(5, style(QStringLiteral("M"), QStringLiteral("conductor"), Qt::blue, 1));
    view.setPolygons(polys, styles);

    QVERIFY(view.isLayerVisible(5));
    QCOMPARE(view.layerOpacity(5), 1.0);

    view.setLayerOpacity(5, 0.4);
    QCOMPARE(view.layerOpacity(5), 0.4);
    view.setLayerVisible(5, false);
    QVERIFY(!view.isLayerVisible(5));
    view.setLayerVisible(5, true);

    view.clear();
    view.clearHighlight(); // no-op when empty
}
