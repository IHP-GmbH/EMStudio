/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 ************************************************************************/

#include "layoutview.h"

#include <algorithm>

#include <QPen>
#include <QBrush>
#include <QSet>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QScrollBar>
#include <QPainter>
#include <QAbstractGraphicsShapeItem>
#include <QGraphicsItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QLineF>
#include <QFont>
#include <QFontMetrics>
#include <QTransform>
#include <QVariant>
#include <QToolButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QNativeGestureEvent>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QtGlobal>
#include <QtMath>
#include <cmath>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \brief Constructs the LayoutView used for GDS top-view preview on the Substrate tab.
 *
 * Creates an empty QGraphicsScene, enables anti-aliasing and scroll-hand panning,
 * and uses a white background. Zoom anchors under the mouse cursor.
 *
 * \param parent Parent widget (optional).
 **********************************************************************************************************************/
LayoutView::LayoutView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(Qt::white);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->setCursor(Qt::ArrowCursor);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    grabGesture(Qt::PinchGesture);
    viewport()->grabGesture(Qt::PinchGesture);

    m_modeBtn = new QToolButton(this);
    m_modeBtn->setObjectName(QStringLiteral("layoutViewModeBtn"));
    m_modeBtn->setCheckable(true);
    m_modeBtn->setAutoRaise(false);
    m_modeBtn->setCursor(Qt::PointingHandCursor);
    m_modeBtn->setFixedSize(40, 26);
    m_modeBtn->setStyleSheet(
        QStringLiteral(
            "QToolButton {"
            "  background: rgba(255,255,255,220);"
            "  border: 1px solid #7a7a7a;"
            "  border-radius: 4px;"
            "  font-weight: bold;"
            "  font-size: 11px;"
            "}"
            "QToolButton:checked {"
            "  background: rgba(40,100,180,210);"
            "  color: white;"
            "  border-color: #245a9e;"
            "}"
            "QToolButton:disabled {"
            "  background: rgba(220,220,220,200);"
            "  color: #888;"
            "}"));
    connect(m_modeBtn, &QToolButton::toggled, this, &LayoutView::onModeButtonToggled);

    m_fieldBtn = new QToolButton(this);
    m_fieldBtn->setObjectName(QStringLiteral("layoutViewFieldBtn"));
    m_fieldBtn->setCheckable(true);
    m_fieldBtn->setAutoRaise(false);
    m_fieldBtn->setCursor(Qt::PointingHandCursor);
    m_fieldBtn->setFixedSize(48, 26);
    m_fieldBtn->setText(QStringLiteral("Field"));
    m_fieldBtn->setStyleSheet(m_modeBtn->styleSheet());
    m_fieldBtn->setToolTip(tr("Field view: Z-clip heatmap + optional arrows.\n"
                              "While Field is on, 3D opens an interactive PyVista volume window.\n"
                              "Requires a field dump (fdump / VTK / VTU)."));
    connect(m_fieldBtn, &QToolButton::toggled, this, &LayoutView::onFieldButtonToggled);

    m_fieldPanel = new QWidget(this);
    m_fieldPanel->setObjectName(QStringLiteral("layoutViewFieldPanel"));
    m_fieldPanel->setStyleSheet(
        QStringLiteral(
            "QWidget#layoutViewFieldPanel {"
            "  background: rgba(255,255,255,230);"
            "  border: 1px solid #9a9a9a;"
            "  border-radius: 4px;"
            "}"
            "QLabel { font-size: 10px; color: #333; }"
            "QCheckBox { font-size: 10px; }"));
    auto *panelLay = new QVBoxLayout(m_fieldPanel);
    panelLay->setContentsMargins(6, 3, 6, 3);
    panelLay->setSpacing(2);
    m_fieldStatusLbl = new QLabel(m_fieldPanel);
    m_fieldStatusLbl->setObjectName(QStringLiteral("layoutViewFieldStatus"));
    m_fieldStatusLbl->setWordWrap(false);
    m_fieldStatusLbl->setFixedWidth(168);
    m_fieldStatusLbl->setMaximumHeight(16);
    m_fieldStatusLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    panelLay->addWidget(m_fieldStatusLbl);
    auto *zRow = new QHBoxLayout;
    zRow->setSpacing(4);
    zRow->addWidget(new QLabel(tr("Z"), m_fieldPanel));
    m_fieldZSlider = new QSlider(Qt::Horizontal, m_fieldPanel);
    m_fieldZSlider->setRange(0, 1000);
    m_fieldZSlider->setValue(500);
    m_fieldZSlider->setFixedWidth(120);
    zRow->addWidget(m_fieldZSlider, 1);
    m_fieldHotZBtn = new QToolButton(m_fieldPanel);
    m_fieldHotZBtn->setObjectName(QStringLiteral("layoutViewFieldHotZBtn"));
    m_fieldHotZBtn->setText(tr("Max"));
    m_fieldHotZBtn->setAutoRaise(false);
    m_fieldHotZBtn->setCursor(Qt::PointingHandCursor);
    m_fieldHotZBtn->setFixedSize(34, 20);
    m_fieldHotZBtn->setStyleSheet(
        QStringLiteral("QToolButton { font-size: 10px; padding: 0 2px; }"));
    m_fieldHotZBtn->setToolTip(tr("Jump to hottest Z (max temperature or |E| in the layout ROI)."));
    zRow->addWidget(m_fieldHotZBtn);
    panelLay->addLayout(zRow);
    auto *optRow = new QHBoxLayout;
    optRow->setSpacing(8);
    m_fieldLogChk = new QCheckBox(tr("Log"), m_fieldPanel);
    m_fieldArrowsChk = new QCheckBox(tr("Arrows"), m_fieldPanel);
    m_fieldArrowsChk->setChecked(true);
    optRow->addWidget(m_fieldLogChk);
    optRow->addWidget(m_fieldArrowsChk);
    optRow->addStretch(1);
    panelLay->addLayout(optRow);
    m_fieldPanel->setVisible(false);
    m_fieldPanel->adjustSize();

    connect(m_fieldZSlider, &QSlider::valueChanged, this, &LayoutView::onFieldZSliderPreview);
    connect(m_fieldZSlider, &QSlider::sliderReleased, this, &LayoutView::onFieldZSliderCommitted);
    connect(m_fieldHotZBtn, &QToolButton::clicked, this, &LayoutView::onFieldHotZClicked);
    connect(m_fieldLogChk, &QCheckBox::toggled, this, &LayoutView::onFieldControlsChanged);
    connect(m_fieldArrowsChk, &QCheckBox::toggled, this, &LayoutView::onFieldControlsChanged);

    loadViewModeFromSettings();
    {
        const QSignalBlocker block(m_modeBtn);
        m_modeBtn->setChecked(m_viewMode == ViewMode::Iso3D);
        m_modeBtn->setText(m_viewMode == ViewMode::Iso3D ? QStringLiteral("3D")
                                                         : QStringLiteral("2D"));
        m_modeBtn->setToolTip(m_viewMode == ViewMode::Iso3D
                                  ? tr("3D view (click for top view).\n"
                                       "Drag: orbit · Click: select · Two-finger scroll: orbit\n"
                                       "Alt+drag / Middle: pan · Pinch / Ctrl+scroll: zoom · R: reset")
                                  : tr("Top view (click for 3D).\n"
                                       "Drag: pan · Click: select · Pinch / Ctrl+scroll: zoom · F: fit layout · Home: full field"));
    }
    {
        const QSignalBlocker block(m_fieldBtn);
        m_fieldBtn->setChecked(m_fieldOn);
    }
    syncFloatingControls();
    m_modeBtn->raise();
    m_fieldBtn->raise();
    m_fieldPanel->raise();
    repositionFloatingControls();
    setContextMenuPolicy(Qt::NoContextMenu);
}

/*!*******************************************************************************************************************
 * \brief Clears all polygons, highlight state, Field overlay, and the scene rectangle.
 **********************************************************************************************************************/
void LayoutView::clear()
{
    m_polys.clear();
    m_styles.clear();
    m_ports.clear();
    m_highlightedName.clear();
    m_field = FieldOverlay{};
    m_zoomLocked = false;
    m_cursorValid = false;
    clearMeasure();
    m_scene->clear();
    m_scene->setSceneRect(QRectF());
    syncFloatingControls();
}

QPointF LayoutView::sceneToGdsUm(const QPointF &scenePt)
{
    return QPointF(scenePt.x(), -scenePt.y());
}

void LayoutView::setPixelOffset(QGraphicsItem *item, qreal dxPx, qreal dyPx)
{
    if (!item)
        return;
    // ItemIgnoresTransformations: local units are device pixels; keep scene anchor at setPos().
    item->setTransform(QTransform::fromTranslate(dxPx, dyPx));
}

/*!*******************************************************************************************************************
 * \brief Replaces the scene contents with flattened GDS polygons.
 *
 * Polygons are sorted by \c LayerStyle::order (lower first). Layers missing from
 * \a styles are drawn as port markers (thick magenta line + Pn label) for zero-width
 * GDS port footprints. GDS Y is flipped for Qt display. After drawing, the view fits
 * the content unless the user has zoomed.
 *
 * \param polys   Flattened polygons in micrometres (from GdsLayout::flattenTopCell).
 * \param styles  Map from GDS layer number to display style / stack name.
 **********************************************************************************************************************/
void LayoutView::setPolygons(const QVector<GdsFlatPolygon> &polys,
                             const QHash<int, LayerStyle> &styles,
                             const QHash<int, PortInfo> &ports)
{
    m_polys = polys;
    m_styles = styles;
    m_ports = ports;
    m_zoomLocked = false;
    rebuildScene();
}

void LayoutView::rebuildScene(bool refit)
{
    const QString keepHighlight = m_highlightedName;
    m_cursorValid = false;
    clearMeasure();
    m_scene->clear();
    m_scene->setSceneRect(QRectF());

    if (isFieldVolume()) {
        addFieldVolumeItems();
        m_highlightedName = keepHighlight;
        if (refit && !m_zoomLocked)
            fitPreferredContent();
        repositionFloatingControls();
        return;
    }

    if (m_viewMode == ViewMode::Iso3D)
        rebuildScene3D(refit);
    else
        rebuildScene2D(refit);

    m_highlightedName = keepHighlight;
    applyHighlight();
    repositionFloatingControls();
}

