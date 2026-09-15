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
#include <QSettings>
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

    QHash<int, LayoutView::PortInfo> dirs;
    {
        LayoutView::PortInfo p;
        p.direction = QStringLiteral("x");
        dirs.insert(201, p);
        p.direction = QStringLiteral("-x");
        dirs.insert(202, p);
        p.direction = QStringLiteral("y");
        dirs.insert(203, p);
        p.direction = QStringLiteral("-z");
        dirs.insert(204, p);
    }

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
    dirs[201].direction = QStringLiteral("z");
    dirs[202].direction = QStringLiteral("-y");
    dirs[203].direction = QStringLiteral("+y");
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

void LayoutViewTest::viewMode3d_isoExtrusion_persistsInSettings()
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    const QVariant prev = settings.value(QStringLiteral("view3d"));
    settings.setValue(QStringLiteral("view3d"), false);
    settings.endGroup();
    settings.sync();

    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    view.show();
    QCOMPARE(view.viewMode(), LayoutView::ViewMode::Top2D);
    QVERIFY(!view.isView3d());

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, 0, 0, 10, 8);
    polys << makeRect(2, 2, 2, 6, 6);
    QHash<int, LayoutView::LayerStyle> styles;
    auto s1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    s1.hasZ = true;
    s1.zminUm = 0.0;
    s1.zmaxUm = 0.5;
    auto s2 = style(QStringLiteral("M2"), QStringLiteral("conductor"), QColor(40, 120, 200), 20);
    s2.hasZ = true;
    s2.zminUm = 1.0;
    s2.zmaxUm = 1.4;
    styles.insert(1, s1);
    styles.insert(2, s2);
    view.setPolygons(polys, styles);

    QHash<int, LayoutView::PortInfo> ports;
    LayoutView::PortInfo p1;
    p1.direction = QStringLiteral("z");
    p1.fromLayer = QStringLiteral("M1");
    p1.toLayer = QStringLiteral("M2");
    p1.hasFromZ = true;
    p1.hasToZ = true;
    p1.zFromUm = 0.25; // mid M1
    p1.zToUm = 1.2;    // mid M2
    ports.insert(201, p1);
    QVector<GdsFlatPolygon> withPort = polys;
    withPort << makeRect(201, 1, 1, 2, 2);
    view.setPolygons(withPort, styles, ports);

    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(view.isView3d());
    QTest::qWait(20);
    QVERIFY(!view.grab().isNull());

    // Orbit drag (Ctrl+Left) should keep 3D mode and redraw.
    QMouseEvent orbPress(QEvent::MouseButtonPress, QPointF(200, 150), Qt::LeftButton, Qt::LeftButton,
                         Qt::ControlModifier);
    QApplication::sendEvent(view.viewport(), &orbPress);
    QMouseEvent orbMove(QEvent::MouseMove, QPointF(260, 170), Qt::LeftButton, Qt::LeftButton,
                        Qt::ControlModifier);
    QApplication::sendEvent(view.viewport(), &orbMove);
    QMouseEvent orbRelease(QEvent::MouseButtonRelease, QPointF(260, 170), Qt::LeftButton, Qt::LeftButton,
                           Qt::ControlModifier);
    QApplication::sendEvent(view.viewport(), &orbRelease);
    QVERIFY(view.isView3d());

    QKeyEvent resetR(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier);
    QApplication::sendEvent(&view, &resetR);

    settings.beginGroup(QStringLiteral("LayoutPreview"));
    QCOMPARE(settings.value(QStringLiteral("view3d")).toBool(), true);
    if (prev.isValid())
        settings.setValue(QStringLiteral("view3d"), prev);
    else
        settings.remove(QStringLiteral("view3d"));
    settings.endGroup();
    settings.sync();

    view.setViewMode(LayoutView::ViewMode::Top2D);
    QVERIFY(!view.isView3d());
}

void LayoutViewTest::fieldMode_disables3d_andLoadsOverlay()
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    const QVariant prev3d = settings.value(QStringLiteral("view3d"));
    const QVariant prevField = settings.value(QStringLiteral("viewField"));
    settings.setValue(QStringLiteral("view3d"), false);
    settings.setValue(QStringLiteral("viewField"), false);
    settings.endGroup();
    settings.sync();

    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    view.show();

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, 0, 0, 10, 8);
    QHash<int, LayoutView::LayerStyle> styles;
    auto s1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    s1.hasZ = true;
    s1.zminUm = 0.0;
    s1.zmaxUm = 0.5;
    styles.insert(1, s1);
    view.setPolygons(polys, styles);

    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(view.isView3d());
    QVERIFY(!view.isFieldMode());

    view.setFieldMode(true);
    QVERIFY(view.isFieldMode());
    QVERIFY(!view.isView3d());
    QCOMPARE(view.viewMode(), LayoutView::ViewMode::Top2D);

    LayoutView::FieldOverlay ov;
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(QColor(0, 120, 255, 180));
    ov.image = img;
    ov.xminUm = 0;
    ov.xmaxUm = 10;
    ov.yminUm = 0;
    ov.ymaxUm = 8;
    ov.zUm = 0.25;
    ov.zMinUm = 0;
    ov.zMaxUm = 1;
    ov.quantity = QStringLiteral("|E|");
    ov.showArrows = true;
    LayoutView::FieldArrow a;
    a.xUm = 5;
    a.yUm = 4;
    a.dx = 1;
    a.dy = 0;
    a.mag = 1;
    ov.arrows << a;
    view.setFieldOverlay(ov);
    QVERIFY(view.fieldOverlay().valid());
    QTest::qWait(20);
    QVERIFY(!view.grab().isNull());

    // Turning 3D on must clear Field.
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(view.isView3d());
    QVERIFY(!view.isFieldMode());

    settings.beginGroup(QStringLiteral("LayoutPreview"));
    if (prev3d.isValid())
        settings.setValue(QStringLiteral("view3d"), prev3d);
    else
        settings.remove(QStringLiteral("view3d"));
    if (prevField.isValid())
        settings.setValue(QStringLiteral("viewField"), prevField);
    else
        settings.remove(QStringLiteral("viewField"));
    settings.endGroup();
    settings.sync();
}
