/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_layout_view.h"
#include "appsettings.h"

#include <QtTest/QtTest>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QToolButton>
#include <QCheckBox>
#include <QSlider>
#include <QScrollBar>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QCoreApplication>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QDebug>
#include <QFileInfo>

#include "layoutview.h"
#include "navigationstyle.h"
#include "gdslayout.h"

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

    // Ctrl+Shift+click measure points (start, end)
    const Qt::KeyboardModifiers measureMods = Qt::ControlModifier | Qt::ShiftModifier;
    QMouseEvent measurePress(QEvent::MouseButtonPress, QPointF(100, 100), Qt::LeftButton, Qt::LeftButton,
                             measureMods);
    QApplication::sendEvent(view.viewport(), &measurePress);
    QVERIFY(view.hasMeasure());
    QMouseEvent measureMove(QEvent::MouseMove, QPointF(180, 140), Qt::NoButton, Qt::NoButton,
                            Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &measureMove);
    QMouseEvent measureEnd(QEvent::MouseButtonPress, QPointF(180, 140), Qt::LeftButton,
                           Qt::LeftButton, measureMods);
    QApplication::sendEvent(view.viewport(), &measureEnd);
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
    // Opacity is the true 2D fill opacity; untouched layers use the built-in default.
    QCOMPARE(view.layerOpacity(5), LayoutView::defaultFillOpacity());

    auto fillAlpha = [&view]() {
        for (QGraphicsItem *it : view.scene()->items())
            if (auto *poly = qgraphicsitem_cast<QGraphicsPolygonItem *>(it))
                return poly->brush().color().alpha();
        return -1;
    };
    QCOMPARE(fillAlpha(), int(LayoutView::defaultFillOpacity() * 255 + 0.5));

    view.setLayerOpacity(5, 0.4);
    QCOMPARE(view.layerOpacity(5), 0.4);
    QCOMPARE(fillAlpha(), int(0.4 * 255 + 0.5)); // the slider value is what is drawn

    view.setAllLayerOpacity(0.8);
    QCOMPARE(view.layerOpacity(5), 0.8);
    QCOMPARE(fillAlpha(), int(0.8 * 255 + 0.5));
    view.setLayerVisible(5, false);
    QVERIFY(!view.isLayerVisible(5));
    view.setLayerVisible(5, true);

    view.clear();
    view.clearHighlight(); // no-op when empty
}

void LayoutViewTest::viewMode3d_isoExtrusion_persistsInSettings()
{
    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    const QVariant prev = settings.value(QStringLiteral("view3d"));
    const QVariant prevField = settings.value(QStringLiteral("viewField"));
    settings.setValue(QStringLiteral("view3d"), false);
    settings.setValue(QStringLiteral("viewField"), false);
    settings.endGroup();
    settings.sync();

    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    view.show();
    // Leftover LayoutPreview/viewField from another suite would force Iso3D → Top2D.
    if (view.isFieldMode())
        view.setFieldMode(false);
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
    if (prevField.isValid())
        settings.setValue(QStringLiteral("viewField"), prevField);
    else
        settings.remove(QStringLiteral("viewField"));
    settings.endGroup();
    settings.sync();

    view.setViewMode(LayoutView::ViewMode::Top2D);
    QVERIFY(!view.isView3d());
}