void LayoutView::rebuildScene2D(bool refit)
{
    addFieldOverlayItems();

    struct Item {
        GdsFlatPolygon poly;
        LayerStyle style;
    };
    QVector<Item> items;
    items.reserve(m_polys.size());

    // Content center (non-port) — Z-port arrows point inward to the injection edge.
    QPointF contentCenter(0, 0);
    int centerN = 0;

    for (const GdsFlatPolygon &p : m_polys) {
        LayerStyle st;
        if (m_styles.contains(p.layer)) {
            st = m_styles.value(p.layer);
        } else {
            const int portIdx = p.layer - 200;
            st.name = (portIdx >= 1 && portIdx <= 99)
                    ? QStringLiteral("P%1").arg(portIdx)
                    : QStringLiteral("L%1").arg(p.layer);
            st.kind = QStringLiteral("port");
            st.color = QColor(220, 40, 180);
            st.order = 10000 + p.layer;
        }
        items.push_back({p, st});

        const bool isPortLayer = (st.kind == QLatin1String("port"))
                || (p.layer >= 201 && p.layer <= 299);
        if (isPortLayer)
            continue;
        for (const QPointF &pt : p.pointsUm) {
            contentCenter += QPointF(pt.x(), -pt.y());
            ++centerN;
        }
    }
    if (centerN > 0)
        contentCenter /= centerN;

    std::stable_sort(items.begin(), items.end(),
                     [](const Item &a, const Item &b) {
                         return a.style.order < b.style.order;
                     });

    QRectF bounds;
    for (const Item &it : items) {
        if (it.poly.pointsUm.size() < 2)
            continue;

        // Flip Y so layout matches the usual GDS/KLayout top-view (Y up).
        QPolygonF drawn;
        drawn.reserve(it.poly.pointsUm.size());
        for (const QPointF &p : it.poly.pointsUm)
            drawn << QPointF(p.x(), -p.y());

        const QRectF bb = drawn.boundingRect();
        const bool thinX = bb.width() < 1e-4;
        const bool thinY = bb.height() < 1e-4;
        const bool isPort = (it.style.kind == QLatin1String("port")) || thinX || thinY;
        const qreal op = opacityFor(it.poly.layer);
        const bool vis = visibleFor(it.poly.layer);

        if (isPort && (thinX || thinY || drawn.size() < 3)) {
            // Zero-width via/in-plane port footprint → thick cosmetic line + label.
            QLineF line;
            if (thinX || (!thinY && bb.height() >= bb.width())) {
                const qreal x = bb.center().x();
                line = QLineF(x, bb.top(), x, bb.bottom());
            } else {
                const qreal y = bb.center().y();
                line = QLineF(bb.left(), y, bb.right(), y);
            }
            if (line.length() < 1e-6) {
                // Degenerate: synthesize a short stub so the port stays visible.
                const QPointF c = bb.isNull() ? drawn.value(0) : bb.center();
                line = QLineF(c.x(), c.y() - 0.5, c.x(), c.y() + 0.5);
            }

            QColor penColor = it.style.color;
            penColor.setAlpha(qBound(40, int(255 * op + 0.5), 255));
            QPen pen(penColor);
            pen.setCosmetic(true);
            pen.setWidth(kPortPenWidth);
            pen.setCapStyle(Qt::FlatCap);

            auto *lineItem = m_scene->addLine(line, pen);
            lineItem->setData(kRoleName, it.style.name);
            lineItem->setData(kRoleKind, QStringLiteral("port"));
            lineItem->setData(kRoleGds, it.poly.layer);
            lineItem->setData(kRoleIsPort, true);
            lineItem->setData(kRolePen, it.style.color);
            lineItem->setVisible(vis);
            lineItem->setZValue(double(it.style.order) + 0.25);

            const QPointF mid = line.pointAt(0.5);

            // Direction from Ports table: in-plane arrows; Z → inward tip on injection edge.
            QString dir = m_ports.value(it.poly.layer).direction.trimmed().toLower();
            if (dir.isEmpty())
                dir = QStringLiteral("z");
            if (dir.contains(QLatin1Char('z'))) {
                const QString zl = dir.startsWith(QLatin1Char('-'))
                        ? QStringLiteral("-z") : QStringLiteral("z");
                addPortInwardArrow(mid, contentCenter, it.style.color, it.style.name,
                                   QStringLiteral("port"), it.poly.layer, vis,
                                   double(it.style.order) + 0.4, zl);
            } else {
                QPointF dirScene(1, 0);
                QString dirLabel = dir;
                if (dir == QLatin1String("-x"))
                    dirScene = QPointF(-1, 0);
                else if (dir == QLatin1String("y") || dir == QLatin1String("+y")) {
                    dirScene = QPointF(0, -1); // GDS +Y → screen up
                    dirLabel = QStringLiteral("y");
                } else if (dir == QLatin1String("-y"))
                    dirScene = QPointF(0, 1);
                else {
                    dirScene = QPointF(1, 0);
                    dirLabel = QStringLiteral("x");
                }
                addPortArrow(mid, dirScene, it.style.color, it.style.name,
                             QStringLiteral("port"), it.poly.layer, vis,
                             double(it.style.order) + 0.4, dirLabel);
            }

            auto *label = m_scene->addSimpleText(it.style.name);
            QFont f = label->font();
            f.setBold(true);
            f.setPointSize(9);
            label->setFont(f);
            label->setBrush(it.style.color);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            label->setData(kRoleName, it.style.name);
            label->setData(kRoleKind, QStringLiteral("port"));
            label->setData(kRoleGds, it.poly.layer);
            label->setData(kRoleIsPort, true);
            label->setData(kRolePen, it.style.color);
            label->setVisible(vis);
            label->setZValue(double(it.style.order) + 0.5);
            label->setPos(mid);
            setPixelOffset(label, 8, -16);

            bounds |= QRectF(line.p1(), line.p2()).normalized().adjusted(-0.5, -0.5, 0.5, 0.5);
            bounds |= QRectF(mid.x() - 1, mid.y() - 1, 2, 2);
            continue;
        }

        if (drawn.size() < 3)
            continue;

        QColor fill = it.style.color;
        fill.setAlpha(qBound(0, int(kBaseFillAlpha * op + 0.5), 255));
        QPen outline(QColor(20, 20, 20), 0);
        if (isPort) {
            outline.setColor(it.style.color);
            outline.setCosmetic(true);
            outline.setWidth(2);
        }
        auto *item = m_scene->addPolygon(drawn, outline, QBrush(fill));
        item->setData(kRoleName, it.style.name);
        item->setData(kRoleKind, isPort ? QStringLiteral("port") : it.style.kind);
        item->setData(kRoleGds, it.poly.layer);
        item->setData(kRoleIsPort, isPort);
        item->setData(kRoleBrush, it.style.color);
        item->setVisible(vis);
        item->setZValue(double(it.style.order));
        bounds |= drawn.boundingRect();

        if (isPort) {
            auto *label = m_scene->addSimpleText(it.style.name);
            QFont f = label->font();
            f.setBold(true);
            f.setPointSize(9);
            label->setFont(f);
            label->setBrush(it.style.color);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            label->setData(kRoleName, it.style.name);
            label->setData(kRoleKind, QStringLiteral("port"));
            label->setData(kRoleGds, it.poly.layer);
            label->setData(kRoleIsPort, true);
            label->setData(kRolePen, it.style.color);
            label->setVisible(vis);
            label->setZValue(double(it.style.order) + 0.5);
            label->setPos(bb.center());
            setPixelOffset(label, 8, -16);

            QString dir = m_ports.value(it.poly.layer).direction.trimmed().toLower();
            if (dir.isEmpty())
                dir = QStringLiteral("x");

            // Via port with finite XY area: gds2palace collapses the shorter axis to its
            // *min* edge (not the geometric center). Draw that effective sheet as a dashed line.
            QPointF arrowOrigin = bb.center();
            if (dir.contains(QLatin1Char('z')) && bb.width() > 1e-4 && bb.height() > 1e-4) {
                // bb is in scene coords (Y-down). Recover GDS extents from drawn points.
                qreal gdsXmin = drawn.first().x();
                qreal gdsXmax = gdsXmin;
                qreal gdsYmin = -drawn.first().y();
                qreal gdsYmax = gdsYmin;
                for (const QPointF &pt : drawn) {
                    gdsXmin = qMin(gdsXmin, pt.x());
                    gdsXmax = qMax(gdsXmax, pt.x());
                    const qreal gy = -pt.y();
                    gdsYmin = qMin(gdsYmin, gy);
                    gdsYmax = qMax(gdsYmax, gy);
                }
                const qreal sizeX = gdsXmax - gdsXmin;
                const qreal sizeY = gdsYmax - gdsYmin;
                QLineF sheet;
                if (sizeY > sizeX)
                    sheet = QLineF(QPointF(gdsXmin, -gdsYmin), QPointF(gdsXmin, -gdsYmax));
                else
                    sheet = QLineF(QPointF(gdsXmin, -gdsYmin), QPointF(gdsXmax, -gdsYmin));

                QPen centerPen(it.style.color);
                centerPen.setCosmetic(true);
                centerPen.setWidth(4);
                centerPen.setStyle(Qt::DashLine);
                auto *centerLine = m_scene->addLine(sheet, centerPen);
                centerLine->setData(kRoleName, it.style.name);
                centerLine->setData(kRoleKind, QStringLiteral("port"));
                centerLine->setData(kRoleGds, it.poly.layer);
                centerLine->setData(kRoleIsPort, true);
                centerLine->setData(kRolePen, it.style.color);
                centerLine->setToolTip(tr("Effective via-port sheet — gds2palace pins the shorter axis to its min edge"));
                centerLine->setVisible(vis);
                centerLine->setZValue(double(it.style.order) + 0.35);
                arrowOrigin = sheet.pointAt(0.5);
            }

            if (dir.contains(QLatin1Char('z'))) {
                const QString zl = dir.startsWith(QLatin1Char('-'))
                        ? QStringLiteral("-z") : QStringLiteral("z");
                addPortInwardArrow(arrowOrigin, contentCenter, it.style.color, it.style.name,
                                   QStringLiteral("port"), it.poly.layer, vis,
                                   double(it.style.order) + 0.4, zl);
            } else {
                QPointF dirScene(1, 0);
                QString dirLabel = dir;
                if (dir == QLatin1String("-x"))
                    dirScene = QPointF(-1, 0);
                else if (dir == QLatin1String("y") || dir == QLatin1String("+y")) {
                    dirScene = QPointF(0, -1);
                    dirLabel = QStringLiteral("y");
                } else if (dir == QLatin1String("-y"))
                    dirScene = QPointF(0, 1);
                else {
                    dirScene = QPointF(1, 0);
                    dirLabel = QStringLiteral("x");
                }
                addPortArrow(arrowOrigin, dirScene, it.style.color, it.style.name,
                             QStringLiteral("port"), it.poly.layer, vis,
                             double(it.style.order) + 0.4, dirLabel);
            }
        }
    }

    if (m_fieldOn && m_field.valid() && m_field.showArrows && !m_field.arrows.isEmpty()) {
        qreal maxMag = 1e-30;
        for (const FieldArrow &a : m_field.arrows)
            maxMag = qMax(maxMag, qAbs(a.mag));
        const qreal extent = qMax(qAbs(m_field.xmaxUm - m_field.xminUm),
                                  qAbs(m_field.ymaxUm - m_field.yminUm));
        const qreal baseLen = qMax(0.5, extent * 0.04);
        for (const FieldArrow &a : m_field.arrows) {
            const qreal len = baseLen * qBound(0.25, qAbs(a.mag) / maxMag, 1.0);
            QPointF dir(a.dx, -a.dy); // GDS Y-up → scene Y-down
            const qreal n = std::hypot(dir.x(), dir.y());
            if (n < 1e-12)
                continue;
            dir /= n;
            const QPointF origin(a.xUm, -a.yUm);
            const QPointF tip = origin + dir * len;
            QPen pen(QColor(20, 20, 20, 200));
            pen.setWidthF(0);
            pen.setCosmetic(true);
            auto *shaft = m_scene->addLine(QLineF(origin, tip), pen);
            shaft->setZValue(20000);
            shaft->setFlag(QGraphicsItem::ItemIgnoresTransformations, false);
            // Arrowhead in scene units
            const QPointF ortho(-dir.y(), dir.x());
            const QPointF h1 = tip - dir * (len * 0.28) + ortho * (len * 0.18);
            const QPointF h2 = tip - dir * (len * 0.28) - ortho * (len * 0.18);
            QPolygonF head;
            head << tip << h1 << h2;
            auto *headItem = m_scene->addPolygon(head, Qt::NoPen, QBrush(QColor(20, 20, 20, 200)));
            headItem->setZValue(20001);
        }
    }

    if (m_fieldOn && m_field.valid()) {
        // Scene Y-down: field image covers [xmin,-ymax] .. [xmax,-ymin].
        const QRectF fieldScene(QPointF(m_field.xminUm, -m_field.ymaxUm),
                                QPointF(m_field.xmaxUm, -m_field.yminUm));
        const QRectF fieldNorm = fieldScene.normalized();
        // Keep the full field domain in sceneRect so the user can zoom out to it.
        bounds = bounds.isNull() ? fieldNorm : bounds.united(fieldNorm);
    }

    if (!bounds.isNull()) {
        bounds.adjust(-bounds.width() * 0.05, -bounds.height() * 0.05,
                      bounds.width() * 0.05, bounds.height() * 0.05);
        m_scene->setSceneRect(bounds);
        if (refit && !m_zoomLocked)
            fitPreferredContent();
    }
}

