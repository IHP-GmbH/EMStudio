/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#include "tst_layout_view.h"
#include "appsettings.h"

#include <QtTest/QtTest>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QToolButton>
#include <QCheckBox>
#include <QLabel>
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

    auto fillAlpha = [&view]() {   // the fill (in the layout fill group), not the outline
        for (QGraphicsItem *it : view.scene()->items())
            if (auto *poly = qgraphicsitem_cast<QGraphicsPolygonItem *>(it))
                if (poly->parentItem())
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
    // Field on: the 2D / 3D switch is replaced by the "3D viewer" launch button.
    auto *modeSwitch = view.findChild<QWidget *>(QStringLiteral("layoutViewModeSwitch"));
    auto *viewerBtn = view.findChild<QToolButton *>(QStringLiteral("layoutViewField3dBtn"));
    QVERIFY(modeSwitch && viewerBtn);
    QVERIFY(modeSwitch->isHidden());
    QVERIFY(!viewerBtn->isHidden());
    QVERIFY(!viewerBtn->isCheckable());
    QVERIFY(viewerBtn->text().contains(QStringLiteral("3D viewer")));
    QVERIFY(!viewerBtn->icon().isNull());
    viewerBtn->click();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!view.isView3d());
    QVERIFY(view.isFieldMode());

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
    view.setMaxViaPolygonsPerLayer(1000000);  // this test measures drawing thousands of vias

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
    view.setMaxViaPolygonsPerLayer(1000000);  // this test measures drawing thousands of vias
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

    // The 2D / 3D switch tooltips follow the style.
    auto *btn3d = view.findChild<QToolButton *>(QStringLiteral("layoutViewMode3dBtn"));
    QVERIFY(btn3d);
    QVERIFY(btn3d->toolTip().contains(QStringLiteral("setupEM")));
    view.setNavigationStyle(NavStyle::EMStudio);
    QVERIFY(btn3d->toolTip().contains(QStringLiteral("EMStudio")));

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

/*! Iso3D draws each port's surface (as gds2palace builds it) and points via-port arrows
 *  from From to To, reversed by -z (swapping From/To reverses it as well). */