void LayoutViewTest::fieldMode_keeps2d_and3dOpensExternalSignal()
{
    QSettings settings = emstudioSettings();
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
    QVERIFY(!view.isFieldVolume());

    QSignalSpy spy(&view, &LayoutView::fieldExternalVolumeRequested);
    QVERIFY(spy.isValid());
    // Simulate the 3D toolbutton while Field is on.
    auto *modeBtn = view.findChild<QToolButton *>(QStringLiteral("layoutViewModeBtn"));
    QVERIFY(modeBtn);
    QCOMPARE(modeBtn->text(), QStringLiteral("3D"));
    QVERIFY(!modeBtn->isChecked());
    // Prefer click(): some platforms coalesce setChecked with prior syncFloatingControls.
    QTest::mouseClick(modeBtn, Qt::LeftButton);
    QCoreApplication::processEvents();
    if (spy.count() == 0) {
        // Fallback for headless/offscreen where click may not toggle.
        modeBtn->setChecked(true);
        QCoreApplication::processEvents();
    }
    QCOMPARE(spy.count(), 1);
    QVERIFY(!view.isView3d());
    QVERIFY(view.isFieldMode());
    QCOMPARE(modeBtn->text(), QStringLiteral("3D"));
    QVERIFY(!modeBtn->isChecked());

    // Iso3D while Field is on must stay Top2D (volume is external only).
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(!view.isView3d());
    QVERIFY(view.isFieldMode());

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
    view.setFieldOverlay(ov);
    QVERIFY(view.fieldOverlay().valid());

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

void LayoutViewTest::iso3d_denseVias_growEnvelope_reportsTiming()
{
    // Dense via array similar to balun_mim_vias (~3k vias): ADS growEnvelope → few bars.
    constexpr int kGrid = 55; // 55×55 = 3025 vias
    constexpr qreal kVia = 0.2;
    constexpr qreal kPitch = 0.45;

    QVector<GdsFlatPolygon> polys;
    polys.reserve(kGrid * kGrid + 2);
    for (int iy = 0; iy < kGrid; ++iy) {
        for (int ix = 0; ix < kGrid; ++ix) {
            const qreal x0 = ix * kPitch;
            const qreal y0 = iy * kPitch;
            polys << makeRect(10, x0, y0, x0 + kVia, y0 + kVia);
        }
    }
    polys << makeRect(1, -2, -2, kGrid * kPitch + 2, kGrid * kPitch + 2);
    polys << makeRect(2, 5, 5, 15, 15);

    QHash<int, LayoutView::LayerStyle> styles;
    auto via = style(QStringLiteral("Via1"), QStringLiteral("via"), QColor(180, 180, 60), 5);
    via.hasZ = true;
    via.zminUm = 0.5;
    via.zmaxUm = 1.0;
    auto m1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    m1.hasZ = true;
    m1.zminUm = 0.0;
    m1.zmaxUm = 0.5;
    auto m2 = style(QStringLiteral("M2"), QStringLiteral("conductor"), QColor(40, 120, 200), 20);
    m2.hasZ = true;
    m2.zminUm = 1.0;
    m2.zmaxUm = 1.4;
    styles.insert(10, via);
    styles.insert(1, m1);
    styles.insert(2, m2);

    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    settings.setValue(QStringLiteral("view3d"), false);
    settings.setValue(QStringLiteral("viewField"), false);
    settings.endGroup();
    settings.sync();

    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(800, 600);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);

    view.setPolygons(polys, styles);

    QElapsedTimer wall;
    wall.start();
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    const qint64 wallMs = wall.elapsed();
    const auto st = view.lastIso3dRebuildStats();

    qInfo().nospace()
        << "Iso3D dense vias timing: polys=" << polys.size()
        << " vias=" << st.viaPolyCount
        << " envelopes=" << st.viaEnvelopeCount
        << " faces=" << st.faceCount
        << " items=" << st.sceneItemCount
        << " merged=" << st.mergedVias
        << " pixmap=" << st.usedPixmap
        << " rebuildMs=" << st.ms
        << " wallMs=" << wallMs;

    QVERIFY(view.isView3d());
    QVERIFY2(st.mergedVias, "expected ADS-style via growEnvelope merge");
    QVERIFY2(st.viaEnvelopeCount > 0 && st.viaEnvelopeCount < st.viaPolyCount / 10,
             qPrintable(QStringLiteral("envelopes=%1 vias=%2 — merge did not collapse arrays")
                                .arg(st.viaEnvelopeCount).arg(st.viaPolyCount)));
    QVERIFY2(st.ms < 2500,
             qPrintable(QStringLiteral("Iso3D rebuild too slow: %1 ms").arg(st.ms)));
    QVERIFY2(wallMs < 4000,
             qPrintable(QStringLiteral("Iso3D mode switch wall too slow: %1 ms").arg(wallMs)));

    // Orbit rebuild should also stay interactive.
    wall.restart();
    view.setViewMode(LayoutView::ViewMode::Iso3D); // no-op path may skip; force rebuild via setPolygons
    view.setPolygons(polys, styles);
    const auto st2 = view.lastIso3dRebuildStats();
    qInfo().nospace() << "Iso3D rebuild#2: rebuildMs=" << st2.ms
                      << " envelopes=" << st2.viaEnvelopeCount;
    QVERIFY2(st2.ms < 2500, qPrintable(QStringLiteral("2nd rebuild %1 ms").arg(st2.ms)));
}