void LayoutView::updateOrbitCenter()
{
    double xLo = 0, xHi = 0, yLo = 0, yHi = 0;
    double zLo = 0, zHi = 1;
    bool anyXy = false;
    bool anyZ = false;

    // Z range only from metals/vias that actually appear in the layout (not whole stackup).
    for (const GdsFlatPolygon &p : m_polys) {
        if (p.layer >= 201 && p.layer <= 299)
            continue;
        if (!m_styles.contains(p.layer) || !m_styles.value(p.layer).hasZ)
            continue;
        const LayerStyle &st = m_styles.value(p.layer);
        const double a = std::min(st.zminUm, st.zmaxUm);
        const double b = std::max(st.zminUm, st.zmaxUm);
        if (!anyZ) {
            zLo = a;
            zHi = b;
            anyZ = true;
        } else {
            zLo = std::min(zLo, a);
            zHi = std::max(zHi, b);
        }
    }
    if (!anyZ || zHi <= zLo)
        zHi = zLo + 1.0;

    for (const GdsFlatPolygon &p : m_polys) {
        for (const QPointF &pt : p.pointsUm) {
            if (!anyXy) {
                xLo = xHi = pt.x();
                yLo = yHi = pt.y();
                anyXy = true;
            } else {
                xLo = std::min(xLo, pt.x());
                xHi = std::max(xHi, pt.x());
                yLo = std::min(yLo, pt.y());
                yHi = std::max(yHi, pt.y());
            }
        }
    }

    m_orbitCx = anyXy ? 0.5 * (xLo + xHi) : 0.0;
    m_orbitCy = anyXy ? 0.5 * (yLo + yHi) : 0.0;
    m_orbitCz = 0.5 * (zLo + zHi);
}

bool LayoutView::layerMidZ(const QString &nameOrGds, qreal *zMid) const
{
    if (!zMid)
        return false;
    const QString n = nameOrGds.trimmed();
    if (n.isEmpty())
        return false;

    bool okNum = false;
    const int gdsNum = n.toInt(&okNum);
    if (okNum && m_styles.contains(gdsNum) && m_styles.value(gdsNum).hasZ) {
        const LayerStyle &st = m_styles.value(gdsNum);
        *zMid = 0.5 * (st.zminUm + st.zmaxUm);
        return true;
    }

    for (auto it = m_styles.cbegin(); it != m_styles.cend(); ++it) {
        if (!it.value().hasZ)
            continue;
        if (it.value().name.compare(n, Qt::CaseInsensitive) != 0)
            continue;
        *zMid = 0.5 * (it.value().zminUm + it.value().zmaxUm);
        return true;
    }
    return false;
}

void LayoutView::rebuildScene3D(bool refit)
{
    struct Item {
        GdsFlatPolygon poly;
        LayerStyle style;
    };
    QVector<Item> items;
    items.reserve(m_polys.size());

    updateOrbitCenter();

    // Used-metal Z span (fallback when a port has no from/to in the table).
    double zLo = m_orbitCz - 0.5;
    double zHi = m_orbitCz + 0.5;
    bool anyZ = false;
    for (const GdsFlatPolygon &p : m_polys) {
        if (p.layer >= 201 && p.layer <= 299)
            continue;
        if (!m_styles.contains(p.layer) || !m_styles.value(p.layer).hasZ)
            continue;
        const LayerStyle &st = m_styles.value(p.layer);
        const double a = std::min(st.zminUm, st.zmaxUm);
        const double b = std::max(st.zminUm, st.zmaxUm);
        if (!anyZ) {
            zLo = a;
            zHi = b;
            anyZ = true;
        } else {
            zLo = std::min(zLo, a);
            zHi = std::max(zHi, b);
        }
    }
    if (!anyZ || zHi <= zLo)
        zHi = zLo + 1.0;

    for (const GdsFlatPolygon &p : m_polys) {
        LayerStyle st;
        if (m_styles.contains(p.layer)) {
            st = m_styles.value(p.layer);
        } else {
            const int portIdx = p.layer - 200;
            st.name = (portIdx >= 1 && portIdx <= 99)
                    ? QStringLiteral("P%1").arg(portIdx)
                    : QStringLiteral("L%1").arg(p.layer);
            st.kind = QStringLiteral("port");
            st.color = QColor(220, 40, 180);
            st.order = 10000 + p.layer;
            st.hasZ = false;
        }
        if (!st.hasZ && st.kind != QLatin1String("port")) {
            st.hasZ = true;
            st.zminUm = zLo;
            st.zmaxUm = zLo + 0.15;
        }
        items.push_back({p, st});
    }

    struct Face {
        QPolygonF poly;
        QColor fill;
        QString name;
        QString kind;
        int gds = 0;
        bool isPort = false;
        qreal depth = 0.0;
    };
    QVector<Face> faces;

    constexpr qreal kMinThickUm = 0.05;
    QRectF portBounds;

    for (const Item &it : items) {
        if (it.poly.pointsUm.size() < 2)
            continue;

        const qreal op = opacityFor(it.poly.layer);
        const bool vis = visibleFor(it.poly.layer);
        const bool isPort = (it.style.kind == QLatin1String("port"))
                || (it.poly.layer >= 201 && it.poly.layer <= 299);

        // Ports: markers on feeder XY at from↔to Z (not top of full stackup).
        if (isPort) {
            QPointF c(0, 0);
            int nPts = 0;
            const int n = it.poly.pointsUm.size();
            const int count = (n > 1 && it.poly.pointsUm.first() == it.poly.pointsUm.last())
                    ? n - 1 : n;
            for (int i = 0; i < count; ++i) {
                c += it.poly.pointsUm.at(i);
                ++nPts;
            }
            if (nPts < 1)
                continue;
            c /= nPts;

            const PortInfo pi = m_ports.value(it.poly.layer);
            QString dir = pi.direction.trimmed().toLower();
            if (dir.isEmpty())
                dir = QStringLiteral("z");

            QColor col = it.style.color;
            col.setAlpha(qBound(40, int(255 * op + 0.5), 255));
            const QString pname = it.style.name.isEmpty()
                    ? QStringLiteral("P%1").arg(it.poly.layer - 200)
                    : it.style.name;

            QPointF tipScene;
            QPointF labelPos;

            if (dir.contains(QLatin1Char('z'))) {
                // XY = port footprint in layout; Z = XML From → To (from Ports table).
                const bool neg = dir.startsWith(QLatin1Char('-'));
                qreal zFrom = 0.0;
                qreal zTo = 0.0;
                bool gotFrom = pi.hasFromZ;
                bool gotTo = pi.hasToZ;
                if (gotFrom)
                    zFrom = pi.zFromUm;
                else
                    gotFrom = layerMidZ(pi.fromLayer, &zFrom);
                if (gotTo)
                    zTo = pi.zToUm;
                else
                    gotTo = layerMidZ(pi.toLayer, &zTo);

                if (gotFrom && !gotTo)
                    zTo = zFrom + 0.5;
                else if (!gotFrom && gotTo)
                    zFrom = zTo - 0.5;
                else if (!gotFrom && !gotTo) {
                    zFrom = 0.5 * (zLo + zHi) - 0.25;
                    zTo = zFrom + 0.5;
                }

                if (qAbs(zTo - zFrom) < 0.05)
                    zTo = zFrom + (neg ? -0.05 : 0.05);
                const qreal zTip = neg ? zFrom : zTo;
                const qreal zTail = neg ? zTo : zFrom;
                tipScene = project3D(c.x(), c.y(), zTip);
                const QPointF tailScene = project3D(c.x(), c.y(), zTail);
                addPortArrowAlong(tailScene, tipScene, col, pname, QStringLiteral("port"),
                                  it.poly.layer, vis, 1e9,
                                  neg ? QStringLiteral("-z") : QStringLiteral("z"));
                labelPos = project3D(c.x(), c.y(), 0.5 * (zTail + zTip));
            } else {
                qreal zMark = 0.5 * (zLo + zHi);
                if (pi.hasFromZ && pi.hasToZ)
                    zMark = 0.5 * (pi.zFromUm + pi.zToUm);
                else if (pi.hasToZ)
                    zMark = pi.zToUm;
                else if (pi.hasFromZ)
                    zMark = pi.zFromUm;
                qreal dx = 1.0, dy = 0.0;
                QString dirLabel = QStringLiteral("x");
                if (dir == QLatin1String("-x")) {
                    dx = -1.0;
                    dirLabel = QStringLiteral("-x");
                } else if (dir == QLatin1String("y") || dir == QLatin1String("+y")) {
                    dx = 0.0;
                    dy = 1.0;
                    dirLabel = QStringLiteral("y");
                } else if (dir == QLatin1String("-y")) {
                    dx = 0.0;
                    dy = -1.0;
                    dirLabel = QStringLiteral("-y");
                }
                tipScene = project3D(c.x(), c.y(), zMark);
                const QPointF outward = project3D(c.x() - dx, c.y() - dy, zMark);
                addPortArrowAlong(outward, tipScene, col, pname, QStringLiteral("port"),
                                  it.poly.layer, vis, 1e9, dirLabel);
                labelPos = tipScene;
            }

            auto *label = m_scene->addSimpleText(pname);
            QFont f = label->font();
            f.setBold(true);
            f.setPointSize(9);
            label->setFont(f);
            label->setBrush(col);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            label->setData(kRoleName, pname);
            label->setData(kRoleKind, QStringLiteral("port"));
            label->setData(kRoleGds, it.poly.layer);
            label->setData(kRoleIsPort, true);
            label->setData(kRolePen, it.style.color);
            label->setVisible(vis);
            label->setZValue(1e9 + 0.1);
            label->setPos(labelPos);
            setPixelOffset(label, 8, -16);
            portBounds |= QRectF(labelPos.x() - 2, labelPos.y() - 2, 4, 4);
            continue;
        }

        if (it.poly.pointsUm.size() < 3)
            continue;

        double z0 = std::min(it.style.zminUm, it.style.zmaxUm);
        double z1 = std::max(it.style.zminUm, it.style.zmaxUm);
        if (z1 - z0 < kMinThickUm)
            z1 = z0 + kMinThickUm;

        QPolygonF topPoly;
        topPoly.reserve(it.poly.pointsUm.size());
        qreal topDepth = 0.0;
        int topN = 0;
        for (const QPointF &p : it.poly.pointsUm) {
            topPoly << project3D(p.x(), p.y(), z1);
            topDepth += depth3D(p.x(), p.y(), z1);
            ++topN;
        }
        if (topN > 0)
            topDepth /= topN;

        const int n = it.poly.pointsUm.size();
        const int edgeCount = (n > 1 && it.poly.pointsUm.first() == it.poly.pointsUm.last())
                ? n - 1 : n;
        for (int i = 0; i < edgeCount; ++i) {
            const QPointF &a = it.poly.pointsUm.at(i);
            const QPointF &b = it.poly.pointsUm.at((i + 1) % n);
            QPolygonF wall;
            wall << project3D(a.x(), a.y(), z0)
                 << project3D(b.x(), b.y(), z0)
                 << project3D(b.x(), b.y(), z1)
                 << project3D(a.x(), a.y(), z1);
            QColor side = it.style.color.darker(135);
            side.setAlpha(qBound(0, int(kBaseFillAlpha * op * 0.85 + 0.5), 255));
            const qreal d = 0.25 * (depth3D(a.x(), a.y(), z0) + depth3D(b.x(), b.y(), z0)
                                    + depth3D(b.x(), b.y(), z1) + depth3D(a.x(), a.y(), z1));
            faces.push_back({wall, side, it.style.name, it.style.kind, it.poly.layer, false, d});
        }

        QColor top = it.style.color;
        top.setAlpha(qBound(0, int(kBaseFillAlpha * op + 0.5), 255));
        faces.push_back({topPoly, top, it.style.name, it.style.kind, it.poly.layer, false, topDepth});
    }

    // Painter's algorithm: farther faces first (lower zValue), closer on top.
    std::stable_sort(faces.begin(), faces.end(),
                     [](const Face &a, const Face &b) { return a.depth > b.depth; });

    QRectF bounds = portBounds;
    for (int i = 0; i < faces.size(); ++i) {
        const Face &f = faces.at(i);
        const bool vis = visibleFor(f.gds);
        auto *item = m_scene->addPolygon(f.poly, QPen(QColor(30, 30, 30), 0), QBrush(f.fill));
        item->setData(kRoleName, f.name);
        item->setData(kRoleKind, f.kind);
        item->setData(kRoleGds, f.gds);
        item->setData(kRoleIsPort, false);
        if (m_styles.contains(f.gds))
            item->setData(kRoleBrush, m_styles.value(f.gds).color);
        else
            item->setData(kRoleBrush, f.fill);
        item->setVisible(vis);
        item->setZValue(double(i));
        bounds |= f.poly.boundingRect();
    }

    if (!bounds.isNull()) {
        bounds.adjust(-bounds.width() * 0.08, -bounds.height() * 0.08,
                      bounds.width() * 0.08, bounds.height() * 0.08);
        m_scene->setSceneRect(bounds);
        if (refit) {
            m_zoomLocked = false;
            fitContent();
        }
    }
}