void LayoutViewTest::iso3d_portsShowSurfaceAndFromToDirection()
{
    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, 0, 0, 10, 8) << makeRect(2, 2, 2, 6, 6);
    polys << makeRect(201, 1, 1, 1, 4);   // via port: zero-width line along y
    polys << makeRect(202, 7, 1, 9, 3);   // in-plane port with area
    QHash<int, LayoutView::LayerStyle> styles;
    auto s1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    s1.hasZ = true;
    s1.zminUm = 0.0;
    s1.zmaxUm = 0.5;
    auto s2 = style(QStringLiteral("M2"), QStringLiteral("conductor"), QColor(40, 120, 200), 20);
    s2.hasZ = true;
    s2.zminUm = 3.0;
    s2.zmaxUm = 3.4;
    styles.insert(1, s1);
    styles.insert(2, s2);

    auto layer = [](LayoutView::PortInfo &p, bool from, double z0, double z1) {
        if (from) {
            p.fromZminUm = z0; p.fromZmaxUm = z1; p.hasFromRange = true;
            p.zFromUm = z1; p.hasFromZ = true;
        } else {
            p.toZminUm = z0; p.toZmaxUm = z1; p.hasToRange = true;
            p.zToUm = 0.5 * (z0 + z1); p.hasToZ = true;
        }
    };
    // Vertical arrow direction of port P1 in scene space: <0 = points up on screen.
    auto arrowDy = [&](const QString &dir, bool fromM1) -> qreal {
        QHash<int, LayoutView::PortInfo> ports;
        LayoutView::PortInfo via;
        via.direction = dir;
        via.fromLayer = fromM1 ? QStringLiteral("M1") : QStringLiteral("M2");
        via.toLayer = fromM1 ? QStringLiteral("M2") : QStringLiteral("M1");
        layer(via, true, fromM1 ? 0.0 : 3.0, fromM1 ? 0.5 : 3.4);
        layer(via, false, fromM1 ? 3.0 : 0.0, fromM1 ? 3.4 : 0.5);
        ports.insert(201, via);
        LayoutView::PortInfo inPlane;
        inPlane.direction = QStringLiteral("-x");
        inPlane.toLayer = QStringLiteral("M2");
        layer(inPlane, false, 3.0, 3.4);
        ports.insert(202, inPlane);
        view.setPolygons(polys, styles, ports);
        view.setViewMode(LayoutView::ViewMode::Iso3D);

        qreal dy = 0.0;
        for (QGraphicsItem *item : view.scene()->items()) {
            auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item);
            if (line && item->data(4).toBool() && item->data(3).toInt() == 201
                && item->toolTip().contains(QStringLiteral("direction")))
                dy = line->line().p2().y() - line->line().p1().y();  // tail -> tip
        }
        return dy;
    };

    QVERIFY(arrowDy(QStringLiteral("z"), true) < 0);    // M1 -> M2: up
    QVERIFY(arrowDy(QStringLiteral("-z"), true) > 0);   // reversed: down
    QVERIFY(arrowDy(QStringLiteral("z"), false) > 0);   // M2 -> M1: down
    QVERIFY(arrowDy(QStringLiteral("-z"), false) < 0);  // both swapped: up

    // Port surfaces: the via sheet (vertical, has area in Iso3D) and the in-plane rectangle.
    QSet<int> surfaces;
    for (QGraphicsItem *item : view.scene()->items())
        if (qgraphicsitem_cast<QGraphicsPolygonItem *>(item) && item->data(4).toBool()
            && item->toolTip().contains(QStringLiteral("surface")))
            surfaces.insert(item->data(3).toInt());
    QCOMPARE(surfaces, QSet<int>({201, 202}));
}

/*!*******************************************************************************************************************
 * \brief A stackup layer on a GDS number in 201–299 (a ground sheet on 250) is drawn as a layer in
 *        Iso3D, not as a port surface.
 **********************************************************************************************************************/
void LayoutViewTest::iso3d_stackupLayerInPortRange_isNoPort()
{
    LayoutView view;
    view.resize(400, 300);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, 0, 0, 10, 8) << makeRect(250, -5, -10, 15, -4);
    QHash<int, LayoutView::LayerStyle> styles;
    auto s1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    s1.hasZ = true;
    s1.zminUm = 2.0;
    s1.zmaxUm = 2.5;
    auto gnd = style(QStringLiteral("SUBGND"), QStringLiteral("sheet"), QColor(120, 120, 140), 0);
    gnd.hasZ = true;
    gnd.zminUm = 0.0;
    gnd.zmaxUm = 0.0;
    styles.insert(1, s1);
    styles.insert(250, gnd);
    view.setPolygons(polys, styles);
    view.setViewMode(LayoutView::ViewMode::Iso3D);

    int faces = 0;
    for (QGraphicsItem *item : view.scene()->items()) {
        if (item->data(3).toInt() != 250)
            continue;
        QVERIFY2(!item->data(4).toBool(), "SUBGND drawn as a port");
        if (auto *poly = qgraphicsitem_cast<QGraphicsPolygonItem *>(item))
            if (poly->polygon().boundingRect().height() > 1.0)
                ++faces;
    }
    QVERIFY(faces > 0);
}

/*!*******************************************************************************************************************
 * \brief Field view: layout shapes start as outlines in their layer color (fill 0), so stacked layers (thermal
 *        stacks) don't hide the heatmap; its opacity is separate from the layout view's, and port markers stay
 *        at full strength.
 **********************************************************************************************************************/