void LayoutViewTest::iso3d_balunExample_flattenAndRebuild_reportsTiming()
{
    const QString gds = QStringLiteral("examples/palace/balun_mim_vias/trans_100diff_to_80se_ports.gds");
    if (!QFileInfo::exists(gds))
        QSKIP("balun_mim_vias example GDS not present");

    QElapsedTimer step;
    step.start();
    QVector<GdsFlatPolygon> polys;
    QString err;
    QVERIFY2(GdsLayout::flattenTopCell(gds, QStringLiteral("central_coils_100_tm2_7u_cm_co"),
                                       &polys, &err),
             qPrintable(err));
    const qint64 flattenMs = step.elapsed();

    // Mark via-like layers by common IHP GDS numbers if present; else treat small polys as via.
    QHash<int, int> layerCount;
    for (const GdsFlatPolygon &p : polys)
        layerCount[p.layer] += 1;

    QHash<int, LayoutView::LayerStyle> styles;
    int order = 0;
    for (auto it = layerCount.cbegin(); it != layerCount.cend(); ++it) {
        LayoutView::LayerStyle st;
        st.name = QStringLiteral("L%1").arg(it.key());
        // Dense layers → via (merge path); sparse → conductor.
        st.kind = (it.value() >= 48) ? QStringLiteral("via") : QStringLiteral("conductor");
        st.color = QColor(100 + (it.key() * 37) % 120, 80, 160);
        st.order = order++;
        st.hasZ = true;
        st.zminUm = 0.1 * order;
        st.zmaxUm = st.zminUm + 0.4;
        styles.insert(it.key(), st);
    }

    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    settings.setValue(QStringLiteral("view3d"), true);
    settings.setValue(QStringLiteral("viewField"), false);
    settings.endGroup();
    settings.sync();

    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(800, 600);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);
    view.setViewMode(LayoutView::ViewMode::Iso3D);

    step.restart();
    view.setPolygons(polys, styles);
    const qint64 setMs = step.elapsed();
    const auto st = view.lastIso3dRebuildStats();

    qInfo().nospace()
        << "Balun Iso3D: polys=" << polys.size()
        << " flattenMs=" << flattenMs
        << " setPolygonsMs=" << setMs
        << " vias=" << st.viaPolyCount
        << " envelopes=" << st.viaEnvelopeCount
        << " faces=" << st.faceCount
        << " merged=" << st.mergedVias
        << " pixmap=" << st.usedPixmap
        << " rebuildMs=" << st.ms;

    QVERIFY(polys.size() > 1000);
    QVERIFY2(flattenMs < 8000,
             qPrintable(QStringLiteral("GDS flatten too slow: %1 ms").arg(flattenMs)));
    QVERIFY2(st.ms < 3000,
             qPrintable(QStringLiteral("Iso3D rebuild too slow: %1 ms").arg(st.ms)));
    QVERIFY2(setMs < 5000,
             qPrintable(QStringLiteral("setPolygons too slow: %1 ms").arg(setMs)));
}

namespace {

void sendWheel(LayoutView &view, int dy, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWheelEvent ev(QPointF(200, 150), QPointF(200, 150), QPoint(0, 0), QPoint(0, dy),
                   Qt::NoButton, mods, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &ev);
}

void sendDrag(LayoutView &view, Qt::MouseButton button, Qt::KeyboardModifiers mods,
              const QPointF &from, const QPointF &to)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, button, button, mods);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, to, button, button, mods);
    QApplication::sendEvent(view.viewport(), &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to, button, Qt::NoButton, mods);
    QApplication::sendEvent(view.viewport(), &release);
}

void sendKey(LayoutView &view, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent ev(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(&view, &ev);
}

} // namespace