QPointF LayoutView::project3D(qreal xUm, qreal yUm, qreal zUm) const
{
    const qreal yaw = qDegreesToRadians(m_yawDeg);
    const qreal pitch = qDegreesToRadians(m_pitchDeg);
    const qreal px = xUm - m_orbitCx;
    const qreal py = yUm - m_orbitCy;
    const qreal pz = zUm - m_orbitCz;

    const qreal cy = std::cos(yaw);
    const qreal sy = std::sin(yaw);
    const qreal x1 = px * cy - py * sy;
    const qreal y1 = px * sy + py * cy;
    const qreal z1 = pz;

    const qreal cp = std::cos(pitch);
    const qreal sp = std::sin(pitch);
    const qreal z2 = y1 * sp + z1 * cp;
    // Orthographic view along +Y after yaw/pitch; Qt Y grows downward → flip Z up.
    return QPointF(x1, -z2);
}

qreal LayoutView::depth3D(qreal xUm, qreal yUm, qreal zUm) const
{
    const qreal yaw = qDegreesToRadians(m_yawDeg);
    const qreal pitch = qDegreesToRadians(m_pitchDeg);
    const qreal px = xUm - m_orbitCx;
    const qreal py = yUm - m_orbitCy;
    const qreal pz = zUm - m_orbitCz;

    const qreal sy = std::sin(yaw);
    const qreal cy = std::cos(yaw);
    const qreal y1 = px * sy + py * cy;
    const qreal z1 = pz;

    const qreal cp = std::cos(pitch);
    const qreal sp = std::sin(pitch);
    
    return y1 * cp - z1 * sp;
}

void LayoutView::resetOrbitAngles()
{
    m_yawDeg = 45.0;
    m_pitchDeg = 30.0;
    m_volumeCamZoom = 1.0;
}

int LayoutView::volumeRenderResolution() const
{
    const qreal dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    // Prefer a bit over viewport so fit-down stays sharp (avoids blurry upscale).
    const int side = qRound(qMax(viewport()->width(), viewport()->height()) * dpr * 1.25);
    return qBound(720, side, 1600);
}

void LayoutView::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode) {
        syncFloatingControls();
        return;
    }
    // Field stays as a 2D Z-slice in this pane; volume is an external window.
    if (mode == ViewMode::Iso3D && m_fieldOn)
        mode = ViewMode::Top2D;

    m_viewMode = mode;
    if (mode == ViewMode::Iso3D)
        resetOrbitAngles();
    saveViewModeToSettings();
    syncFloatingControls();
    if (!m_polys.isEmpty() || m_field.valid() || !m_field.status.isEmpty() || m_fieldOn)
        rebuildScene(true);
}

void LayoutView::onModeButtonToggled(bool on)
{
    if (on && m_fieldOn) {
        // Keep the layout pane on the 2D Field slice; open external volume viewer.
        const QSignalBlocker block(m_modeBtn);
        m_modeBtn->setChecked(false);
        m_modeBtn->setText(QStringLiteral("3D"));
        m_modeBtn->setToolTip(tr("Open interactive PyVista volume window (layout stays 2D).\n"
                                "Drag: pan · Pinch / Ctrl+scroll: zoom · F: fit · Home: full"));
        emit fieldExternalVolumeRequested();
        return;
    }
    setViewMode(on ? ViewMode::Iso3D : ViewMode::Top2D);
}

/*!*******************************************************************************************************************
 * \brief Slot: Field toolbutton toggled — forwards to \c setFieldMode.
 **********************************************************************************************************************/
void LayoutView::onFieldButtonToggled(bool on)
{
    setFieldMode(on);
}

/*!*******************************************************************************************************************
 * \brief Enables or disables Field mode (Z-clip heatmap overlay).
 *
 * Layout pane stays Top2D; Iso3D geometry preview is turned off while Field is
 * on. Persists LayoutPreview/viewField and emits \c fieldModeChanged.
 *
 * \param on True to enter Field mode.
 **********************************************************************************************************************/
void LayoutView::setFieldMode(bool on)
{
    if (m_fieldOn == on) {
        syncFloatingControls();
        return;
    }
    m_fieldOn = on;
    if (m_fieldOn)
        m_zoomLocked = false;
    if (m_fieldOn && m_viewMode == ViewMode::Iso3D)
        m_viewMode = ViewMode::Top2D;
    if (m_fieldBtn) {
        const QSignalBlocker block(m_fieldBtn);
        m_fieldBtn->setChecked(m_fieldOn);
    }
    saveViewModeToSettings();
    syncFloatingControls();
    emit fieldModeChanged(m_fieldOn);
    if (!m_polys.isEmpty() || m_field.valid() || !m_field.status.isEmpty() || m_fieldOn)
        rebuildScene(true);
}

/*!*******************************************************************************************************************
 * \brief Replaces the current Field overlay and refreshes the floating panel.
 *
 * \param overlay Heatmap, GDS µm frame, Z range, quantity, arrows, status.
 **********************************************************************************************************************/
void LayoutView::setFieldOverlay(const FieldOverlay &overlay)
{
    const bool sameImage = (overlay.image.cacheKey() == m_field.image.cacheKey())
            && (overlay.image.isNull() == m_field.image.isNull());
    const bool sameFrame =
            qFuzzyCompare(overlay.xminUm, m_field.xminUm)
            && qFuzzyCompare(overlay.xmaxUm, m_field.xmaxUm)
            && qFuzzyCompare(overlay.yminUm, m_field.yminUm)
            && qFuzzyCompare(overlay.ymaxUm, m_field.ymaxUm)
            && (overlay.showArrows == m_field.showArrows)
            && (overlay.arrows.size() == m_field.arrows.size())
            && (overlay.volume == m_field.volume);
    const bool statusOnly = sameImage && sameFrame && m_field.valid();
    const bool sliderDown = m_fieldZSlider && m_fieldZSlider->isSliderDown();

    m_field = overlay;

    // Keep the handle where the user dragged it: a status-only "Exporting…" update
    // still carries the previous slice zUm and must not yank the slider back.
    if (!sliderDown && !statusOnly)
        updateFieldControlsFromOverlay();

    syncFloatingControls();
    if (m_fieldOn && !statusOnly) {
        rebuildScene(false);
        // Volume screenshots must stay fitted 1:1 (zoom = camera re-render).
        if (m_field.volume || isFieldVolume() || !m_zoomLocked)
            fitPreferredContent();
    }
}

/*!*******************************************************************************************************************
 * \brief Clears the Field overlay (heatmap / status) and rebuilds if Field is on.
 **********************************************************************************************************************/
void LayoutView::clearFieldOverlay()
{
    m_field = FieldOverlay{};
    syncFloatingControls();
    if (m_fieldOn)
        rebuildScene(false);
}

/*!*******************************************************************************************************************
 * \brief Z clip [µm] from the Field Z slider mapped into overlay zMin..zMax.
 **********************************************************************************************************************/
qreal LayoutView::fieldClipZUm() const
{
    if (!m_fieldZSlider)
        return m_field.zUm;
    const qreal z0 = m_field.zMinUm;
    const qreal z1 = qMax(m_field.zMaxUm, z0 + 1e-9);
    const qreal t = m_fieldZSlider->value() / 1000.0;
    return z0 + t * (z1 - z0);
}

/*!*******************************************************************************************************************
 * \brief True when the Field panel Log checkbox is checked.
 **********************************************************************************************************************/
bool LayoutView::fieldLogScale() const
{
    return m_fieldLogChk && m_fieldLogChk->isChecked();
}

/*!*******************************************************************************************************************
 * \brief True when the Field panel Arrows checkbox is checked.
 **********************************************************************************************************************/