void LayoutViewTest::layoutOpacity_fadesStackAsOneImage()
{
    LayoutView view;
    view.resize(400, 300);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);
    view.setViewMode(LayoutView::ViewMode::Top2D);

    // 21 stacked layers (like a thermal stack) and a port line.
    QVector<GdsFlatPolygon> polys;
    QHash<int, LayoutView::LayerStyle> styles;
    for (int l = 1; l <= 21; ++l) {
        polys << makeRect(l, 0, 0, 100, 100);
        styles.insert(l, style(QStringLiteral("L%1").arg(l), QStringLiteral("conductor"),
                               QColor::fromHsv((l * 37) % 360, 200, 200), l));
    }
    polys << makeRect(201, 20, 20, 20, 60);
    view.setPolygons(polys, styles);
    QTest::qWait(20);

    // Fills are children of one faded group; outlines carry the names, have no fill, stay on top.
    auto fillGroup = [&view]() -> QGraphicsItem * {
        for (QGraphicsItem *it : view.scene()->items())
            if (it->data(7).toBool())
                return it;
        return nullptr;
    };
    QVERIFY(fillGroup());
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(fillGroup()->graphicsEffect());
    QVERIFY(effect);
    QCOMPARE(fillGroup()->childItems().size(), 21);
    int outlines = 0;
    for (QGraphicsItem *it : view.scene()->items())
        if (auto *p = qgraphicsitem_cast<QGraphicsPolygonItem *>(it))
            if (!p->parentItem() && p->data(3).toInt() <= 21) {
                ++outlines;
                QCOMPARE(p->brush().style(), Qt::NoBrush);
                QVERIFY(!p->data(0).toString().isEmpty());
            }
    QCOMPARE(outlines, 21);

    // The center pixel moves from the background to the stack color in proportion to the slider,
    // however many layers overlap.
    auto center = [&view]() {
        const QImage img = view.viewport()->grab().toImage();
        return QColor(img.pixel(view.mapFromScene(QPointF(70, -70))));
    };
    auto dist = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
    };
    view.setLayoutOpacity(0.0);
    QCOMPARE(view.layoutOpacity(), 0.0);
    const QColor bg = center();
    view.setLayoutOpacity(1.0);
    const QColor full = center();
    const int span = dist(full, bg);
    QVERIFY2(span > 60, qPrintable(QStringLiteral("span %1").arg(span)));
    view.setLayoutOpacity(0.5);
    const qreal half = dist(center(), bg) / qreal(span);
    QVERIFY2(half > 0.35 && half < 0.65, qPrintable(QString::number(half)));
    view.setLayoutOpacity(0.2);
    const qreal fifth = dist(center(), bg) / qreal(span);
    QVERIFY2(fifth > 0.1 && fifth < 0.3, qPrintable(QString::number(fifth)));
    // Per-layer values are untouched by the layout opacity.
    QCOMPARE(view.layerOpacity(3), LayoutView::defaultFillOpacity());

    auto portLine = [&view]() -> QGraphicsLineItem * {
        for (QGraphicsItem *it : view.scene()->items())
            if (auto *l = qgraphicsitem_cast<QGraphicsLineItem *>(it))
                if (l->data(3).toInt() == 201 && l->data(4).toBool())
                    return l;
        return nullptr;
    };
    auto outlineOf = [&view](int gds) -> QGraphicsPolygonItem * {
        for (QGraphicsItem *it : view.scene()->items())
            if (auto *p = qgraphicsitem_cast<QGraphicsPolygonItem *>(it))
                if (!p->parentItem() && p->data(3).toInt() == gds)
                    return p;
        return nullptr;
    };

    // Field view: own value, starting at 0 (outlines in layer color over the heatmap).
    view.setFieldMode(true);
    QVERIFY(view.isFieldMode());
    QCOMPARE(view.layoutOpacity(), 0.0);
    QCOMPARE(qobject_cast<QGraphicsOpacityEffect *>(fillGroup()->graphicsEffect())->opacity(), 0.0);
    QCOMPARE(outlineOf(2)->pen().color().rgb(), styles.value(2).color.rgb());
    QVERIFY(portLine());
    QCOMPARE(portLine()->pen().color().alpha(), 255);
    view.setLayoutOpacity(0.6);
    QCOMPARE(view.layoutOpacity(), 0.6);

    // Back in the layout view: its own value again.
    view.setFieldMode(false);
    QCOMPARE(view.layoutOpacity(), 0.2);
    QCOMPARE(qobject_cast<QGraphicsOpacityEffect *>(fillGroup()->graphicsEffect())->opacity(), 0.2);
    QCOMPARE(outlineOf(1)->pen().color().rgb(), QColor(20, 20, 20).rgb());

    // Highlight and restore keep the outline unfilled.
    view.setHighlightedLayer(QStringLiteral("L5"));
    QVERIFY(outlineOf(5)->brush().style() != Qt::NoBrush);
    view.clearHighlight();
    QCOMPARE(outlineOf(5)->brush().style(), Qt::NoBrush);
    QCOMPARE(outlineOf(5)->pen().color().rgb(), QColor(20, 20, 20).rgb());

    // Iso3D: the faces are faded by the same value.
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    bool faded = false;
    for (QGraphicsItem *it : view.scene()->items())
        if (it->data(7).toBool()) {
            faded = true;
            auto *e = qobject_cast<QGraphicsOpacityEffect *>(it->graphicsEffect());
            QCOMPARE(e ? e->opacity() : it->opacity(), 0.2);
        }
    QVERIFY(faded);
    view.setLayoutOpacity(0.7);
    for (QGraphicsItem *it : view.scene()->items())
        if (it->data(7).toBool()) {
            auto *e = qobject_cast<QGraphicsOpacityEffect *>(it->graphicsEffect());
            QCOMPARE(e ? e->opacity() : it->opacity(), 0.7);
        }
    view.setViewMode(LayoutView::ViewMode::Top2D);
}