void LayoutViewTest::navigationStyles_mouseAndKeys()
{
    QSettings settings = emstudioSettings();
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
    polys << makeRect(1, 0, 0, 100, 80);
    QHash<int, LayoutView::LayerStyle> styles;
    auto s1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    s1.hasZ = true;
    s1.zmaxUm = 2.0;
    styles.insert(1, s1);
    view.setPolygons(polys, styles);

    // Keys 3 / 2 switch the view mode (and announce it, e.g. for the layer panel).
    QSignalSpy modeSpy(&view, &LayoutView::viewModeChanged);
    sendKey(view, Qt::Key_3);
    QVERIFY(view.isView3d());
    QCOMPARE(modeSpy.count(), 1);
    QVERIFY(modeSpy.last().at(0).toBool());

    // EMStudio: the wheel orbits in 3D (no zoom), Ctrl+wheel zooms, right drag orbits.
    view.setNavigationStyle(NavStyle::EMStudio);
    const qreal yaw0 = view.orbitYawDeg();
    const qreal pitch0 = view.orbitPitchDeg();
    const qreal scale0 = view.transform().m11();
    sendWheel(view, -120);
    QVERIFY(!qFuzzyCompare(view.orbitPitchDeg(), pitch0));
    QCOMPARE(view.transform().m11(), scale0);
    sendWheel(view, 120, Qt::ControlModifier);
    QVERIFY(view.transform().m11() > scale0);
    const qreal yawBeforeRight = view.orbitYawDeg();
    sendDrag(view, Qt::RightButton, Qt::NoModifier, QPointF(200, 150), QPointF(260, 150));
    QVERIFY(view.orbitYawDeg() > yawBeforeRight);
    QVERIFY(!qFuzzyCompare(view.orbitYawDeg(), yaw0));

    // setupEM: the wheel zooms in 3D, right drag zooms (up = in) instead of orbiting.
    view.setNavigationStyle(NavStyle::SetupEM);
    QCOMPARE(view.navigationStyle(), NavStyle::SetupEM);
    const qreal yaw1 = view.orbitYawDeg();
    const qreal scale1 = view.transform().m11();
    sendWheel(view, 120);
    QVERIFY(view.transform().m11() > scale1);
    QCOMPARE(view.orbitYawDeg(), yaw1);
    const qreal scale2 = view.transform().m11();
    sendDrag(view, Qt::RightButton, Qt::NoModifier, QPointF(200, 150), QPointF(200, 100));
    QVERIFY(view.transform().m11() > scale2);
    QCOMPARE(view.orbitYawDeg(), yaw1);

    // Axis views: X looks from +X (yaw -90, pitch 0), I back to isometric, Z to the top view.
    sendKey(view, Qt::Key_X);
    QCOMPARE(view.orbitYawDeg(), -90.0);
    QCOMPARE(view.orbitPitchDeg(), 0.0);
    sendKey(view, Qt::Key_Y, Qt::ShiftModifier);
    QCOMPARE(view.orbitYawDeg(), 0.0);
    sendKey(view, Qt::Key_I);
    QCOMPARE(view.orbitYawDeg(), 45.0);
    QCOMPARE(view.orbitPitchDeg(), 30.0);
    sendKey(view, Qt::Key_Z);
    QVERIFY(!view.isView3d());

    // 2D: + / - zoom, arrows pan, Shift+left pans in both styles.
    const qreal scale3 = view.transform().m11();
    sendKey(view, Qt::Key_Plus);
    QVERIFY(view.transform().m11() > scale3);
    sendKey(view, Qt::Key_Plus);
    sendKey(view, Qt::Key_Plus);
    const int h0 = view.horizontalScrollBar()->value();
    sendKey(view, Qt::Key_Right);
    QVERIFY(view.horizontalScrollBar()->value() > h0);
    const int h1 = view.horizontalScrollBar()->value();
    sendDrag(view, Qt::LeftButton, Qt::ShiftModifier, QPointF(200, 150), QPointF(240, 150));
    QVERIFY(view.horizontalScrollBar()->value() < h1);
    QVERIFY(!view.hasMeasure());

    // M arms measure mode: two clicks make a ruler; Esc clears ruler and mode.
    sendKey(view, Qt::Key_M);
    QVERIFY(view.isMeasureArmed());
    QSignalSpy measureSpy(&view, &LayoutView::measureChanged);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(200, 120));
    QVERIFY(view.hasMeasure());
    QVERIFY(measureSpy.count() >= 2);
    QVERIFY(measureSpy.last().at(0).toBool());
    QVERIFY(measureSpy.last().at(3).toReal() > 0.0);
    sendKey(view, Qt::Key_Escape);
    QVERIFY(!view.isMeasureArmed());
    QVERIFY(!view.hasMeasure());

    // Field keys: Shift+F toggles Field, L / P toggle the panel checkboxes,
    // PgUp moves the Z slider, 3 asks for the external 3D viewer.
    sendKey(view, Qt::Key_F, Qt::ShiftModifier);
    QVERIFY(view.isFieldMode());
    const bool log0 = view.fieldLogScale();
    sendKey(view, Qt::Key_L);
    QCOMPARE(view.fieldLogScale(), !log0);
    const bool probe0 = view.fieldShowTemp();
    sendKey(view, Qt::Key_P);
    QCOMPARE(view.fieldShowTemp(), !probe0);
    sendKey(view, Qt::Key_P); // restore the persisted Probe setting
    auto *zSlider = view.findChild<QSlider *>();
    QVERIFY(zSlider);
    const int z0 = zSlider->value();
    QSignalSpy sliceSpy(&view, &LayoutView::fieldSliceRequest);
    sendKey(view, Qt::Key_PageUp);
    QCOMPARE(zSlider->value(), z0 + 50);
    QTRY_VERIFY_WITH_TIMEOUT(sliceSpy.count() >= 1, 2000);
    QSignalSpy extSpy(&view, &LayoutView::fieldExternalVolumeRequested);
    sendKey(view, Qt::Key_3);
    QCOMPARE(extSpy.count(), 1);
    QVERIFY(!view.isView3d());
    sendKey(view, Qt::Key_F, Qt::ShiftModifier);
    QVERIFY(!view.isFieldMode());

    // The mode button tooltip follows the style.
    auto *modeBtn = view.findChild<QToolButton *>(QStringLiteral("layoutViewModeBtn"));
    QVERIFY(modeBtn);
    QVERIFY(modeBtn->toolTip().contains(QStringLiteral("setupEM")));
    view.setNavigationStyle(NavStyle::EMStudio);
    QVERIFY(modeBtn->toolTip().contains(QStringLiteral("EMStudio")));

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