bool LayoutView::fieldShowArrows() const
{
    return m_fieldArrowsChk && m_fieldArrowsChk->isChecked();
}

/*!*******************************************************************************************************************
 * \brief GDS µm Y-up bounding box of non-port layout polygons (for field crop).
 *
 * Skips port GDS layers (201–299) and styles with kind=port.
 **********************************************************************************************************************/
QRectF LayoutView::layoutContentBoundsUm() const
{
    QRectF bb;
    bool any = false;
    for (const GdsFlatPolygon &p : m_polys) {
        if (p.layer >= 201 && p.layer <= 299)
            continue;
        if (m_styles.contains(p.layer)
            && m_styles.value(p.layer).kind.compare(QLatin1String("port"), Qt::CaseInsensitive) == 0)
            continue;
        for (const QPointF &pt : p.pointsUm) {
            if (!any) {
                bb = QRectF(pt, pt);
                any = true;
            } else {
                bb.setLeft(qMin(bb.left(), pt.x()));
                bb.setRight(qMax(bb.right(), pt.x()));
                bb.setTop(qMin(bb.top(), pt.y()));
                bb.setBottom(qMax(bb.bottom(), pt.y()));
            }
        }
    }
    return any ? bb.normalized() : QRectF();
}

/*!*******************************************************************************************************************
 * \brief Adds the Field heatmap pixmap under layout polygons (scene Y-down).
 **********************************************************************************************************************/
void LayoutView::addFieldOverlayItems()
{
    if (!m_fieldOn || !m_field.valid() || m_field.volume)
        return;

    const QPixmap pix = QPixmap::fromImage(m_field.image);
    if (pix.isNull())
        return;

    auto *item = m_scene->addPixmap(pix);
    const qreal wUm = m_field.xmaxUm - m_field.xminUm;
    const qreal hUm = m_field.ymaxUm - m_field.yminUm;
    const qreal sx = wUm / qMax(1, pix.width());
    const qreal sy = hUm / qMax(1, pix.height());
    item->setTransform(QTransform::fromScale(sx, sy));
    // Image row 0 = ymax (GDS); scene Y grows down → place top-left at (xmin, -ymax).
    item->setPos(m_field.xminUm, -m_field.ymaxUm);
    item->setZValue(-100000);
    item->setOpacity(0.72);
    item->setAcceptedMouseButtons(Qt::NoButton);
}

/*!*******************************************************************************************************************
 * \brief Places the Field+3D volume screenshot as the sole scene content.
 **********************************************************************************************************************/
void LayoutView::addFieldVolumeItems()
{
    if (!m_fieldOn)
        return;
    if (!m_field.valid()) {
        // Keep a tiny scene so fit/status still work while exporting.
        m_scene->setSceneRect(QRectF(0, 0, 10, 10));
        return;
    }

    const QPixmap pix = QPixmap::fromImage(m_field.image);
    if (pix.isNull())
        return;

    auto *item = m_scene->addPixmap(pix);
    item->setPos(0, 0);
    item->setZValue(0);
    item->setOpacity(1.0);
    item->setAcceptedMouseButtons(Qt::NoButton);
    // Fast = sharper when the PNG is at/above viewport size (Smooth blurs upscales).
    item->setTransformationMode(Qt::FastTransformation);
    m_scene->setSceneRect(QRectF(pix.rect()));
}

/*!*******************************************************************************************************************
 * \brief Syncs Z slider / Log / Arrows widgets from \c m_field without re-export.
 **********************************************************************************************************************/
void LayoutView::updateFieldControlsFromOverlay()
{
    if (!m_fieldZSlider)
        return;
    m_blockFieldControls = true;
    const qreal z0 = m_field.zMinUm;
    const qreal z1 = qMax(m_field.zMaxUm, z0 + 1e-9);
    const qreal z = qBound(z0, m_field.zUm, z1);
    const int slider = qRound(1000.0 * (z - z0) / (z1 - z0));
    m_fieldZSlider->setValue(qBound(0, slider, 1000));
    if (m_fieldLogChk)
        m_fieldLogChk->setChecked(m_field.logScale);
    if (m_fieldArrowsChk)
        m_fieldArrowsChk->setChecked(m_field.showArrows);
    m_blockFieldControls = false;
}

/*!*******************************************************************************************************************
 * \brief Shows/hides Field panel, updates compact status line, repositions controls.
 **********************************************************************************************************************/
void LayoutView::syncFloatingControls()
{
    if (m_modeBtn) {
        m_modeBtn->setEnabled(true);
        const QSignalBlocker block(m_modeBtn);
        if (m_fieldOn) {
            // Field pane is always Top2D; the button launches the external viewer.
            m_modeBtn->setChecked(false);
            m_modeBtn->setText(QStringLiteral("3D"));
            m_modeBtn->setToolTip(tr("Open interactive PyVista volume window (layout stays 2D).\n"
                                     "Drag: pan · Pinch / Ctrl+scroll: zoom · F: fit · Home: full"));
        } else {
            m_modeBtn->setChecked(m_viewMode == ViewMode::Iso3D);
            m_modeBtn->setText(m_viewMode == ViewMode::Iso3D ? QStringLiteral("3D")
                                                            : QStringLiteral("2D"));
            m_modeBtn->setToolTip(m_viewMode == ViewMode::Iso3D
                                      ? tr("3D view (click for top view).\n"
                                           "Drag: orbit · Click: select · Two-finger scroll: orbit\n"
                                           "Alt+drag / Middle: pan · Pinch / Ctrl+scroll: zoom · R: reset")
                                      : tr("Top view (click for 3D).\n"
                                           "Drag: pan · Click: select · Pinch / Ctrl+scroll: zoom · F: fit layout · Home: full field"));
        }
    }
    if (m_fieldArrowsChk)
        m_fieldArrowsChk->setVisible(m_fieldOn && !isFieldVolume());
    if (m_fieldHotZBtn) {
        m_fieldHotZBtn->setToolTip(isFieldVolume()
                                       ? tr("Jump clip to hottest Z (max temperature or |E|).")
                                       : tr("Jump to hottest Z (max temperature or |E| in the layout ROI)."));
    }
    if (m_fieldPanel)
        m_fieldPanel->setVisible(m_fieldOn);
    if (m_fieldStatusLbl) {
        QString full;
        const bool exporting = m_field.status.contains(QStringLiteral("Exporting"), Qt::CaseInsensitive);
        if (!m_field.status.isEmpty() && !exporting)
            full = m_field.status;
        else if (m_field.valid()) {
            if (isFieldVolume() || m_field.volume) {
                full = tr("%1 volume @ Z=%2 µm")
                           .arg(m_field.quantity.isEmpty()
                                    ? QStringLiteral("Field")
                                    : m_field.quantity)
                           .arg(m_field.zUm, 0, 'g', 4);
            } else {
                full = tr("%1 @ Z=%2 µm")
                           .arg(m_field.quantity.isEmpty()
                                    ? QStringLiteral("Field")
                                    : m_field.quantity)
                           .arg(m_field.zUm, 0, 'g', 4);
            }
            if (exporting)
                full += tr(" (updating…)");
        } else if (!m_field.status.isEmpty())
            full = m_field.status;
        else if (m_fieldOn)
            full = isFieldVolume() ? tr("Loading field volume…")
                                   : tr("Loading field slice…");

        // One compact line in the panel; full text on hover (and Simulation log).
        QString shortTxt = full;
        shortTxt.replace(QLatin1Char('\n'), QLatin1Char(' '));
        shortTxt = shortTxt.simplified();
        if (shortTxt.size() > 42)
            shortTxt = shortTxt.left(40) + QStringLiteral("…");
        m_fieldStatusLbl->setText(shortTxt);
        m_fieldStatusLbl->setToolTip(full);
        m_fieldStatusLbl->setVisible(!shortTxt.isEmpty());
    }
    if (m_fieldPanel)
        m_fieldPanel->adjustSize();
    repositionFloatingControls();
}

/*!*******************************************************************************************************************
 * \brief Emits \c fieldSliceRequest from current slider / checkbox state.
 **********************************************************************************************************************/
void LayoutView::emitFieldSliceRequest()
{
    emit fieldSliceRequest(fieldClipZUm(), fieldLogScale(), fieldShowArrows());
}

/*!*******************************************************************************************************************
 * \brief Coalesce volume zoom into one re-render after the user stops scrolling / pinching.
 **********************************************************************************************************************/
void LayoutView::scheduleVolumeCameraRefresh()
{
    if (!isFieldVolume())
        return;
    if (m_fieldStatusLbl)
        m_fieldStatusLbl->setText(tr("Zoom… release to update"));
    if (!m_volumeCamSettle) {
        m_volumeCamSettle = new QTimer(this);
        m_volumeCamSettle->setSingleShot(true);
        m_volumeCamSettle->setInterval(380);
        connect(m_volumeCamSettle, &QTimer::timeout, this, [this]() {
            if (isFieldVolume())
                emitFieldSliceRequest();
        });
    }
    m_volumeCamSettle->start();
}

void LayoutView::applyVolumeCamZoomFactor(qreal factor)
{
    if (!isFieldVolume() || factor <= 0.0 || qFuzzyCompare(factor, 1.0))
        return;
    m_volumeCamZoom = qBound(0.35, m_volumeCamZoom * factor, 8.0);
    scheduleVolumeCameraRefresh();
}

/*!*******************************************************************************************************************
 * \brief Clears the previous Field frame when switching 2D ↔ volume so the old picture does not linger.
 **********************************************************************************************************************/
void LayoutView::wipeFieldSceneForModeSwitch(bool toVolume)
{
    FieldOverlay blank;
    blank.volume = toVolume;
    blank.zUm = m_field.zUm;
    blank.zMinUm = m_field.zMinUm;
    blank.zMaxUm = m_field.zMaxUm;
    blank.logScale = fieldLogScale();
    blank.showArrows = false;
    blank.status = toVolume ? tr("Exporting volume…") : tr("Exporting slice…");
    // Bypass setFieldOverlay status-only short-circuit: force empty image + rebuild.
    m_field = blank;
    syncFloatingControls();
    rebuildScene(true);
}

/*!*******************************************************************************************************************
 * \brief Live Z readout while dragging; export is deferred until slider release.
 **********************************************************************************************************************/
void LayoutView::onFieldZSliderPreview(int /*value*/)
{
    if (m_blockFieldControls || !m_fieldOn || !m_fieldStatusLbl)
        return;
    // Instant Z readout while dragging — export only on release.
    const QString q = m_field.quantity.isEmpty() ? QStringLiteral("Field") : m_field.quantity;
    const QString full = tr("%1 @ Z=%2 µm").arg(q).arg(fieldClipZUm(), 0, 'g', 4);
    QString shortTxt = full;
    if (shortTxt.size() > 42)
        shortTxt = shortTxt.left(40) + QStringLiteral("…");
    m_fieldStatusLbl->setText(shortTxt);
    m_fieldStatusLbl->setToolTip(full + tr("\n(release slider to reload slice)"));
}

/*!*******************************************************************************************************************
 * \brief Slider released → emit \c fieldSliceRequest for a new Z-slice export.
 **********************************************************************************************************************/
void LayoutView::onFieldZSliderCommitted()
{
    if (m_blockFieldControls || !m_fieldOn)
        return;
    emitFieldSliceRequest();
}