/*! Signed shoelace area sum of one layer's drawn polygons [µm²]. */
static qreal drawnArea(const LayoutView &view, int layer, int *count = nullptr)
{
    qreal area = 0.0;
    int n = 0;
    for (const GdsFlatPolygon &p : view.drawnPolygons()) {
        if (p.layer != layer)
            continue;
        ++n;
        qreal a = 0.0;
        for (int i = 0; i < p.pointsUm.size(); ++i) {
            const QPointF &u = p.pointsUm.at(i);
            const QPointF &v = p.pointsUm.at((i + 1) % p.pointsUm.size());
            a += u.x() * v.y() - v.x() * u.y();
        }
        area += std::abs(a) / 2.0;
    }
    if (count)
        *count = n;
    return area;
}

/*! Via layers are merged like gds2palace's merge_via_array (grow spacing/2 + 0.01, unite, shrink)
 *  with the model's merge_polygon_size, in 2D and 3D; 0 shows single vias. */
void LayoutViewTest::viaMerge_followsModelMergeSize()
{
    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    if (view.isFieldMode())
        view.setFieldMode(false);

    // 4 x 3 array of 0.19 µm vias with 0.22 µm gaps, plus an L of vias and a lone via far away.
    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, -5, -5, 20, 20);
    for (int ix = 0; ix < 4; ++ix)
        for (int iy = 0; iy < 3; ++iy)
            polys << makeRect(29, ix * 0.41, iy * 0.41, ix * 0.41 + 0.19, iy * 0.41 + 0.19);
    for (int k = 0; k < 4; ++k)  // L: 4 along x, 3 more up from the first
        polys << makeRect(29, 10 + k * 0.41, 0, 10 + k * 0.41 + 0.19, 0.19);
    for (int k = 1; k <= 3; ++k)
        polys << makeRect(29, 10, k * 0.41, 10.19, k * 0.41 + 0.19);
    polys << makeRect(29, 18, 18, 18.19, 18.19);

    QHash<int, LayoutView::LayerStyle> styles;
    styles.insert(1, style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10));
    styles.insert(29, style(QStringLiteral("Via2"), QStringLiteral("via"), QColor(40, 120, 200), 20));

    // Not set: 2D draws every via.
    view.setPolygons(polys, styles);
    int n = 0;
    drawnArea(view, 29, &n);
    QCOMPARE(n, 4 * 3 + 7 + 1);

    // 0.75 µm: array -> its bounding box, L -> L shape (concave corner stays empty), lone via kept.
    view.setViaMergeSize(0.75);
    const qreal merged = drawnArea(view, 29, &n);
    const qreal array = (3 * 0.41 + 0.19) * (2 * 0.41 + 0.19);
    const qreal lShape = (3 * 0.41 + 0.19) * 0.19 + 0.19 * (3 * 0.41);
    const qreal lone = 0.19 * 0.19;
    QVERIFY2(std::abs(merged - (array + lShape + lone)) < 0.01,
             qPrintable(QStringLiteral("area %1, expected %2").arg(merged).arg(array + lShape + lone)));
    QCOMPARE(n, 3);

    // Gaps larger than the merge distance: nothing merges.
    view.setViaMergeSize(0.1);
    drawnArea(view, 29, &n);
    QCOMPARE(n, 4 * 3 + 7 + 1);

    // The conductor layer is never touched; 3D uses the same merged vias.
    view.setViaMergeSize(0.75);
    QCOMPARE(drawnArea(view, 1), 25.0 * 25.0);
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QCOMPARE(view.lastIso3dRebuildStats().viaPolyCount, 3);
    view.setViewMode(LayoutView::ViewMode::Top2D);
}