namespace {

/*! Sum of alpha over the Iso3D pixmap (dense scenes are one pre-rendered image); -1 if none. */
qint64 iso3dPixmapAlpha(LayoutView &view)
{
    for (QGraphicsItem *it : view.scene()->items()) {
        if (auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(it)) {
            const QImage img = pix->pixmap().toImage().convertToFormat(QImage::Format_ARGB32);
            qint64 sum = 0;
            for (int y = 0; y < img.height(); y += 2) {
                const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); x += 2)
                    sum += qAlpha(row[x]);
            }
            return sum;
        }
    }
    return -1;
}

} // namespace

void LayoutViewTest::iso3d_opacityAndVisibility_redrawDenseScene()
{
    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    const QVariant prev3d = settings.value(QStringLiteral("view3d"));
    const QVariant prevField = settings.value(QStringLiteral("viewField"));
    settings.setValue(QStringLiteral("view3d"), false);
    settings.setValue(QStringLiteral("viewField"), false);
    settings.endGroup();

    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(500, 400);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);

    // 10 x 10 separate metal squares: 500 faces, above the pixmap threshold.
    QVector<GdsFlatPolygon> polys;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
            polys << makeRect(1, i * 10.0, j * 10.0, i * 10.0 + 6.0, j * 10.0 + 6.0);
    QHash<int, LayoutView::LayerStyle> styles;
    auto s1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    s1.hasZ = true;
    s1.zmaxUm = 2.0;
    styles.insert(1, s1);
    view.setPolygons(polys, styles);
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(view.lastIso3dRebuildStats().usedPixmap);

    const qint64 alphaDefault = iso3dPixmapAlpha(view);
    QVERIFY(alphaDefault > 0);
    const qreal yaw = view.orbitYawDeg();
    const QTransform zoom = view.transform();

    // Measure ruler survives an opacity change (same projection).
    sendKey(view, Qt::Key_M);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(200, 120));
    sendKey(view, Qt::Key_M);
    QVERIFY(view.hasMeasure());

    // The slider value must show up at once, not at the next orbit.
    view.setAllLayerOpacity(0.2);
    const qint64 alphaLow = iso3dPixmapAlpha(view);
    QVERIFY2(alphaLow > 0 && alphaLow < alphaDefault * 0.6,
             qPrintable(QStringLiteral("%1 vs %2").arg(alphaLow).arg(alphaDefault)));
    QVERIFY(view.hasMeasure());
    QCOMPARE(view.orbitYawDeg(), yaw);
    QCOMPARE(view.transform(), zoom);

    view.setLayerOpacity(1, 1.0);
    QVERIFY(iso3dPixmapAlpha(view) > alphaDefault);

    // Orbiting must not move the scene extent, the scrollbars or the view center.
    sendKey(view, Qt::Key_Plus); // zoomed in: scrollbars are in use
    sendKey(view, Qt::Key_Plus);
    QCoreApplication::processEvents();
    const QRectF rect0 = view.sceneRect();
    const int hMax0 = view.horizontalScrollBar()->maximum();
    const int vMax0 = view.verticalScrollBar()->maximum();
    const QPointF center0 = view.mapToScene(view.viewport()->rect().center());
    QPointF pos(250, 200);
    QMouseEvent press(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(view.viewport(), &press);
    for (int i = 0; i < 12; ++i) {
        pos += QPointF(15, (i % 2) ? 9 : -6);
        QMouseEvent move(QEvent::MouseMove, pos, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        QApplication::sendEvent(view.viewport(), &move);
        QTest::qWait(40); // let the coalesced orbit rebuild run
        QCOMPARE(view.sceneRect(), rect0);
        QCOMPARE(view.horizontalScrollBar()->maximum(), hMax0);
        QCOMPARE(view.verticalScrollBar()->maximum(), vMax0);
        const QPointF c = view.mapToScene(view.viewport()->rect().center());
        QVERIFY2(QLineF(c, center0).length() < 2.0 / view.transform().m11(),
                 qPrintable(QStringLiteral("center moved by %1").arg(QLineF(c, center0).length())));
    }
    QMouseEvent release(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QVERIFY(!qFuzzyCompare(view.orbitYawDeg(), yaw));
    QCOMPARE(view.sceneRect(), rect0);

    // Visibility checkbox: hidden layer disappears from the 3D image at once.
    view.setLayerVisible(1, false);
    QVERIFY(iso3dPixmapAlpha(view) <= 0);
    view.setLayerVisible(1, true);
    QVERIFY(iso3dPixmapAlpha(view) > alphaDefault);

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

namespace {

/*! Highest Z value (paint order) of the polygons named \a name; -1 if none. */
qreal topPaintZ(LayoutView &view, const QString &name)
{
    qreal z = -1.0;
    for (QGraphicsItem *it : view.scene()->items()) {
        if (qgraphicsitem_cast<QGraphicsPolygonItem *>(it) && it->data(0).toString() == name)
            z = qMax(z, it->zValue());
    }
    return z;
}

} // namespace

void LayoutViewTest::iso3d_viewFromBelow_reversesStackOrder()
{
    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    const QVariant prev3d = settings.value(QStringLiteral("view3d"));
    const QVariant prevField = settings.value(QStringLiteral("viewField"));
    settings.setValue(QStringLiteral("view3d"), false);
    settings.setValue(QStringLiteral("viewField"), false);
    settings.endGroup();

    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);

    // Two stacked, overlapping metals: few faces, so they stay individual scene items.
    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, 0, 0, 20, 20) << makeRect(2, 5, 5, 25, 25);
    QHash<int, LayoutView::LayerStyle> styles;
    auto low = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    low.hasZ = true;
    low.zminUm = 0.0;
    low.zmaxUm = 0.5;
    auto high = style(QStringLiteral("M2"), QStringLiteral("conductor"), QColor(40, 120, 200), 20);
    high.hasZ = true;
    high.zminUm = 2.0;
    high.zmaxUm = 2.5;
    styles.insert(1, low);
    styles.insert(2, high);
    view.setPolygons(polys, styles);
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(!view.lastIso3dRebuildStats().usedPixmap);

    // From above (default pitch 30°): the upper metal is painted last (nearest the camera).
    QVERIFY(view.orbitPitchDeg() > 0);
    QVERIFY(topPaintZ(view, QStringLiteral("M2")) > topPaintZ(view, QStringLiteral("M1")));

    // Dragging down orbits below the layout: now the lower metal is nearest and on top.
    sendDrag(view, Qt::LeftButton, Qt::ControlModifier, QPointF(200, 100), QPointF(200, 260));
    QVERIFY(view.orbitPitchDeg() < 0);
    QVERIFY(topPaintZ(view, QStringLiteral("M1")) > topPaintZ(view, QStringLiteral("M2")));

    // Shift+Z jumps to the view from below.
    sendKey(view, Qt::Key_I);
    QVERIFY(view.orbitPitchDeg() > 0);
    sendKey(view, Qt::Key_Z, Qt::ShiftModifier);
    QVERIFY(view.isView3d());
    QCOMPARE(view.orbitPitchDeg(), -85.0);
    QVERIFY(topPaintZ(view, QStringLiteral("M1")) > topPaintZ(view, QStringLiteral("M2")));

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