/*!*******************************************************************************************************************
 * \brief Max button → ask MainWindow to re-export at the hottest Z (auto-Z).
 **********************************************************************************************************************/
void LayoutView::onFieldHotZClicked()
{
    if (m_blockFieldControls || !m_fieldOn)
        return;
    if (m_fieldStatusLbl)
        m_fieldStatusLbl->setText(tr("Finding max Z…"));
    emit fieldHotZRequest();
}

/*!*******************************************************************************************************************
 * \brief Log / Arrows changed; arrow-only toggles redraw without re-export.
 **********************************************************************************************************************/
void LayoutView::onFieldControlsChanged()
{
    if (m_blockFieldControls || !m_fieldOn)
        return;
    m_field.logScale = fieldLogScale();
    m_field.showArrows = fieldShowArrows();
    // Arrow toggle alone can redraw without re-export.
    if (sender() == m_fieldArrowsChk && m_field.valid()) {
        rebuildScene(false);
        return;
    }
    emitFieldSliceRequest();
}

void LayoutView::repositionFloatingControls()
{
    const int m = 10;
    // Keep clear of the vertical scrollbar so 2D/3D is not clipped.
    int sb = 0;
    if (verticalScrollBar() && verticalScrollBar()->isVisible())
        sb = verticalScrollBar()->width();
    int x = width() - m - sb;
    if (m_modeBtn) {
        x -= m_modeBtn->width();
        m_modeBtn->move(x, m);
        m_modeBtn->raise();
        x -= m;
    }
    if (m_fieldBtn) {
        x -= m_fieldBtn->width();
        m_fieldBtn->move(x, m);
        m_fieldBtn->raise();
    }
    if (m_fieldPanel && m_fieldPanel->isVisible()) {
        m_fieldPanel->adjustSize();
        const int px = width() - m_fieldPanel->width() - m - sb;
        m_fieldPanel->move(qMax(m, px), m + 30);
        m_fieldPanel->raise();
    }
}

void LayoutView::loadViewModeFromSettings()
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    const bool v3d = settings.value(QStringLiteral("view3d"), false).toBool();
    const bool vField = settings.value(QStringLiteral("viewField"), false).toBool();
    settings.endGroup();
    m_fieldOn = vField;
    m_viewMode = v3d ? ViewMode::Iso3D : ViewMode::Top2D;
}

void LayoutView::saveViewModeToSettings() const
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    settings.setValue(QStringLiteral("view3d"), m_viewMode == ViewMode::Iso3D);
    settings.setValue(QStringLiteral("viewField"), m_fieldOn);
    settings.endGroup();
}

/*!*******************************************************************************************************************
 * \brief Highlights all polygons tagged with the given stack / layer name.
 *
 * Clears the previous highlight first. Empty \a name is ignored for the “on”
 * step after clearing the old name.
 *
 * \param name Layer name stored on graphics items (matches SubstrateView).
 **********************************************************************************************************************/
void LayoutView::setHighlightedLayer(const QString &name)
{
    if (name == m_highlightedName)
        return;
    if (!m_highlightedName.isEmpty())
        setLayerHighlightVisual(m_highlightedName, false);
    m_highlightedName = name;
    if (!m_highlightedName.isEmpty())
        setLayerHighlightVisual(m_highlightedName, true);
}

/*!*******************************************************************************************************************
 * \brief Removes the current highlight and emits \c highlightCleared.
 *
 * No-op if nothing is highlighted (avoids recursive clear loops with SubstrateView).
 **********************************************************************************************************************/
void LayoutView::clearHighlight()
{
    if (m_highlightedName.isEmpty())
        return;
    const QString previous = m_highlightedName;
    m_highlightedName.clear();
    setLayerHighlightVisual(previous, false);
    emit highlightCleared();
}

/*!*******************************************************************************************************************
 * \brief Applies or removes a high-contrast highlight (pen + fill) for the named layer.
 *
 * Outline alone blends into polygon edges; fill is also brightened so the pick is obvious.
 *
 * \param name Layer name role on scene items.
 * \param on   True to highlight; false to restore the default pen and fill.
 **********************************************************************************************************************/
void LayoutView::setLayerHighlightVisual(const QString &name, bool on)
{
    if (!m_scene || name.isEmpty())
        return;

    const QPen normalPoly(QColor(20, 20, 20), 0);
    QPen hiPen(QColor(255, 220, 0));
    hiPen.setCosmetic(true);
    hiPen.setWidth(4);

    for (QGraphicsItem *item : m_scene->items()) {
        if (item->data(kRoleName).toString() != name)
            continue;

        const int gds = item->data(kRoleGds).toInt();
        const qreal op = opacityFor(gds);

        if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item)) {
            if (on) {
                if (!item->data(kRolePen).isValid())
                    item->setData(kRolePen, line->pen().color());
                QPen p = hiPen;
                p.setWidth(kPortPenWidth + 1);
                p.setColor(QColor(255, 220, 0));
                line->setPen(p);
                line->setZValue(line->zValue() + 0.5);
            } else {
                QColor c = item->data(kRolePen).isValid()
                        ? item->data(kRolePen).value<QColor>()
                        : QColor(220, 40, 180);
                c.setAlpha(qBound(40, int(255 * op + 0.5), 255));
                QPen p(c);
                p.setCosmetic(true);
                p.setWidth(kPortPenWidth);
                p.setCapStyle(Qt::FlatCap);
                line->setPen(p);
            }
            continue;
        }

        if (auto *text = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item)) {
            if (on)
                text->setBrush(QColor(255, 180, 0));
            else {
                QColor c = item->data(kRolePen).isValid()
                        ? item->data(kRolePen).value<QColor>()
                        : QColor(220, 40, 180);
                text->setBrush(c);
            }
            continue;
        }

        if (auto *shape = qgraphicsitem_cast<QAbstractGraphicsShapeItem *>(item)) {
            if (on) {
                if (!item->data(kRoleBrush).isValid())
                    item->setData(kRoleBrush, shape->brush().color());
                const QColor base = item->data(kRoleBrush).value<QColor>();
                QColor fill(qMin(255, int(0.35 * base.red()   + 0.65 * 255)),
                            qMin(255, int(0.35 * base.green() + 0.65 * 210)),
                            qMin(255, int(0.35 * base.blue()  + 0.65 * 0)));
                // Highlight alpha tracks layer opacity so the slider is visible while selected.
                fill.setAlpha(qBound(40, int(230 * op + 0.5), 255));
                shape->setPen(hiPen);
                shape->setBrush(fill);
                shape->setZValue(shape->zValue() + 0.5);
            } else {
                shape->setPen(normalPoly);
                if (item->data(kRoleBrush).isValid()) {
                    QColor orig = item->data(kRoleBrush).value<QColor>();
                    orig.setAlpha(qBound(0, int(kBaseFillAlpha * op + 0.5), 255));
                    shape->setBrush(orig);
                    item->setData(kRoleBrush, QVariant());
                }
            }
        }
    }
    viewport()->update();
}

/*!*******************************************************************************************************************
 * \brief Re-applies the current highlight after a redraw (e.g. setPolygons).
 **********************************************************************************************************************/
void LayoutView::applyHighlight()
{
    if (!m_highlightedName.isEmpty())
        setLayerHighlightVisual(m_highlightedName, true);
}

/*!*******************************************************************************************************************
 * \brief Fits layout (or full scene) into the viewport while keeping aspect ratio.
 *
 * With Field on, prefers the GDS layout bbox so the DUT is readable; the sceneRect
 * still spans the full field domain so the user can zoom out. Home fits the full scene.
 **********************************************************************************************************************/
void LayoutView::fitPreferredContent()
{
    if (isFieldVolume()) {
        // Volume is a screenshot — always show 1:1 fit; zoom is camera re-render.
        resetTransform();
        fitFullContent();
        return;
    }
    if (m_fieldOn && m_field.valid() && !m_field.volume) {
        // Prefer visible metals so a large hidden sheet does not force a wide frame.
        QRectF layoutUm;
        bool any = false;
        for (const GdsFlatPolygon &p : m_polys) {
            if (p.layer >= 201 && p.layer <= 299)
                continue;
            if (m_styles.contains(p.layer)
                && m_styles.value(p.layer).kind.compare(QLatin1String("port"), Qt::CaseInsensitive) == 0)
                continue;
            if (!visibleFor(p.layer))
                continue;
            for (const QPointF &pt : p.pointsUm) {
                if (!any) {
                    layoutUm = QRectF(pt, pt);
                    any = true;
                } else {
                    layoutUm.setLeft(qMin(layoutUm.left(), pt.x()));
                    layoutUm.setRight(qMax(layoutUm.right(), pt.x()));
                    layoutUm.setTop(qMin(layoutUm.top(), pt.y()));
                    layoutUm.setBottom(qMax(layoutUm.bottom(), pt.y()));
                }
            }
        }
        if (any) {
            layoutUm = layoutUm.normalized();
            if (layoutUm.width() > 0 && layoutUm.height() > 0) {
                // GDS Y-up → scene Y-down.
                QRectF layoutScene(QPointF(layoutUm.left(), -layoutUm.bottom()),
                                   QPointF(layoutUm.right(), -layoutUm.top()));
                layoutScene = layoutScene.normalized();
                layoutScene.adjust(-layoutScene.width() * 0.25, -layoutScene.height() * 0.25,
                                   layoutScene.width() * 0.25, layoutScene.height() * 0.25);
                fitInView(layoutScene, Qt::KeepAspectRatio);
                return;
            }
        }
    }
    fitFullContent();
}

/*!*******************************************************************************************************************
 * \brief Fits the full scene rectangle (layout ∪ field domain).
 **********************************************************************************************************************/
void LayoutView::fitFullContent()
{
    if (m_scene->sceneRect().isEmpty())
        return;
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

/*!*******************************************************************************************************************
 * \brief Alias for \c fitPreferredContent (F key / resize auto-fit).
 **********************************************************************************************************************/
void LayoutView::fitContent()
{
    fitPreferredContent();
}

/*!*******************************************************************************************************************
 * \brief Fills the exposed scene area with a white background.
 *
 * Uses the scene-coordinate \a rect from Qt (not viewport pixel bounds).
 *
 * \param painter Painter already transformed to scene coordinates.
 * \param rect    Exposed region in scene coordinates.
 **********************************************************************************************************************/
void LayoutView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, Qt::white);
}

/*!*******************************************************************************************************************
 * \brief Zooms the view with the mouse wheel and locks auto-fit on resize.
 *
 * \param event Wheel event (angle delta selects zoom in/out).
 **********************************************************************************************************************/