/*! A via layer with more drawn polygons than the limit shows a message instead of the layout
 *  (2D and 3D); merging the vias or raising the limit draws it again. */
void LayoutViewTest::viaLimit_showsMessageInsteadOfLayout()
{
    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(500, 400);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, -5, -5, 40, 40);
    for (int ix = 0; ix < 15; ++ix)          // 150 vias, 0.19 µm with 0.22 µm gaps
        for (int iy = 0; iy < 10; ++iy)
            polys << makeRect(29, ix * 0.41, iy * 0.41, ix * 0.41 + 0.19, iy * 0.41 + 0.19);
    QHash<int, LayoutView::LayerStyle> styles;
    styles.insert(1, style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10));
    styles.insert(29, style(QStringLiteral("Via2"), QStringLiteral("via"), QColor(40, 120, 200), 20));

    view.setViaMergeSize(-1.0);
    view.setPolygons(polys, styles);  // default limit 100
    QCOMPARE(view.denseViaLayers(), QStringList{QStringLiteral("Via2: 150")});
    auto *label = view.findChild<QLabel *>(QStringLiteral("layoutViewDenseViaLabel"));
    QVERIFY(label && label->isVisible());
    QVERIFY(label->text().contains(QStringLiteral("Via2: 150")));
    QCOMPARE(view.scene()->items().size(), 0);
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QCOMPARE(view.scene()->items().size(), 0);
    QVERIFY(label->isVisible());
    view.setViewMode(LayoutView::ViewMode::Top2D);

    // The model's merge_polygon_size merges the array into one shape: drawn again.
    view.setViaMergeSize(0.75);
    QVERIFY(view.denseViaLayers().isEmpty());
    QVERIFY(!label->isVisible());
    QVERIFY(view.scene()->items().size() > 0);

    // A higher limit (Preferences) also draws the single vias.
    view.setViaMergeSize(0.0);
    QVERIFY(label->isVisible());
    view.setMaxViaPolygonsPerLayer(200);
    QVERIFY(!label->isVisible());
    QVERIFY(view.scene()->items().size() > 150);
}

/*! The 2D / 3D switch shows both options, exactly one highlighted, and clicking a side switches. */
void LayoutViewTest::modeSwitch_showsBothOptionsWithActiveHighlighted()
{
    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(400, 300);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);
    view.setViewMode(LayoutView::ViewMode::Top2D);

    QVector<GdsFlatPolygon> polys;
    polys << makeRect(1, 0, 0, 10, 8);
    QHash<int, LayoutView::LayerStyle> styles;
    auto s1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
    s1.hasZ = true;
    s1.zmaxUm = 0.5;
    styles.insert(1, s1);
    view.setPolygons(polys, styles);

    auto *modeSwitch = view.findChild<QWidget *>(QStringLiteral("layoutViewModeSwitch"));
    auto *b2 = view.findChild<QToolButton *>(QStringLiteral("layoutViewMode2dBtn"));
    auto *b3 = view.findChild<QToolButton *>(QStringLiteral("layoutViewMode3dBtn"));
    QVERIFY(modeSwitch && b2 && b3);
    QVERIFY(!modeSwitch->isHidden());
    QCOMPARE(b2->text(), QStringLiteral("2D"));
    QCOMPARE(b3->text(), QStringLiteral("3D"));
    QVERIFY(b2->isChecked() && !b3->isChecked());

    b3->click();
    QVERIFY(view.isView3d());
    QVERIFY(!b2->isChecked() && b3->isChecked());
    b3->click();  // clicking the active side keeps it
    QVERIFY(view.isView3d());
    b2->click();
    QVERIFY(!view.isView3d());
    QVERIFY(b2->isChecked() && !b3->isChecked());

    // Keys keep the switch in sync.
    QKeyEvent key3(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier);
    QApplication::sendEvent(&view, &key3);
    QVERIFY(b3->isChecked());
    view.setViewMode(LayoutView::ViewMode::Top2D);
}