void LayoutView::wheelEvent(QWheelEvent *event)
{
    m_zoomLocked = true;

    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    const bool ctrl = event->modifiers() & Qt::ControlModifier;

    auto applyOrbit = [&](qreal dx, qreal dy) {
        m_yawDeg += dx;
        m_pitchDeg = qBound(-85.0, m_pitchDeg - dy, 85.0);
        if (isFieldVolume()) {
            // Keep last volume frame while orbiting; export on release / debounce.
            if (m_fieldStatusLbl)
                m_fieldStatusLbl->setText(tr("Orbit… release to update"));
            return;
        }
        if (!m_polys.isEmpty())
            rebuildScene(false);
    };

    auto applyPan = [&](int dx, int dy) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
        verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
    };

    // Field volume: wheel / trackpad scroll = camera zoom (settle → one re-render).
    // Orbit is left-drag / right-drag only.
    if (isFieldVolume()) {
        qreal dy = angle.y();
        if (qFuzzyIsNull(dy))
            dy = pixel.y();
        // Trackpad horizontal flick → orbit preview (no immediate re-render).
        if (!ctrl && !pixel.isNull() && qAbs(pixel.x()) > qAbs(pixel.y()) * 1.5) {
            applyOrbit(pixel.x() * 0.35, pixel.y() * 0.35);
            event->accept();
            return;
        }
        if (qFuzzyIsNull(dy) && !angle.isNull() && qAbs(angle.x()) > qAbs(angle.y()) * 1.5) {
            applyOrbit(angle.x() * 0.08, 0);
            event->accept();
            return;
        }
        if (qFuzzyIsNull(dy)) {
            event->accept();
            return;
        }
        // Proportional to delta — trackpads send many small steps; fixed 1.12 felt broken.
        const double factor = std::pow(1.0035, static_cast<double>(dy));
        applyVolumeCamZoomFactor(factor);
        event->accept();
        return;
    }

    // Ctrl+scroll / pinch path below → zoom. Otherwise navigate.
    if (!ctrl) {
        if (m_viewMode == ViewMode::Iso3D) {
            // Trackpad / wheel → orbit (geometry 3D only).
            if (!pixel.isNull())
                applyOrbit(pixel.x() * 0.35, pixel.y() * 0.35);
            else if (!angle.isNull())
                applyOrbit(angle.x() * 0.08, angle.y() * 0.08);
            else {
                QGraphicsView::wheelEvent(event);
                return;
            }
            event->accept();
            return;
        }

        // 2D: pan with trackpad pixel scroll; plain vertical notches still zoom below.
        if (!pixel.isNull()) {
            applyPan(pixel.x(), pixel.y());
            event->accept();
            return;
        }
        if (qAbs(angle.x()) > qAbs(angle.y()) && !angle.isNull()) {
            applyPan(angle.x(), angle.y());
            event->accept();
            return;
        }
    }

    qreal dy = angle.y();
    if (qFuzzyIsNull(dy))
        dy = pixel.y();
    if (qFuzzyIsNull(dy)) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    const double factor = (dy > 0) ? 1.15 : (1.0 / 1.15);
    const QGraphicsView::ViewportAnchor oldAnchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    setTransformationAnchor(oldAnchor);
    event->accept();
}

bool LayoutView::event(QEvent *event)
{
    // Windows Precision Touchpad pinch often lands on the view (not viewport).
    if (isFieldVolume() && event->type() == QEvent::NativeGesture) {
        auto *ne = static_cast<QNativeGestureEvent *>(event);
        if (ne->gestureType() == Qt::ZoomNativeGesture && !qFuzzyIsNull(ne->value())) {
            applyVolumeCamZoomFactor(1.0 + ne->value());
            return true;
        }
    }
    if (isFieldVolume() && event->type() == QEvent::Gesture) {
        auto *ge = static_cast<QGestureEvent *>(event);
        if (QPinchGesture *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                const qreal factor = pinch->scaleFactor();
                if (factor > 0.0 && !qFuzzyCompare(factor, 1.0)) {
                    m_volumeCamZoom = qBound(0.35, m_volumeCamZoom * factor, 8.0);
                    if (m_fieldStatusLbl)
                        m_fieldStatusLbl->setText(tr("Zoom… release to update"));
                }
            }
            if (pinch->state() == Qt::GestureFinished
                || pinch->state() == Qt::GestureCanceled) {
                if (m_volumeCamSettle)
                    m_volumeCamSettle->stop();
                emitFieldSliceRequest();
            }
            return true;
        }
    }
    return QGraphicsView::event(event);
}

bool LayoutView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        auto *ge = static_cast<QGestureEvent *>(event);
        if (QPinchGesture *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                const qreal factor = pinch->scaleFactor();
                if (factor > 0.0 && !qFuzzyCompare(factor, 1.0)) {
                    if (isFieldVolume()) {
                        m_volumeCamZoom = qBound(0.35, m_volumeCamZoom * factor, 8.0);
                        if (m_fieldStatusLbl)
                            m_fieldStatusLbl->setText(tr("Zoom… release to update"));
                    } else {
                        m_zoomLocked = true;
                        const QGraphicsView::ViewportAnchor oldAnchor = transformationAnchor();
                        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
                        scale(factor, factor);
                        setTransformationAnchor(oldAnchor);
                    }
                }
            }
            if (isFieldVolume()
                && (pinch->state() == Qt::GestureFinished
                    || pinch->state() == Qt::GestureCanceled)) {
                if (m_volumeCamSettle)
                    m_volumeCamSettle->stop();
                emitFieldSliceRequest();
            }
            return true;
        }
    }
    if (event->type() == QEvent::NativeGesture) {
        auto *ne = static_cast<QNativeGestureEvent *>(event);
        if (ne->gestureType() == Qt::ZoomNativeGesture) {
            if (!qFuzzyIsNull(ne->value())) {
                if (isFieldVolume()) {
                    applyVolumeCamZoomFactor(1.0 + ne->value());
                } else {
                    const qreal factor = 1.0 + ne->value();
                    if (factor > 0.0) {
                        m_zoomLocked = true;
                        const QGraphicsView::ViewportAnchor oldAnchor = transformationAnchor();
                        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
                        scale(factor, factor);
                        setTransformationAnchor(oldAnchor);
                    }
                }
                return true;
            }
        }
    }
    return QGraphicsView::viewportEvent(event);
}

/*!*******************************************************************************************************************
 * \brief Handles Escape (clear highlight) and F/Home (fit view).
 *
 * \param event Key event.
 **********************************************************************************************************************/
void LayoutView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        clearHighlight();
        clearMeasure();
        emit highlightCleared();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_R && m_viewMode == ViewMode::Iso3D) {
        resetOrbitAngles();
        m_zoomLocked = false;
        if (isFieldVolume()) {
            emitFieldSliceRequest();
        } else if (!m_polys.isEmpty()) {
            rebuildScene(true);
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F) {
        m_zoomLocked = false;
        resetTransform();
        fitPreferredContent();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Home) {
        m_zoomLocked = false;
        resetTransform();
        fitFullContent();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

/*!*******************************************************************************************************************
 * \brief Re-fits content on resize unless the user has manually zoomed.
 *
 * \param event Resize event.
 **********************************************************************************************************************/
void LayoutView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (!m_zoomLocked)
        fitContent();
    repositionFloatingControls();
}

/*!*******************************************************************************************************************
 * \brief Highlights the clicked polygon and emits \c layerClicked.
 *
 * Collects named layers under the cursor in top-to-bottom stacking order. The first
 * click selects the topmost; further clicks cycle to the next layer below (wrapping).
 *
 * \param event Mouse event; only the left button triggers selection.
 **********************************************************************************************************************/
void LayoutView::mousePressEvent(QMouseEvent *event)
{
    // Explicit orbit shortcuts (also available via plain left-drag after threshold).
    if (m_viewMode == ViewMode::Iso3D
        && (event->button() == Qt::RightButton
            || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)))) {
        m_orbiting = true;
        m_leftPressPending = false;
        m_panLast = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Middle button or Alt+Left: pan.
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_panning = true;
        m_leftPressPending = false;
        m_panLast = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) {
        m_leftPressPending = false;
        setFocus(Qt::MouseFocusReason);
        const QPointF p = mapToScene(event->pos());
        if (!m_measureHasStart || m_measureHasEnd) {
            m_measureStart = p;
            m_measureHasStart = true;
            m_measureHasEnd = false;
            m_measureEnd = p;
        } else {
            m_measureEnd = p;
            m_measureHasEnd = true;
        }
        emitMeasure();
        viewport()->update();
        event->accept();
        return;
    }

    // Plain left: defer select vs drag (orbit in 3D / pan in 2D).
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
        setFocus(Qt::MouseFocusReason);
        m_leftPressPending = true;
        m_pressPos = event->pos();
        m_panLast = event->pos();
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void LayoutView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_leftPressPending && !m_orbiting && !m_panning) {
        if ((event->pos() - m_pressPos).manhattanLength() >= 6) {
            m_leftPressPending = false;
            if (m_viewMode == ViewMode::Iso3D)
                m_orbiting = true;
            else
                m_panning = true;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
    }

    if (m_orbiting) {
        const QPoint delta = event->pos() - m_panLast;
        m_panLast = event->pos();
        m_yawDeg += delta.x() * 0.4;
        m_pitchDeg = qBound(-85.0, m_pitchDeg - delta.y() * 0.4, 85.0);
        m_zoomLocked = true;
        if (isFieldVolume()) {
            if (m_fieldStatusLbl)
                m_fieldStatusLbl->setText(tr("Orbit… release to update"));
        } else if (!m_polys.isEmpty()) {
            rebuildScene(false);
        }
        event->accept();
        return;
    }

    if (m_panning) {
        const QPoint delta = event->pos() - m_panLast;
        m_panLast = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_zoomLocked = true;
        event->accept();
        return;
    }

    m_cursorView = event->pos();
    m_cursorScene = mapToScene(event->pos());
    m_cursorValid = true;
    const QPointF g = sceneToGdsUm(m_cursorScene);
    emit cursorUmChanged(g.x(), g.y());

    if (m_measureHasStart && !m_measureHasEnd) {
        m_measureEnd = m_cursorScene;
        emitMeasure();
    }
    viewport()->update();
    QGraphicsView::mouseMoveEvent(event);
}