void LayoutViewTest::iso3d_thermalMarkersFollowStackOrder()
{
    LayoutView view;
    view.setAttribute(Qt::WA_DontShowOnScreen, true);
    view.resize(500, 400);
    view.show();
    if (view.isFieldMode())
        view.setFieldMode(false);

    // M1 (z 0..2) and M2 (z 10..12); a constant temperature at z = -5 below the stack and a heat
    // source in M1. dense = 10 x 10 M1 squares, so the faces are drawn as pixmaps.
    auto build = [&view](bool dense) {
        QVector<GdsFlatPolygon> polys;
        if (dense) {
            for (int i = 0; i < 10; ++i)
                for (int j = 0; j < 10; ++j)
                    polys << makeRect(1, i * 10.0, j * 10.0, i * 10.0 + 6.0, j * 10.0 + 6.0);
        } else {
            polys << makeRect(1, 0, 0, 100, 100);
        }
        polys << makeRect(2, 0, 0, 100, 100) << makeRect(201, 20, 20, 40, 40)
              << makeRect(202, -10, -10, 110, 110);
        QHash<int, LayoutView::LayerStyle> styles;
        auto m1 = style(QStringLiteral("M1"), QStringLiteral("conductor"), QColor(200, 80, 40), 10);
        m1.hasZ = true;
        m1.zminUm = 0.0;
        m1.zmaxUm = 2.0;
        auto m2 = style(QStringLiteral("M2"), QStringLiteral("conductor"), QColor(40, 120, 200), 20);
        m2.hasZ = true;
        m2.zminUm = 10.0;
        m2.zmaxUm = 12.0;
        styles.insert(1, m1);
        styles.insert(2, m2);
        styles.insert(201, style(QStringLiteral("Heat 0.1 W"), QStringLiteral("port"), Qt::red, 100));
        styles.insert(202, style(QStringLiteral("T 300 K"), QStringLiteral("port"), Qt::blue, 101));
        QHash<int, LayoutView::PortInfo> ports;
        LayoutView::PortInfo heat;
        heat.thermalKind = QStringLiteral("heatsource");
        heat.hasToRange = true;
        heat.toZminUm = 0.0;
        heat.toZmaxUm = 2.0;
        ports.insert(201, heat);
        LayoutView::PortInfo temp;
        temp.thermalKind = QStringLiteral("consttemp");
        temp.hasToRange = true;
        temp.toZminUm = -5.0;
        temp.toZmaxUm = -5.0;
        ports.insert(202, temp);
        view.setPolygons(polys, styles, ports);
    };

    // z-value range of the marker surfaces (polygons) of a GDS layer.
    auto markerZ = [&view](int gds, qreal *lo, qreal *hi) {
        *lo = 1e300;
        *hi = -1e300;
        for (QGraphicsItem *it : view.scene()->items())
            if (qgraphicsitem_cast<QGraphicsPolygonItem *>(it) && it->data(3).toInt() == gds
                && it->data(4).toBool()) {
                *lo = qMin(*lo, it->zValue());
                *hi = qMax(*hi, it->zValue());
            }
        return *lo <= *hi;
    };
    auto faceZ = [&view](int gds, qreal *lo, qreal *hi) {
        *lo = 1e300;
        *hi = -1e300;
        for (QGraphicsItem *it : view.scene()->items())
            if (qgraphicsitem_cast<QGraphicsPolygonItem *>(it) && it->data(3).toInt() == gds
                && !it->data(4).toBool()) {
                *lo = qMin(*lo, it->zValue());
                *hi = qMax(*hi, it->zValue());
            }
        return *lo <= *hi;
    };

    // Individual faces, seen from above: T below M1, heat source over M1 and under M2.
    build(false);
    view.setViewMode(LayoutView::ViewMode::Iso3D);
    QVERIFY(!view.lastIso3dRebuildStats().usedPixmap);
    qreal tLo, tHi, hLo, hHi, m1Lo, m1Hi, m2Lo, m2Hi;
    QVERIFY(markerZ(202, &tLo, &tHi));
    QVERIFY(markerZ(201, &hLo, &hHi));
    QVERIFY(faceZ(1, &m1Lo, &m1Hi));
    QVERIFY(faceZ(2, &m2Lo, &m2Hi));
    QVERIFY(tHi < m1Lo);
    QVERIFY(hLo > m1Hi);
    QVERIFY(hHi < m2Lo);

    // From below the stack order reverses: M2 first, then the heat source after M1, T last.
    sendDrag(view, Qt::LeftButton, Qt::ControlModifier, QPointF(200, 100), QPointF(200, 260));
    QVERIFY(view.orbitPitchDeg() < 0.0);
    QVERIFY(markerZ(202, &tLo, &tHi));
    QVERIFY(markerZ(201, &hLo, &hHi));
    QVERIFY(faceZ(1, &m1Lo, &m1Hi));
    QVERIFY(faceZ(2, &m2Lo, &m2Hi));
    QVERIFY(m2Hi < m1Lo);
    QVERIFY(hLo > m1Hi);
    QVERIFY(tLo > hHi);

    // Pixmaps: split at the heat source, so it is drawn between the M1 and M2 pixmaps.
    sendKey(view, Qt::Key_I);
    QVERIFY(view.orbitPitchDeg() > 0.0);
    build(true);
    QVERIFY(view.lastIso3dRebuildStats().usedPixmap);
    QVector<qreal> pixZ;
    for (QGraphicsItem *it : view.scene()->items())
        if (qgraphicsitem_cast<QGraphicsPixmapItem *>(it))
            pixZ << it->zValue();
    std::sort(pixZ.begin(), pixZ.end());
    QCOMPARE(pixZ.size(), 2);
    QVERIFY(markerZ(202, &tLo, &tHi));
    QVERIFY(markerZ(201, &hLo, &hHi));
    QVERIFY(tHi < pixZ.at(0));
    QVERIFY(hLo > pixZ.at(0) && hHi < pixZ.at(1));

    // Labels stay on top.
    for (QGraphicsItem *it : view.scene()->items())
        if (qgraphicsitem_cast<QGraphicsSimpleTextItem *>(it) && it->data(4).toBool())
            QVERIFY(it->zValue() >= 1e9);
}