void LayoutView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_leftPressPending) {
        m_leftPressPending = false;
        // Click without drag → select shape under press point.
        struct Hit { QString name; QString kind; int gds = -1; };
        QVector<Hit> stack;
        QSet<QString> seen;
        for (QGraphicsItem *it : items(m_pressPos)) {
            const QString name = it->data(kRoleName).toString();
            if (name.isEmpty() || seen.contains(name))
                continue;
            seen.insert(name);
            const int gds = it->data(kRoleGds).isValid() ? it->data(kRoleGds).toInt() : -1;
            stack.append({name, it->data(kRoleKind).toString(), gds});
        }
        if (!stack.isEmpty()) {
            int idx = 0;
            for (int i = 0; i < stack.size(); ++i) {
                if (stack.at(i).name == m_highlightedName) {
                    idx = (i + 1) % stack.size();
                    break;
                }
            }
            const Hit &pick = stack.at(idx);
            setHighlightedLayer(pick.name);
            emit layerClicked(pick.name, pick.kind, pick.gds);
        }
        event->accept();
        return;
    }

    if (m_orbiting
        && (event->button() == Qt::RightButton || event->button() == Qt::LeftButton)) {
        m_orbiting = false;
        viewport()->setCursor(Qt::ArrowCursor);
        if (isFieldVolume())
            emitFieldSliceRequest();
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        viewport()->setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void LayoutView::leaveEvent(QEvent *event)
{
    m_cursorValid = false;
    viewport()->update();
    QGraphicsView::leaveEvent(event);
}

void LayoutView::setShowCoordinates(bool on)
{
    if (m_showCoordinates == on)
        return;
    m_showCoordinates = on;
    viewport()->update();
}

bool LayoutView::showCoordinates() const
{
    return m_showCoordinates;
}

void LayoutView::drawForeground(QPainter *painter, const QRectF &/*rect*/)
{
    // Measure line in scene coordinates (view transform still active).
    if (m_measureHasStart) {
        const QPointF end = (m_measureHasEnd || m_cursorValid) ? m_measureEnd : m_measureStart;
        QPen pen(QColor(0, 0, 0));
        pen.setWidth(0);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->drawLine(m_measureStart, end);
        const qreal r = 0.2;
        painter->setBrush(Qt::black);
        painter->drawEllipse(m_measureStart, r, r);
        painter->drawEllipse(end, r, r);
    }

    if (!m_cursorValid && !m_measureHasStart)
        return;

    painter->save();
    painter->resetTransform();

    QFont font = painter->font();
    font.setPointSize(9);
    font.setBold(true);
    painter->setFont(font);
    const QFontMetrics fm(font);

    QStringList lines;
    if (m_showCoordinates && m_cursorValid) {
        const QPointF g = sceneToGdsUm(m_cursorScene);
        lines << QStringLiteral("X=%1  Y=%2 µm")
                     .arg(g.x(), 0, 'f', 3)
                     .arg(g.y(), 0, 'f', 3);
    }
    if (m_measureHasStart) {
        const QPointF a = sceneToGdsUm(m_measureStart);
        const QPointF b = sceneToGdsUm(m_measureHasEnd || m_cursorValid ? m_measureEnd : m_measureStart);
        const qreal dx = b.x() - a.x();
        const qreal dy = b.y() - a.y();
        const qreal len = std::hypot(dx, dy);
        lines << QStringLiteral("ΔX=%1  ΔY=%2  L=%3")
                     .arg(dx, 0, 'f', 3)
                     .arg(dy, 0, 'f', 3)
                     .arg(len, 0, 'f', 3);
    }

    if (!lines.isEmpty()) {
        int w = 0;
        int h = 0;
        for (const QString &ln : lines) {
            w = qMax(w, fm.horizontalAdvance(ln));
            h += fm.height();
        }
        const int pad = 4;
        const int boxW = w + 2 * pad;
        const int boxH = h + 2 * pad;
        int x = m_cursorView.x() + 14;
        int y = m_cursorView.y() + 16;
        if (x + boxW > width() - 4)
            x = m_cursorView.x() - boxW - 10;
        if (y + boxH > height() - 4)
            y = m_cursorView.y() - boxH - 10;
        if (x < 4)
            x = 4;
        if (y < 4)
            y = 4;

        const QRect box(x, y, boxW, boxH);
        painter->setPen(QPen(QColor(0, 0, 0, 80), 1));
        painter->setBrush(QColor(255, 255, 255, 230));
        painter->drawRoundedRect(box, 3, 3);
        painter->setPen(QColor(20, 20, 20));
        int ty = box.top() + pad + fm.ascent();
        for (const QString &ln : lines) {
            painter->drawText(box.left() + pad, ty, ln);
            ty += fm.height();
        }
    }

    painter->restore();
}

void LayoutView::clearMeasure()
{
    m_measureHasStart = false;
    m_measureHasEnd = false;
    m_measureStart = QPointF();
    m_measureEnd = QPointF();
    emit measureChanged(false, 0, 0, 0);
    viewport()->update();
}

void LayoutView::emitMeasure()
{
    if (!m_measureHasStart) {
        emit measureChanged(false, 0, 0, 0);
        return;
    }
    const QPointF a = sceneToGdsUm(m_measureStart);
    const QPointF b = sceneToGdsUm(m_measureEnd);
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    emit measureChanged(true, dx, dy, std::hypot(dx, dy));
}


qreal LayoutView::opacityFor(int gdsLayer) const
{
    return m_layerOpacity.value(gdsLayer, 1.0);
}

bool LayoutView::visibleFor(int gdsLayer) const
{
    return m_layerVisible.value(gdsLayer, true);
}

bool LayoutView::isLayerVisible(int gdsLayer) const
{
    return visibleFor(gdsLayer);
}

qreal LayoutView::layerOpacity(int gdsLayer) const
{
    return opacityFor(gdsLayer);
}

void LayoutView::setLayerVisible(int gdsLayer, bool visible)
{
    m_layerVisible.insert(gdsLayer, visible);
    applyLayerVisual(gdsLayer);
}

void LayoutView::setLayerOpacity(int gdsLayer, qreal opacity)
{
    m_layerOpacity.insert(gdsLayer, qBound(0.0, opacity, 1.0));
    applyLayerVisual(gdsLayer);
}

void LayoutView::applyLayerVisual(int gdsLayer)
{
    if (!m_scene)
        return;
    const bool vis = visibleFor(gdsLayer);
    const qreal op = opacityFor(gdsLayer);

    for (QGraphicsItem *item : m_scene->items()) {
        if (item->data(kRoleGds).toInt() != gdsLayer)
            continue;
        item->setVisible(vis);

        const bool isHi = !m_highlightedName.isEmpty()
                && item->data(kRoleName).toString() == m_highlightedName;

        if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item)) {
            QColor c = isHi ? QColor(255, 220, 0)
                            : (item->data(kRolePen).isValid()
                               ? item->data(kRolePen).value<QColor>()
                               : QColor(220, 40, 180));
            c.setAlpha(qBound(40, int(255 * op + 0.5), 255));
            QPen p(c);
            p.setCosmetic(true);
            p.setWidth(isHi ? kPortPenWidth + 1 : kPortPenWidth);
            p.setCapStyle(Qt::FlatCap);
            line->setPen(p);
            continue;
        }

        if (auto *text = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item)) {
            QColor c = isHi ? QColor(255, 180, 0)
                            : (item->data(kRolePen).isValid()
                               ? item->data(kRolePen).value<QColor>()
                               : QColor(220, 40, 180));
            c.setAlpha(qBound(40, int(255 * op + 0.5), 255));
            text->setBrush(c);
            continue;
        }

        if (auto *shape = qgraphicsitem_cast<QAbstractGraphicsShapeItem *>(item)) {
            if (isHi) {
                if (!item->data(kRoleBrush).isValid())
                    item->setData(kRoleBrush, shape->brush().color());
                const QColor base = item->data(kRoleBrush).value<QColor>();
                QColor fill(qMin(255, int(0.35 * base.red()   + 0.65 * 255)),
                            qMin(255, int(0.35 * base.green() + 0.65 * 210)),
                            qMin(255, int(0.35 * base.blue()  + 0.65 * 0)));
                fill.setAlpha(qBound(40, int(230 * op + 0.5), 255));
                shape->setBrush(fill);
            } else {
                QColor fill = shape->brush().color();
                if (item->data(kRoleBrush).isValid())
                    fill = item->data(kRoleBrush).value<QColor>();
                fill.setAlpha(qBound(0, int(kBaseFillAlpha * op + 0.5), 255));
                shape->setBrush(fill);
                item->setData(kRoleBrush, QVariant());
            }
        }
    }
    viewport()->update();
}

void LayoutView::addPortArrow(const QPointF &origin,
                              const QPointF &dirScene,
                              const QColor &color,
                              const QString &name,
                              const QString &kind,
                              int gdsLayer,
                              bool visible,
                              qreal z,
                              const QString &dirLabel)
{
    QPointF d = dirScene;
    const qreal len = std::hypot(d.x(), d.y());
    if (len < 1e-12)
        d = QPointF(1, 0);
    else
        d /= len;

    // Pixel-space arrow (ItemIgnoresTransformations): tip at origin (injection point).
    const qreal tip = 10.0;
    const qreal wing = 5.0;
    const QPointF n(-d.y(), d.x());
    QPolygonF head;
    head << QPointF(0, 0)
         << (-d * tip + n * wing)
         << (-d * tip - n * wing);

    auto *arrow = m_scene->addPolygon(head, QPen(Qt::NoPen), QBrush(color));
    arrow->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    arrow->setPos(origin);
    arrow->setData(kRoleName, name);
    arrow->setData(kRoleKind, kind);
    arrow->setData(kRoleGds, gdsLayer);
    arrow->setData(kRoleIsPort, true);
    arrow->setData(kRolePen, color);
    arrow->setVisible(visible);
    arrow->setZValue(z);
    arrow->setToolTip(QStringLiteral("%1 direction %2").arg(name, dirLabel));

    auto *dl = m_scene->addSimpleText(dirLabel);
    QFont f = dl->font();
    f.setPointSize(8);
    f.setBold(true);
    dl->setFont(f);
    dl->setBrush(color);
    dl->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    dl->setPos(origin);
    setPixelOffset(dl, 8, 6);
    dl->setData(kRoleName, name);
    dl->setData(kRoleKind, kind);
    dl->setData(kRoleGds, gdsLayer);
    dl->setData(kRoleIsPort, true);
    dl->setData(kRolePen, color);
    dl->setVisible(visible);
    dl->setZValue(z + 0.05);
}

void LayoutView::addPortInwardArrow(const QPointF &tipScene,
                                    const QPointF &towardScene,
                                    const QColor &color,
                                    const QString &name,
                                    const QString &kind,
                                    int gdsLayer,
                                    bool visible,
                                    qreal z,
                                    const QString &dirLabel)
{
    QPointF into = towardScene - tipScene;
    if (std::hypot(into.x(), into.y()) < 1e-9)
        into = QPointF(1, 0);
    // Tip on the port edge; direction into the layout (injection).
    addPortArrow(tipScene, into, color, name, kind, gdsLayer, visible, z, dirLabel);
}

void LayoutView::addPortArrowAlong(const QPointF &tailScene,
                                   const QPointF &tipScene,
                                   const QColor &color,
                                   const QString &name,
                                   const QString &kind,
                                   int gdsLayer,
                                   bool visible,
                                   qreal z,
                                   const QString &dirLabel)
{
    QPointF delta = tipScene - tailScene;
    qreal len = std::hypot(delta.x(), delta.y());
    QPointF tail = tailScene;
    if (len < 1e-6) {
        delta = QPointF(0, -1);
        len = 1.0;
        tail = tipScene - delta;
    }

    QColor penColor = color;
    if (penColor.alpha() < 40)
        penColor.setAlpha(200);
    QPen shaft(penColor);
    shaft.setCosmetic(true);
    shaft.setWidth(4);
    shaft.setCapStyle(Qt::RoundCap);

    auto *line = m_scene->addLine(QLineF(tail, tipScene), shaft);
    line->setData(kRoleName, name);
    line->setData(kRoleKind, kind);
    line->setData(kRoleGds, gdsLayer);
    line->setData(kRoleIsPort, true);
    line->setData(kRolePen, color);
    line->setVisible(visible);
    line->setZValue(z);
    line->setToolTip(QStringLiteral("%1 direction %2").arg(name, dirLabel));

    // Arrowhead in pixel space at the tip (touches injection face).
    addPortArrow(tipScene, tipScene - tail, color, name, kind, gdsLayer, visible, z + 0.02,
                 dirLabel);
}

#endif // QT_VERSION
