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
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/

#include "layoutview.h"
#include "appsettings.h"

#include <QMetaMethod>

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
#include <QButtonGroup>
#include <QIcon>
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
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QClipboard>
#include <QPainterPath>
#include <QRegion>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QElapsedTimer>
#include <QHash>
#include <QPair>
#include <QtGlobal>
#include <QtMath>
#include <cmath>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

namespace {

/*! ADS-style via-array envelope for Iso3D (display-only).
 *  Grow → cluster overlapping expanded AABBs (union-find) → one solid bar per array.
 *  Avoids QPainterPath::simplified() which is O(n²)-ish on thousands of vias and freezes the UI. */
QVector<QPolygonF> growEnvelopeMerge(const QVector<QPolygonF> &polys, qreal growUm)
{
    QVector<QPolygonF> out;
    const int n = polys.size();
    if (n <= 0)
        return out;
    if (n == 1) {
        out.push_back(polys.first());
        return out;
    }

    QVector<QRectF> orig;
    QVector<QRectF> grown;
    orig.reserve(n);
    grown.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (polys.at(i).size() < 3)
            continue;
        const QRectF br = polys.at(i).boundingRect();
        if (!br.isValid() || br.width() <= 0 || br.height() <= 0)
            continue;
        orig.push_back(br);
        grown.push_back(br.adjusted(-growUm, -growUm, growUm, growUm));
    }
    const int m = orig.size();
    if (m == 0)
        return out;
    if (m == 1) {
        const QRectF &r = orig.first();
        QPolygonF p;
        p << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft() << r.topLeft();
        out.push_back(p);
        return out;
    }

    QVector<int> parent(m);
    for (int i = 0; i < m; ++i)
        parent[i] = i;
    auto find = [&](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&](int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b)
            parent[b] = a;
    };

    // Spatial hash so we only test nearby vias (not O(n²) AABB checks).
    const qreal cell = qMax(growUm * 4.0, 1.0);
    QHash<QPair<int, int>, QVector<int>> grid;
    auto keyOf = [&](const QRectF &r) {
        const qreal cx = r.center().x();
        const qreal cy = r.center().y();
        return qMakePair(int(std::floor(cx / cell)), int(std::floor(cy / cell)));
    };
    for (int i = 0; i < m; ++i) {
        const auto key = keyOf(grown.at(i));
        const int gx = key.first;
        const int gy = key.second;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                const auto it = grid.constFind(qMakePair(gx + dx, gy + dy));
                if (it == grid.cend())
                    continue;
                for (int j : *it) {
                    if (grown.at(i).intersects(grown.at(j)))
                        unite(i, j);
                }
            }
        }
        grid[key].push_back(i);
    }

    QHash<int, QRectF> clusterBox;
    for (int i = 0; i < m; ++i) {
        const int root = find(i);
        auto it = clusterBox.find(root);
        if (it == clusterBox.end())
            clusterBox.insert(root, orig.at(i));
        else
            *it = it->united(orig.at(i));
    }

    out.reserve(clusterBox.size());
    for (auto it = clusterBox.cbegin(); it != clusterBox.cend(); ++it) {
        const QRectF &r = it.value();
        QPolygonF p;
        p << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft() << r.topLeft();
        out.push_back(p);
    }
    return out;
}

/*! Via array merge as gds2palace / gds2openEMS do it (util_gds_reader.merge_via_array):
 *  grow each via by spacing/2 + 0.01 µm, unite, shrink back by the same amount. Done exactly for
 *  axis-aligned boxes with QRegion on a 1 nm grid, one cluster of touching vias at a time. A lone
 *  via keeps its own outline. */
QVector<QPolygonF> mergeViaArray(const QVector<QPolygonF> &vias, qreal spacingUm)
{
    QVector<QPolygonF> out;
    constexpr qreal kNmPerUm = 1000.0;
    const int off = qMax(1, qRound((spacingUm / 2.0 + 0.01) * kNmPerUm));

    QVector<QRect> boxes;
    QVector<int> source;
    for (int i = 0; i < vias.size(); ++i) {
        const QRectF br = vias.at(i).boundingRect();
        if (vias.at(i).size() < 3 || br.width() <= 0 || br.height() <= 0) {
            out.push_back(vias.at(i));
            continue;
        }
        const int x0 = qFloor(br.left() * kNmPerUm);
        const int y0 = qFloor(br.top() * kNmPerUm);
        const int x1 = qCeil(br.right() * kNmPerUm);
        const int y1 = qCeil(br.bottom() * kNmPerUm);
        boxes.push_back(QRect(QPoint(x0, y0), QPoint(x1 - 1, y1 - 1)));
        source.push_back(i);
    }

    // Clusters of vias whose grown boxes overlap (spatial hash + union-find).
    const int m = boxes.size();
    QVector<int> parent(m);
    for (int i = 0; i < m; ++i)
        parent[i] = i;
    auto find = [&](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    const int cell = qMax(4 * off, 1000);
    QHash<QPair<int, int>, QVector<int>> grid;
    for (int i = 0; i < m; ++i) {
        const QRect g = boxes.at(i).adjusted(-off, -off, off, off);
        const int gx = g.center().x() / cell;
        const int gy = g.center().y() / cell;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                const auto it = grid.constFind(qMakePair(gx + dx, gy + dy));
                if (it == grid.cend())
                    continue;
                for (int j : *it)
                    if (g.intersects(boxes.at(j).adjusted(-off, -off, off, off)))
                        parent[find(j)] = find(i);
            }
        grid[qMakePair(gx, gy)].push_back(i);
    }
    QHash<int, QVector<int>> clusters;
    for (int i = 0; i < m; ++i)
        clusters[find(i)].push_back(i);

    for (auto it = clusters.cbegin(); it != clusters.cend(); ++it) {
        const QVector<int> &members = it.value();
        if (members.size() == 1) {
            out.push_back(vias.at(source.at(members.first())));
            continue;
        }
        // Closing: dilate by off, then erode by off (erosion = complement of the dilated complement).
        QRegion grown;
        for (int k : members)
            grown += boxes.at(k).adjusted(-off, -off, off, off);
        const QRect frame = grown.boundingRect().adjusted(-off - 1, -off - 1, off + 1, off + 1);
        const QRegion outside = QRegion(frame) - grown;
        QRegion outsideGrown;
        for (const QRect &r : outside)
            outsideGrown += r.adjusted(-off, -off, off, off);
        const QRegion merged = grown - outsideGrown;
        if (merged.isEmpty()) {
            for (int k : members)
                out.push_back(vias.at(source.at(k)));
            continue;
        }
        QPainterPath path;
        path.addRegion(merged);
        path = path.simplified();
        for (const QPolygonF &poly : path.toFillPolygons()) {
            QPolygonF um;
            um.reserve(poly.size());
            for (const QPointF &p : poly)
                um << QPointF(p.x() / kNmPerUm, p.y() / kNmPerUm);
            out.push_back(um);
        }
    }
    return out;
}

/*! Small isometric cube outline, the icon of the "3D viewer" button. */
QIcon cubeIcon(const QColor &color)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 2.4);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const QPointF top(16, 4), left(5, 10), right(27, 10), mid(16, 16);
    const QPointF bl(5, 22), br(27, 22), bottom(16, 28);
    p.setBrush(QColor(color.red(), color.green(), color.blue(), 60));
    p.drawPolygon(QPolygonF({top, right, mid, left}));        // top face
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(QPolygonF({left, bl, bottom, br, right}));
    p.drawLine(mid, bottom);
    p.end();
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}

qreal estimateViaGrowUm(const QVector<QPolygonF> &polys)
{
    if (polys.isEmpty())
        return 0.35;
    // Median half-width of vias. Array pitch is typically ~2× via size (gap ≈ size),
    // so grow must be ≥ half-gap ≈ half-width; use ~2× with headroom for PDK pitch stretch.
    QVector<qreal> half;
    half.reserve(qMin(polys.size(), 256));
    for (int i = 0; i < polys.size() && half.size() < 256; ++i) {
        const QRectF br = polys.at(i).boundingRect();
        if (br.width() > 0 && br.height() > 0)
            half.push_back(0.5 * qMin(br.width(), br.height()));
    }
    if (half.isEmpty())
        return 0.35;
    std::nth_element(half.begin(), half.begin() + half.size() / 2, half.end());
    const qreal med = half.at(half.size() / 2);
    // Bridge gaps up to ~2× via size (IHP-like arrays); clamp for safety.
    return qBound(0.08, med * 2.2, 2.0);
}

} // namespace

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

    // Substrate: [2D | 3D] segmented switch, the active side filled. Fields: a separate
    // "3D viewer ↗" button that opens the 3D field viewer window (the pane itself stays 2D).
    m_modeSwitch = new QWidget(this);
    m_modeSwitch->setObjectName(QStringLiteral("layoutViewModeSwitch"));
    auto *switchLayout = new QHBoxLayout(m_modeSwitch);
    switchLayout->setContentsMargins(0, 0, 0, 0);
    switchLayout->setSpacing(0);
    auto *modeGroup = new QButtonGroup(m_modeSwitch);
    modeGroup->setExclusive(true);
    auto makeSide = [&](const QString &name, const QString &text, bool left) {
        auto *b = new QToolButton(m_modeSwitch);
        b->setObjectName(name);
        b->setText(text);
        b->setCheckable(true);
        b->setAutoRaise(false);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(34, 26);
        b->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  background: rgba(255,255,255,225); color: #404040;"
            "  border: 1px solid #7a7a7a; %1"
            "  font-weight: bold; font-size: 11px;"
            "}"
            "QToolButton:hover:!checked { background: rgba(225,235,248,235); }"
            "QToolButton:checked {"
            "  background: rgba(40,100,180,225); color: white; border-color: #245a9e;"
            "}")
            .arg(left ? QStringLiteral("border-right: none; border-top-left-radius: 4px;"
                                       " border-bottom-left-radius: 4px;")
                      : QStringLiteral("border-top-right-radius: 4px; border-bottom-right-radius: 4px;")));
        modeGroup->addButton(b);
        switchLayout->addWidget(b);
        return b;
    };
    m_mode2dBtn = makeSide(QStringLiteral("layoutViewMode2dBtn"), QStringLiteral("2D"), true);
    m_mode3dBtn = makeSide(QStringLiteral("layoutViewMode3dBtn"), QStringLiteral("3D"), false);
    m_modeSwitch->setFixedSize(68, 26);
    connect(m_mode2dBtn, &QToolButton::clicked, this, [this]() { setViewMode(ViewMode::Top2D); });
    connect(m_mode3dBtn, &QToolButton::clicked, this, [this]() { setViewMode(ViewMode::Iso3D); });

    m_field3dBtn = new QToolButton(this);
    m_field3dBtn->setObjectName(QStringLiteral("layoutViewField3dBtn"));
    m_field3dBtn->setCursor(Qt::PointingHandCursor);
    m_field3dBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_field3dBtn->setIcon(cubeIcon(QColor(36, 90, 158)));
    m_field3dBtn->setIconSize(QSize(16, 16));
    m_field3dBtn->setText(tr("3D viewer ↗"));
    m_field3dBtn->setFixedHeight(26);
    m_field3dBtn->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  background: rgba(255,255,255,235); color: #245a9e;"
        "  border: 2px solid #245a9e; border-radius: 5px;"
        "  font-weight: bold; font-size: 11px; padding: 0 8px 0 5px;"
        "}"
        "QToolButton:hover { background: rgba(225,235,248,245); }"
        "QToolButton:pressed { background: rgba(40,100,180,225); color: white; }"));
    m_field3dBtn->hide();
    connect(m_field3dBtn, &QToolButton::clicked, this, &LayoutView::fieldExternalVolumeRequested);

    m_fieldBtn = new QToolButton(this);
    m_fieldBtn->setObjectName(QStringLiteral("layoutViewFieldBtn"));
    m_fieldBtn->setCheckable(true);
    m_fieldBtn->setAutoRaise(false);
    m_fieldBtn->setCursor(Qt::PointingHandCursor);
    m_fieldBtn->setFixedSize(48, 26);
    m_fieldBtn->setText(QStringLiteral("Field"));
    m_fieldBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(255,255,255,220); border: 1px solid #7a7a7a;"
        "  border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QToolButton:checked { background: rgba(40,100,180,210); color: white; border-color: #245a9e; }"
        "QToolButton:disabled { background: rgba(220,220,220,200); color: #888; }"));
    m_fieldBtn->setToolTip(tr("Field view: Z-clip heatmap.\n"
                              "While Field is on, 3D opens the Field 3D viewer window.\n"
                              "Click the heatmap to probe the value (Esc clears).\n"
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
            "QCheckBox { font-size: 10px; }"
            "QComboBox { font-size: 10px; }"));
    auto *panelLay = new QVBoxLayout(m_fieldPanel);
    panelLay->setContentsMargins(6, 3, 6, 3);
    panelLay->setSpacing(2);
    m_fieldChoiceCombo = new QComboBox(m_fieldPanel);
    m_fieldChoiceCombo->setObjectName(QStringLiteral("layoutViewFieldChoice"));
    m_fieldChoiceCombo->setFixedWidth(168);
    m_fieldChoiceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fieldChoiceCombo->setToolTip(tr("Result file / frequency shown in the slice (and opened in 3D)."));
    m_fieldChoiceCombo->setVisible(false);
    panelLay->addWidget(m_fieldChoiceCombo);
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
    m_fieldTempChk = new QCheckBox(tr("Probe"), m_fieldPanel);
    m_fieldTempChk->setChecked(true);
    m_fieldTempChk->setToolTip(tr("Click the Field heatmap to probe the value.\nEsc clears the probe."));
    optRow->addWidget(m_fieldLogChk);
    optRow->addWidget(m_fieldTempChk);
    optRow->addStretch(1);
    panelLay->addLayout(optRow);
    m_fieldPanel->setVisible(false);
    m_fieldPanel->adjustSize();

    connect(m_fieldZSlider, &QSlider::valueChanged, this, &LayoutView::onFieldZSliderPreview);
    connect(m_fieldZSlider, &QSlider::sliderReleased, this, &LayoutView::onFieldZSliderCommitted);
    connect(m_fieldHotZBtn, &QToolButton::clicked, this, &LayoutView::onFieldHotZClicked);
    connect(m_fieldLogChk, &QCheckBox::toggled, this, &LayoutView::onFieldControlsChanged);
    connect(m_fieldTempChk, &QCheckBox::toggled, this, &LayoutView::onFieldTempToggled);
    connect(m_fieldChoiceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LayoutView::fieldChoiceChanged);

    // PgUp / PgDn step the Z slider; export once the keys settle.
    m_fieldZKeyTimer = new QTimer(this);
    m_fieldZKeyTimer->setSingleShot(true);
    m_fieldZKeyTimer->setInterval(350);
    connect(m_fieldZKeyTimer, &QTimer::timeout, this, &LayoutView::onFieldZSliderCommitted);

    loadViewModeFromSettings();
    {
        const QSignalBlocker block(m_fieldBtn);
        m_fieldBtn->setChecked(m_fieldOn);
    }
    syncFloatingControls();
    m_modeSwitch->raise();
    m_field3dBtn->raise();
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
    m_iso3dSceneHalf = 0.0;
    m_polys.clear();
    m_rawPolys.clear();
    m_styles.clear();
    m_ports.clear();
    m_highlightedName.clear();
    m_field = FieldOverlay{};
    m_fieldProbeActive = false;
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
    m_iso3dSceneHalf = 0.0;
    m_rawPolys = polys;
    m_styles = styles;
    m_ports = ports;
    m_zoomLocked = false;
    applyViaMerge();
    rebuildScene();
}

/*!*******************************************************************************************************************
 * \brief Sets the via array merge distance of the model (\c merge_polygon_size).
 *
 * > 0: 2D and 3D show via layers merged like gds2palace / gds2openEMS do (the simulated geometry).
 * 0: every via is shown. < 0 (model without the setting): 2D shows every via, Iso3D merges dense
 * via layers with its own display-only estimate.
 *
 * \param um Merge distance in µm.
 **********************************************************************************************************************/
void LayoutView::setViaMergeSize(qreal um)
{
    if (qFuzzyCompare(um + 10.0, m_viaMergeUm + 10.0))
        return;
    m_viaMergeUm = um;
    if (m_rawPolys.isEmpty())
        return;
    applyViaMerge();
    if (m_viewMode == ViewMode::Iso3D)
        m_iso3dSceneHalf = 0.0;
    rebuildScene(false);
}

/*!*******************************************************************************************************************
 * \brief Builds \c m_polys from \c m_rawPolys: via layers merged when \c m_viaMergeUm > 0.
 **********************************************************************************************************************/
void LayoutView::applyViaMerge()
{
    if (m_viaMergeUm <= 0.0) {
        m_polys = m_rawPolys;
        return;
    }
    QVector<GdsFlatPolygon> out;
    out.reserve(m_rawPolys.size());
    QHash<int, QVector<QPolygonF>> viasByLayer;
    for (const GdsFlatPolygon &p : m_rawPolys) {
        const bool isVia = m_styles.contains(p.layer)
                && m_styles.value(p.layer).kind.compare(QLatin1String("via"), Qt::CaseInsensitive) == 0;
        if (isVia)
            viasByLayer[p.layer].push_back(p.pointsUm);
        else
            out.push_back(p);
    }
    for (auto it = viasByLayer.cbegin(); it != viasByLayer.cend(); ++it) {
        for (const QPolygonF &poly : mergeViaArray(it.value(), m_viaMergeUm)) {
            GdsFlatPolygon gp;
            gp.layer = it.key();
            gp.pointsUm = poly;
            out.push_back(gp);
        }
    }
    m_polys = out;
}

void LayoutView::rebuildScene(bool refit)
{
    const QString keepHighlight = m_highlightedName;
    // Iso3D redraw without refit (orbit, opacity): the square sceneRect does not change, so
    // restoring the exact scroll position keeps the view still (centerOn would round and drift).
    const bool keepScroll = !refit && m_viewMode == ViewMode::Iso3D && !m_fieldOn;
    const int hScroll = horizontalScrollBar()->value();
    const int vScroll = verticalScrollBar()->value();
    m_cursorValid = false;
    clearMeasure();
    m_scene->clear();
    m_scene->setSceneRect(QRectF());

    if (isFieldVolume()) {
        m_scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
        addFieldVolumeItems();
        m_highlightedName = keepHighlight;
        if (refit && !m_zoomLocked)
            fitPreferredContent();
        repositionFloatingControls();
        return;
    }

    // Too many via polygons: a message instead of the layout (2D and 3D); a Field heatmap stays.
    const QStringList dense = denseViaLayers();
    if (!dense.isEmpty()) {
        if (!m_denseViaLabel) {
            m_denseViaLabel = new QLabel(this);
            m_denseViaLabel->setObjectName(QStringLiteral("layoutViewDenseViaLabel"));
            m_denseViaLabel->setWordWrap(true);
            m_denseViaLabel->setAlignment(Qt::AlignCenter);
            m_denseViaLabel->setStyleSheet(QStringLiteral(
                "QLabel { background: rgba(255,255,255,230); color: #202020; border: 1px solid #b0b0b0;"
                " border-radius: 4px; padding: 10px; }"));
        }
        m_denseViaLabel->setText(
            tr("Layout not shown: too many via polygons (limit %1 per layer).\n%2\n\n"
               "Set merge_polygon_size in the model to merge via arrays (as the simulation does), "
               "or raise the limit in Setup → Preferences → Layout Preview.")
                .arg(m_maxViaPolygons).arg(dense.join(QStringLiteral(", "))));
        m_denseViaLabel->show();
        if (m_fieldOn) {
            m_scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
            addFieldOverlayItems();
            if (refit && !m_zoomLocked)
                fitPreferredContent();
        }
        m_highlightedName = keepHighlight;
        repositionFloatingControls();
        return;
    }
    if (m_denseViaLabel)
        m_denseViaLabel->hide();

    if (m_viewMode == ViewMode::Iso3D) {
        // Dense Iso3D (thousands of vias) — BSP build dominates load/orbit time.
        m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
        rebuildScene3D(refit);
        if (keepScroll) {
            horizontalScrollBar()->setValue(hScroll);
            verticalScrollBar()->setValue(vScroll);
        }
    } else {
        m_scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
        rebuildScene2D(refit);
    }

    m_highlightedName = keepHighlight;
    applyHighlight();
    repositionFloatingControls();
}

/*!*******************************************************************************************************************
 * \brief Sets the most polygons a via layer may have before the layout is replaced by a message.
 *
 * Drawing thousands of single vias (no or a small merge_polygon_size) makes 2D and 3D slow.
 *
 * \param maxPolygons Limit per via layer, counted after via merging (at least 1).
 **********************************************************************************************************************/
void LayoutView::setMaxViaPolygonsPerLayer(int maxPolygons)
{
    maxPolygons = qMax(1, maxPolygons);
    if (maxPolygons == m_maxViaPolygons)
        return;
    m_maxViaPolygons = maxPolygons;
    if (!m_polys.isEmpty())
        rebuildScene();
}

/*!*******************************************************************************************************************
 * \brief Via layers with more drawn polygons than the limit.
 * \return "<layer name>: <count>" per layer over the limit, sorted by name.
 **********************************************************************************************************************/
QStringList LayoutView::denseViaLayers() const
{
    QHash<int, int> counts;
    for (const GdsFlatPolygon &p : m_polys) {
        const auto st = m_styles.constFind(p.layer);
        if (st != m_styles.cend() && st->kind.compare(QLatin1String("via"), Qt::CaseInsensitive) == 0)
            ++counts[p.layer];
    }
    QStringList out;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.value() > m_maxViaPolygons) {
            const QString name = m_styles.value(it.key()).name;
            out << QStringLiteral("%1: %2").arg(name.isEmpty() ? QStringLiteral("L%1").arg(it.key()) : name)
                                            .arg(it.value());
        }
    }
    out.sort();
    return out;
}

void LayoutView::scheduleOrbitRebuild()
{
    if (m_viewMode != ViewMode::Iso3D || m_polys.isEmpty() || isFieldVolume())
        return;
    if (!m_orbitRebuildTimer) {
        m_orbitRebuildTimer = new QTimer(this);
        m_orbitRebuildTimer->setSingleShot(true);
        m_orbitRebuildTimer->setInterval(33); // ~30 fps while dragging
        connect(m_orbitRebuildTimer, &QTimer::timeout, this, [this]() {
            if (m_viewMode == ViewMode::Iso3D && !m_polys.isEmpty() && !isFieldVolume())
                rebuildScene(false);
        });
    }
    if (!m_orbitRebuildTimer->isActive())
        m_orbitRebuildTimer->start();
}

void LayoutView::flushOrbitRebuild()
{
    if (m_orbitRebuildTimer && m_orbitRebuildTimer->isActive())
        m_orbitRebuildTimer->stop();
    if (m_viewMode == ViewMode::Iso3D && !m_polys.isEmpty() && !isFieldVolume())
        rebuildScene(false);
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

        const bool isPortLayer = (st.kind == QLatin1String("port")) || isPortLayerNumber(p.layer);
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
            const QString thermalTip = thermalMarkerToolTip(it.poly.layer, it.style.name);
            lineItem->setToolTip(thermalTip);

            // Direction from Ports table: in-plane arrows; Z → inward tip on injection edge.
            QString dir = m_ports.value(it.poly.layer).direction.trimmed().toLower();
            if (dir.isEmpty())
                dir = QStringLiteral("z");
            if (!thermalTip.isEmpty()) {
                // Thermal markers have no direction.
            } else if (dir.contains(QLatin1Char('z'))) {
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

            // Thermal markers (heat source / constant temperature) have no direction.
            const QString thermalTip = thermalMarkerToolTip(it.poly.layer, it.style.name);
            if (!thermalTip.isEmpty()) {
                item->setToolTip(thermalTip);
                label->setToolTip(thermalTip);
                continue;
            }

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
        if (isPortLayerNumber(p.layer))
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
    // Every orbit projects the layout box inside this radius around the orbit center.
    m_orbitRadius = 0.5 * std::sqrt((xHi - xLo) * (xHi - xLo) + (yHi - yLo) * (yHi - yLo)
                                    + (zHi - zLo) * (zHi - zLo));
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
    QElapsedTimer timer;
    timer.start();
    m_lastIso3dStats = Iso3dRebuildStats{};
    m_lastIso3dStats.inputPolyCount = m_polys.size();

    struct Item {
        GdsFlatPolygon poly;
        LayerStyle style;
    };
    QVector<Item> items;
    items.reserve(m_polys.size());

    updateOrbitCenter();

    double zLo = m_orbitCz - 0.5;
    double zHi = m_orbitCz + 0.5;
    bool anyZ = false;
    for (const GdsFlatPolygon &p : m_polys) {
        if (isPortLayerNumber(p.layer))
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

    constexpr int kViaMergePerLayer = 48;
    QHash<int, QVector<QPolygonF>> viasByLayer;
    QHash<int, LayerStyle> viaStyleByLayer;
    int viaPolyCount = 0;
    QVector<Item> extrudeItems;
    extrudeItems.reserve(items.size());

    for (const Item &it : items) {
        const bool isPort = (it.style.kind == QLatin1String("port")) || isPortLayerNumber(it.poly.layer);
        if (isPort)
            continue;
        const bool isVia = (it.style.kind.compare(QLatin1String("via"), Qt::CaseInsensitive) == 0);
        if (isVia && it.poly.pointsUm.size() >= 3) {
            viasByLayer[it.poly.layer].push_back(it.poly.pointsUm);
            viaStyleByLayer.insert(it.poly.layer, it.style);
            ++viaPolyCount;
        } else {
            extrudeItems.push_back(it);
        }
    }
    m_lastIso3dStats.viaPolyCount = viaPolyCount;

    bool anyMerged = false;
    int viaEnvelopeCount = 0;
    for (auto it = viasByLayer.cbegin(); it != viasByLayer.cend(); ++it) {
        const LayerStyle st = viaStyleByLayer.value(it.key());
        // Own display-only estimate only when the model gives no merge distance (m_viaMergeUm < 0);
        // with one, m_polys already holds the merged vias (applyViaMerge).
        const bool mergeLayer = m_viaMergeUm < 0.0 && it.value().size() >= kViaMergePerLayer;
        if (mergeLayer) {
            const qreal grow = estimateViaGrowUm(it.value());
            const QVector<QPolygonF> envelopes = growEnvelopeMerge(it.value(), grow);
            // Only count as merged when growEnvelope actually collapsed the array.
            if (!envelopes.isEmpty() && envelopes.size() < it.value().size()) {
                anyMerged = true;
                viaEnvelopeCount += envelopes.size();
                for (const QPolygonF &env : envelopes) {
                    GdsFlatPolygon gp;
                    gp.layer = it.key();
                    gp.pointsUm = env;
                    extrudeItems.push_back({gp, st});
                }
            } else {
                viaEnvelopeCount += it.value().size();
                for (const QPolygonF &poly : it.value()) {
                    GdsFlatPolygon gp;
                    gp.layer = it.key();
                    gp.pointsUm = poly;
                    extrudeItems.push_back({gp, st});
                }
            }
        } else {
            viaEnvelopeCount += it.value().size();
            for (const QPolygonF &poly : it.value()) {
                GdsFlatPolygon gp;
                gp.layer = it.key();
                gp.pointsUm = poly;
                extrudeItems.push_back({gp, st});
            }
        }
    }
    m_lastIso3dStats.mergedVias = anyMerged;
    m_lastIso3dStats.viaEnvelopeCount = viaEnvelopeCount;

    struct Face {
        QPolygonF poly;
        QColor fill;
        QString name;
        QString kind;
        int gds = 0;
        bool isPort = false;
        qreal depth = 0.0;   //!< Camera depth (farther = larger); tie-break within a stack band
        qreal zMid = 0.0;    //!< Stack mid-Z [µm] — primary back-to-front key
        int faceKind = 1;    //!< 1 = wall, 2 = cap (top, or bottom when seen from below)
    };
    QVector<Face> faces;
    faces.reserve(extrudeItems.size() * 5);

    constexpr qreal kMinThickUm = 0.05;
    QRectF portBounds;

    // Ports first (interactive items — few).
    for (const Item &it : items) {
        if (it.poly.pointsUm.size() < 2)
            continue;

        const qreal op = opacityFor(it.poly.layer);
        const bool vis = visibleFor(it.poly.layer);
        const bool isPort = (it.style.kind == QLatin1String("port")) || isPortLayerNumber(it.poly.layer);
        if (!isPort)
            continue;

        // Port surface as gds2palace builds it, from the polygon's bounding box.
        const QRectF bb = it.poly.pointsUm.boundingRect();
        const PortInfo pi = m_ports.value(it.poly.layer);
        QString dir = pi.direction.trimmed().toLower();
        if (dir.isEmpty())
            dir = QStringLiteral("z");

        QColor col = it.style.color;
        col.setAlpha(qBound(40, int(255 * op + 0.5), 255));
        const QString pname = it.style.name.isEmpty()
                ? QStringLiteral("P%1").arg(it.poly.layer - 200)
                : it.style.name;

        const QString thermalTip = thermalMarkerToolTip(it.poly.layer, pname);
        const QString sheetTip = thermalTip.isEmpty() ? tr("%1 port surface").arg(pname) : thermalTip;

        // Translucent sheet (or a line when it has no area) under the arrow; \a flatAsLine false
        // skips surfaces seen edge-on (box walls).
        auto addPortSheet = [&](const QPolygonF &scenePoly, bool flatAsLine = true) {
            QGraphicsItem *sheet = nullptr;
            QPen pen(col);
            pen.setCosmetic(true);
            qreal area2 = 0.0;  // twice the signed area (shoelace)
            for (int i = 0; i < scenePoly.size(); ++i) {
                const QPointF &p = scenePoly.at(i);
                const QPointF &q = scenePoly.at((i + 1) % scenePoly.size());
                area2 += p.x() * q.y() - q.x() * p.y();
            }
            const QRectF sb = scenePoly.boundingRect();
            if (std::abs(area2) < 1e-6 * std::max<qreal>(1e-12, sb.width() * sb.width() + sb.height() * sb.height())) {
                // Zero-area surface (seen edge-on, or a zero-width line): a thick line.
                if (!flatAsLine)
                    return;
                QPointF a = scenePoly.first(), b = scenePoly.first();
                qreal best = -1.0;
                for (const QPointF &p : scenePoly)
                    for (const QPointF &q : scenePoly) {
                        const qreal d = std::hypot(p.x() - q.x(), p.y() - q.y());
                        if (d > best) { best = d; a = p; b = q; }
                    }
                pen.setWidth(kPortPenWidth);
                pen.setCapStyle(Qt::FlatCap);
                sheet = m_scene->addLine(QLineF(a, b), pen);
            } else {
                pen.setWidth(2);
                QColor fill = it.style.color;
                fill.setAlpha(qBound(20, int(110 * op + 0.5), 255));
                sheet = m_scene->addPolygon(scenePoly, pen, QBrush(fill));
            }
            sheet->setData(kRoleName, pname);
            sheet->setData(kRoleKind, QStringLiteral("port"));
            sheet->setData(kRoleGds, it.poly.layer);
            sheet->setData(kRoleIsPort, true);
            sheet->setData(kRolePen, it.style.color);
            sheet->setToolTip(sheetTip);
            sheet->setVisible(vis);
            sheet->setZValue(1e9 - 0.5);
            portBounds |= scenePoly.boundingRect();
        };

        QPointF tipScene;
        QPointF labelPos;

        if (!pi.thermalKind.isEmpty()) {
            // Thermal marker as gds2palace builds it on the target layer: a heat source is a box
            // over the bounding box from target zmin to zmax, a constant temperature the polygon
            // at target zmin and zmax. Without a target in the stackup: at the bottom of the layout.
            qreal z0 = pi.hasToRange ? pi.toZminUm : zLo;
            qreal z1 = pi.hasToRange ? pi.toZmaxUm : zLo;
            if (pi.thermalKind == QLatin1String("heatsource")) {
                if (z1 - z0 < kMinThickUm)
                    z1 = z0 + kMinThickUm;
                const QPointF c[4] = {bb.topLeft(), bb.topRight(), bb.bottomRight(), bb.bottomLeft()};
                auto face = [&](qreal z) {
                    return QPolygonF({project3D(c[0].x(), c[0].y(), z), project3D(c[1].x(), c[1].y(), z),
                                      project3D(c[2].x(), c[2].y(), z), project3D(c[3].x(), c[3].y(), z)});
                };
                addPortSheet(face((m_pitchDeg < 0.0) ? z1 : z0), false);
                for (int i = 0; i < 4; ++i) {
                    const QPointF &a = c[i];
                    const QPointF &b = c[(i + 1) % 4];
                    addPortSheet(QPolygonF({project3D(a.x(), a.y(), z0), project3D(b.x(), b.y(), z0),
                                            project3D(b.x(), b.y(), z1), project3D(a.x(), a.y(), z1)}),
                                 false);
                }
                addPortSheet(face((m_pitchDeg < 0.0) ? z0 : z1));
            } else {
                auto flat = [&](qreal z) {
                    QPolygonF poly;
                    for (const QPointF &pt : it.poly.pointsUm)
                        poly << project3D(pt.x(), pt.y(), z);
                    return poly;
                };
                addPortSheet(flat(z0));
                if (z1 > z0)
                    addPortSheet(flat(z1));
            }
            labelPos = project3D(bb.center().x(), bb.center().y(), z1);
        } else if (dir.contains(QLatin1Char('z'))) {
            // Via port: vertical sheet from the top of the lower metal to the bottom of the
            // upper one, on the xmin edge (polygon taller in y) or the ymin edge.
            // The arrow points from From to To; -z reverses it (so swapping From/To does too).
            const bool neg = dir.startsWith(QLatin1Char('-'));
            qreal zA = 0.0;      // lower end of the sheet
            qreal zB = 0.0;      // upper end
            bool toAbove = true; // To layer above From
            if (pi.hasFromRange && pi.hasToRange) {
                toAbove = pi.toZminUm >= pi.fromZminUm;
                zA = toAbove ? pi.fromZmaxUm : pi.toZmaxUm;
                zB = toAbove ? pi.toZminUm : pi.fromZminUm;
            } else {
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
                toAbove = zTo >= zFrom;
                zA = std::min(zFrom, zTo);
                zB = std::max(zFrom, zTo);
            }
            if (zB - zA < 0.05)
                zB = zA + 0.05;

            QPointF s0, s1;  // sheet edge in XY [µm]
            if (bb.height() > bb.width()) {
                s0 = QPointF(bb.left(), bb.top());
                s1 = QPointF(bb.left(), bb.bottom());
            } else {
                s0 = QPointF(bb.left(), bb.top());
                s1 = QPointF(bb.right(), bb.top());
            }
            addPortSheet(QPolygonF({project3D(s0.x(), s0.y(), zA), project3D(s1.x(), s1.y(), zA),
                                    project3D(s1.x(), s1.y(), zB), project3D(s0.x(), s0.y(), zB)}));

            const QPointF c = 0.5 * (s0 + s1);
            const bool up = (toAbove != neg);
            const qreal zTip = up ? zB : zA;
            const qreal zTail = up ? zA : zB;
            tipScene = project3D(c.x(), c.y(), zTip);
            const QPointF tailScene = project3D(c.x(), c.y(), zTail);
            addPortArrowAlong(tailScene, tipScene, col, pname, QStringLiteral("port"),
                              it.poly.layer, vis, 1e9,
                              QStringLiteral("%1 (%2 → %3)").arg(neg ? QStringLiteral("-z")
                                                                     : QStringLiteral("z"),
                                                                 pi.fromLayer, pi.toLayer));
            labelPos = project3D(c.x(), c.y(), 0.5 * (zTail + zTip));
        } else {
            // In-plane port: the bounding rectangle at the bottom of the target metal.
            qreal zMark = 0.5 * (zLo + zHi);
            if (pi.hasToRange)
                zMark = pi.toZminUm;
            else if (pi.hasFromZ && pi.hasToZ)
                zMark = 0.5 * (pi.zFromUm + pi.zToUm);
            else if (pi.hasToZ)
                zMark = pi.zToUm;
            else if (pi.hasFromZ)
                zMark = pi.zFromUm;

            addPortSheet(QPolygonF({project3D(bb.left(), bb.top(), zMark),
                                    project3D(bb.right(), bb.top(), zMark),
                                    project3D(bb.right(), bb.bottom(), zMark),
                                    project3D(bb.left(), bb.bottom(), zMark)}));

            const QPointF c = bb.center();
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
        label->setToolTip(thermalTip);
        label->setPos(labelPos);
        setPixelOffset(label, 8, -16);
        portBounds |= QRectF(labelPos.x() - 2, labelPos.y() - 2, 4, 4);
    }

    // Metals + via envelopes: walls + the cap that faces the camera (top from above,
    // bottom when orbiting below the layout, pitch < 0).
    const bool fromBelow = m_pitchDeg < 0.0;
    for (const Item &it : extrudeItems) {
        if (it.poly.pointsUm.size() < 3)
            continue;
        if (!visibleFor(it.poly.layer))
            continue;

        const qreal op = opacityFor(it.poly.layer);
        const bool isVia = (it.style.kind.compare(QLatin1String("via"), Qt::CaseInsensitive) == 0);
        double z0 = std::min(it.style.zminUm, it.style.zmaxUm);
        double z1 = std::max(it.style.zminUm, it.style.zmaxUm);
        if (z1 - z0 < kMinThickUm)
            z1 = z0 + kMinThickUm;
        const qreal zMid = 0.5 * (z0 + z1);

        // Vias slightly more opaque so pillars stay readable under translucent metals.
        const qreal fillScale = isVia ? 1.15 : 1.0;
        const int fillAlpha = qBound(0, int(kBaseFillAlpha * op * fillScale + 0.5), 230);
        const int wallAlpha = qBound(0, int(kBaseFillAlpha * op * 0.85 * fillScale + 0.5), 220);

        const qreal zCap = fromBelow ? z0 : z1;
        QPolygonF topPoly;
        topPoly.reserve(it.poly.pointsUm.size());
        qreal topDepthFar = -1e300;
        for (const QPointF &p : it.poly.pointsUm) {
            topPoly << project3D(p.x(), p.y(), zCap);
            // Farthest vertex (larger depth) — better than average for large translucent slabs.
            topDepthFar = qMax(topDepthFar, depth3D(p.x(), p.y(), zCap));
        }

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
            side.setAlpha(wallAlpha);
            const qreal d = qMax(qMax(depth3D(a.x(), a.y(), z0), depth3D(b.x(), b.y(), z0)),
                                 qMax(depth3D(b.x(), b.y(), z1), depth3D(a.x(), a.y(), z1)));
            faces.push_back({wall, side, it.style.name, it.style.kind, it.poly.layer, false, d,
                             zMid, 1});
        }

        QColor top = it.style.color;
        top.setAlpha(fillAlpha);
        faces.push_back({topPoly, top, it.style.name, it.style.kind, it.poly.layer, false,
                         topDepthFar, zMid, 2});
    }

    // Stack-aware painter's algorithm: the layers farthest from the camera first — lower
    // metals/vias first from above, higher ones first from below.
    // Pure camera-depth sort makes near vias paint over translucent TopMetal2 (see palace_core_dev).
    std::stable_sort(faces.begin(), faces.end(),
                     [fromBelow](const Face &a, const Face &b) {
                         constexpr qreal kEpsZ = 1e-4;
                         const qreal za = fromBelow ? -a.zMid : a.zMid;
                         const qreal zb = fromBelow ? -b.zMid : b.zMid;
                         if (za + kEpsZ < zb)
                             return true;
                         if (zb + kEpsZ < za)
                             return false;
                         if (a.faceKind != b.faceKind)
                             return a.faceKind < b.faceKind; // walls before the cap in a band
                         return a.depth > b.depth; // farther first within the same face class
                     });
    m_lastIso3dStats.faceCount = faces.size();

    QRectF geomBounds;
    for (const Face &f : faces)
        geomBounds |= f.poly.boundingRect();
    QRectF bounds = portBounds;
    bounds = bounds.isNull() ? geomBounds : bounds.united(geomBounds);

    // Dense Iso3D: one pixmap instead of thousands of QGraphicsItems (orbit stays responsive).
    constexpr int kPixmapFaceThreshold = 250;
    const bool usePixmap = faces.size() >= kPixmapFaceThreshold;
    m_lastIso3dStats.usedPixmap = usePixmap;

    if (usePixmap && !geomBounds.isNull() && geomBounds.width() > 1e-9 && geomBounds.height() > 1e-9) {
        constexpr int kMaxPx = 2048;
        const qreal scale = qBound(2.0,
                                   qreal(kMaxPx) / qMax(geomBounds.width(), geomBounds.height()),
                                   64.0);
        const int pw = qMax(1, int(std::ceil(geomBounds.width() * scale)));
        const int ph = qMax(1, int(std::ceil(geomBounds.height() * scale)));
        QImage img(pw, ph, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(-geomBounds.left() * scale, -geomBounds.top() * scale);
        painter.scale(scale, scale);
        painter.setPen(QPen(QColor(40, 40, 40, 160), 0));
        for (const Face &f : faces) {
            painter.setBrush(f.fill);
            painter.drawPolygon(f.poly);
        }
        painter.end();

        auto *pix = m_scene->addPixmap(QPixmap::fromImage(img));
        pix->setPos(geomBounds.topLeft());
        pix->setScale(1.0 / scale);
        pix->setZValue(0);
        pix->setAcceptedMouseButtons(Qt::NoButton);
    } else {
        const QPen facePen(QColor(30, 30, 30), 0);
        for (int i = 0; i < faces.size(); ++i) {
            const Face &f = faces.at(i);
            auto *item = m_scene->addPolygon(f.poly, facePen, QBrush(f.fill));
            item->setData(kRoleName, f.name);
            item->setData(kRoleKind, f.kind);
            item->setData(kRoleGds, f.gds);
            item->setData(kRoleIsPort, false);
            if (m_styles.contains(f.gds))
                item->setData(kRoleBrush, m_styles.value(f.gds).color);
            else
                item->setData(kRoleBrush, f.fill);
            item->setVisible(true);
            item->setZValue(double(i));
        }
    }

    if (!bounds.isNull()) {
        bounds.adjust(-bounds.width() * 0.08, -bounds.height() * 0.08,
                      bounds.width() * 0.08, bounds.height() * 0.08);
        m_iso3dContentRect = bounds; // what F / auto-fit frames
        // Scene extent must not follow the rotation: a changing sceneRect makes the scroll
        // ranges, the scrollbars and the view center jump on every orbit frame. The projected
        // orbit center is the scene origin, so a square of the bounding-sphere radius holds
        // the layout at any yaw / pitch.
        // Grow-only (reset with new polygons), in case port labels stick out of the sphere.
        qreal r = qMax(m_iso3dSceneHalf, qMax<qreal>(m_orbitRadius, 1e-6) * 1.25);
        r = qMax(r, qMax(qMax(-bounds.left(), bounds.right()), qMax(-bounds.top(), bounds.bottom())));
        m_iso3dSceneHalf = r;
        m_scene->setSceneRect(QRectF(-r, -r, 2 * r, 2 * r));
        if (refit) {
            m_zoomLocked = false;
            fitContent();
        }
    }

    m_lastIso3dStats.sceneItemCount = m_scene->items().size();
    m_lastIso3dStats.ms = timer.elapsed();
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
    emit viewModeChanged(mode == ViewMode::Iso3D);
    if (mode == ViewMode::Iso3D)
        resetOrbitAngles();
    saveViewModeToSettings();
    syncFloatingControls();
    if (!m_polys.isEmpty() || m_field.valid() || !m_field.status.isEmpty() || m_fieldOn)
        rebuildScene(true);
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
 * on. Emits \c fieldModeChanged.
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
    if (!m_fieldOn)
        m_fieldProbeActive = false;
    if (m_fieldOn)
        m_zoomLocked = false;
    if (m_fieldOn && m_viewMode == ViewMode::Iso3D) {
        m_viewMode = ViewMode::Top2D;
        emit viewModeChanged(false);
    }
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
 * \param overlay Heatmap, GDS µm frame, Z range, quantity, status.
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
            && (overlay.volume == m_field.volume);
    const bool sameZ = qFuzzyCompare(overlay.zUm, m_field.zUm);
    const bool statusOnly = sameImage && sameFrame && sameZ && m_field.valid();
    const bool sliderDown = m_fieldZSlider && m_fieldZSlider->isSliderDown();

    const QPointF probeGds = m_fieldProbeActive ? sceneToGdsUm(m_fieldProbeScene) : QPointF();
    const bool keepProbe = m_fieldProbeActive && sameFrame && sameZ && overlay.hasSamples();

    m_field = overlay;

    if (keepProbe) {
        qreal v = 0.0;
        if (sampleFieldAtGdsUm(probeGds.x(), probeGds.y(), &v))
            m_fieldProbeValue = v;
        else
            m_fieldProbeActive = false;
    } else if (!statusOnly) {
        m_fieldProbeActive = false;
    }

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
    viewport()->update();
}

/*!*******************************************************************************************************************
 * \brief Clears the Field overlay (heatmap / status) and rebuilds if Field is on.
 **********************************************************************************************************************/
void LayoutView::clearFieldOverlay()
{
    m_field = FieldOverlay{};
    m_fieldProbeActive = false;
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

bool LayoutView::fieldShowTemp() const
{
    return m_fieldTempChk && m_fieldTempChk->isChecked();
}

/*!*******************************************************************************************************************
 * \brief Fills the result file / cycle picker without emitting \c fieldChoiceChanged.
 *
 * \param labels  One entry per selectable file + cycle.
 * \param current Index to select; the combo is hidden with fewer than two entries.
 **********************************************************************************************************************/
void LayoutView::setFieldChoices(const QStringList &labels, int current)
{
    if (!m_fieldChoiceCombo)
        return;
    {
        const QSignalBlocker block(m_fieldChoiceCombo);
        m_fieldChoiceCombo->clear();
        for (const QString &label : labels) {
            m_fieldChoiceCombo->addItem(label);
            m_fieldChoiceCombo->setItemData(m_fieldChoiceCombo->count() - 1, label, Qt::ToolTipRole);
        }
        if (current >= 0 && current < labels.size())
            m_fieldChoiceCombo->setCurrentIndex(current);
    }
    m_fieldChoiceCombo->setVisible(labels.size() > 1);
    if (m_fieldPanel && m_fieldPanel->isVisible()) {
        m_fieldPanel->adjustSize();
        repositionFloatingControls();
    }
}

int LayoutView::fieldChoiceIndex() const
{
    return m_fieldChoiceCombo ? m_fieldChoiceCombo->currentIndex() : -1;
}

/*!*******************************************************************************************************************
 * \brief Relabels the probe checkbox for thermal vs EM Field context.
 **********************************************************************************************************************/
void LayoutView::setFieldProbeThermal(bool thermal)
{
    if (!m_fieldTempChk)
        return;
    if (thermal) {
        m_fieldTempChk->setText(tr("Temp"));
        m_fieldTempChk->setToolTip(
            tr("Click the Field heatmap to place a temperature probe.\nEsc clears the probe."));
    } else {
        m_fieldTempChk->setText(tr("Probe"));
        m_fieldTempChk->setToolTip(
            tr("Click the Field heatmap to probe the field value (e.g. |E|).\n"
               "Not temperature — use Elmer Thermal for Temp.\nEsc clears the probe."));
    }
    if (m_fieldPanel && m_fieldPanel->isVisible()) {
        m_fieldPanel->adjustSize();
        repositionFloatingControls();
    }
}

/*!*******************************************************************************************************************
 * \brief GDS µm Y-up bounding box of non-port layout polygons (for field crop).
 *
 * Skips port marker layers (\c isPortLayerNumber).
 **********************************************************************************************************************/
QRectF LayoutView::layoutContentBoundsUm() const
{
    QRectF bb;
    bool any = false;
    for (const GdsFlatPolygon &p : m_polys) {
        if (isPortLayerNumber(p.layer))
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
    m_blockFieldControls = false;
}

/*!*******************************************************************************************************************
 * \brief Shows/hides Field panel, updates compact status line, repositions controls.
 **********************************************************************************************************************/
void LayoutView::syncFloatingControls()
{
    if (m_modeSwitch) {
        // Fields page: the pane stays 2D, a separate button opens the 3D field viewer.
        m_modeSwitch->setVisible(!m_fieldOn);
        m_field3dBtn->setVisible(m_fieldOn);
        const QSignalBlocker b2(m_mode2dBtn);
        const QSignalBlocker b3(m_mode3dBtn);
        m_mode2dBtn->setChecked(m_viewMode != ViewMode::Iso3D);
        m_mode3dBtn->setChecked(m_viewMode == ViewMode::Iso3D);
        updateModeToolTips();
    }
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
        QString tip = full;
        if (m_field.hasSamples() && !m_field.volume)
            tip += tr("\nClick layout to probe value (Esc clears).");
        m_fieldStatusLbl->setToolTip(tip);
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
    emit fieldSliceRequest(fieldClipZUm(), fieldLogScale());
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
 * \brief Log changed → re-export the slice with the new color scale.
 **********************************************************************************************************************/
void LayoutView::onFieldControlsChanged()
{
    if (m_blockFieldControls || !m_fieldOn)
        return;
    m_field.logScale = fieldLogScale();
    emitFieldSliceRequest();
}

void LayoutView::onFieldTempToggled(bool on)
{
    if (!on)
        clearFieldProbe();
    else
        viewport()->update();
    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    settings.setValue(QStringLiteral("fieldShowTemp"), on);
}

void LayoutView::repositionFloatingControls()
{
    const int m = 10;
    // Keep clear of the vertical scrollbar so 2D/3D is not clipped.
    int sb = 0;
    if (verticalScrollBar() && verticalScrollBar()->isVisible())
        sb = verticalScrollBar()->width();
    int x = width() - m - sb;
    QWidget *modeControl = (m_field3dBtn && m_field3dBtn->isVisible()) ? static_cast<QWidget *>(m_field3dBtn)
                                                                        : m_modeSwitch;
    if (modeControl) {
        modeControl->adjustSize();
        x -= modeControl->width();
        modeControl->move(x, m);
        modeControl->raise();
        x -= m;
    }
    if (m_fieldBtn && !m_fieldBtn->isHidden()) {
        x -= m_fieldBtn->width();
        m_fieldBtn->move(x, m);
        m_fieldBtn->raise();
    }
    if (m_denseViaLabel && m_denseViaLabel->isVisible()) {
        const int w = qMin(560, qMax(200, viewport()->width() - 40));
        m_denseViaLabel->setFixedWidth(w);
        m_denseViaLabel->adjustSize();
        m_denseViaLabel->move((width() - m_denseViaLabel->width()) / 2,
                              (height() - m_denseViaLabel->height()) / 2);
        m_denseViaLabel->raise();
    }
    if (m_fieldPanel && m_fieldPanel->isVisible()) {
        m_fieldPanel->adjustSize();
        const int px = width() - m_fieldPanel->width() - m - sb;
        m_fieldPanel->move(qMax(m, px), m + 30);
        m_fieldPanel->raise();
    }
}

/*!*******************************************************************************************************************
 * \brief Shows or hides the Field toggle button.
 *
 * MainWindow hides it: the Fields page turns Field mode on and the Substrate page turns it off.
 *
 * \param visible True to show the button.
 **********************************************************************************************************************/
void LayoutView::setFieldToggleVisible(bool visible)
{
    if (!m_fieldBtn)
        return;
    m_fieldBtn->setVisible(visible);
    repositionFloatingControls();
}

/*!*******************************************************************************************************************
 * \brief Current zoom / pan, to be restored when the page that shows this view comes back.
 * \return Transform, centre scene point and whether the user zoomed by hand.
 **********************************************************************************************************************/
LayoutView::ViewState LayoutView::viewState() const
{
    ViewState st;
    st.transform = transform();
    st.center = mapToScene(viewport()->rect().center());
    st.userZoomed = m_zoomLocked;
    return st;
}

/*!*******************************************************************************************************************
 * \brief Restores a zoom / pan from \c viewState.
 *
 * A state the user never zoomed keeps the automatic fit instead.
 *
 * \param state Saved state.
 **********************************************************************************************************************/
void LayoutView::restoreViewState(const ViewState &state)
{
    if (!state.userZoomed) {
        m_zoomLocked = false;
        fitPreferredContent();
        return;
    }
    setTransform(state.transform);
    centerOn(state.center);
    m_zoomLocked = true;
}

void LayoutView::loadViewModeFromSettings()
{
    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    const bool v3d = settings.value(QStringLiteral("view3d"), false).toBool();
    const bool showTemp = settings.value(QStringLiteral("fieldShowTemp"), true).toBool();
    settings.endGroup();
    // Field mode isn't restored: the Fields page turns it on (MainWindow::placeLayoutPane).
    m_fieldOn = false;
    m_viewMode = v3d ? ViewMode::Iso3D : ViewMode::Top2D;
    if (m_fieldTempChk) {
        const QSignalBlocker block(m_fieldTempChk);
        m_fieldTempChk->setChecked(showTemp);
    }
}

void LayoutView::saveViewModeToSettings() const
{
    QSettings settings = emstudioSettings();
    settings.beginGroup(QStringLiteral("LayoutPreview"));
    // view3d is the layout preference; Field mode forces Top2D and must not overwrite it.
    if (!m_fieldOn)
        settings.setValue(QStringLiteral("view3d"), m_viewMode == ViewMode::Iso3D);
    settings.remove(QStringLiteral("viewField"));
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
            if (isPortLayerNumber(p.layer))
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
    // Iso3D: the scene is a rotation-proof square; frame the visible geometry instead.
    if (m_viewMode == ViewMode::Iso3D && !m_fieldOn && m_iso3dContentRect.isValid()) {
        fitInView(m_iso3dContentRect, Qt::KeepAspectRatio);
        return;
    }
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
 * \brief Wheel / trackpad scroll per navigation style; locks auto-fit on resize.
 *
 * setupEM: always zooms under the cursor. EMStudio: scroll orbits (Iso3D) or pans (2D trackpad),
 * a plain mouse wheel in 2D and Ctrl+wheel zoom.
 *
 * \param event Wheel event.
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
            scheduleOrbitRebuild();
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

    // setupEM (VTK trackball): the wheel always zooms, in 2D and 3D.
    if (m_navStyle == NavStyle::SetupEM) {
        qreal factor = 1.0;
        if (angle.y() != 0)
            factor = std::pow(1.15, angle.y() / 120.0);
        else if (pixel.y() != 0)
            factor = std::pow(1.0035, static_cast<double>(pixel.y()));
        if (qFuzzyCompare(factor, 1.0)) {
            QGraphicsView::wheelEvent(event);
            return;
        }
        zoomAt(factor, event->position().toPoint());
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
 * \brief Handles the Layout keys (see \c NavigationStyle::bindingTable, "Layout*" rows).
 *
 * Esc clears highlight, ruler, measure mode and probe; everything else goes through
 * \c handleViewKey, unhandled keys to QGraphicsView.
 *
 * \param event Key event.
 **********************************************************************************************************************/
void LayoutView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        clearHighlight();
        clearMeasure();
        clearFieldProbe();
        m_measureArmed = false;
        viewport()->setCursor(Qt::ArrowCursor);
        emit highlightCleared();
        event->accept();
        return;
    }
    if (handleViewKey(event)) {
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

/*!*******************************************************************************************************************
 * \brief View, mode, measure, Field-panel and copy keys of the Layout preview.
 *
 * \param event Key event (keypad modifier ignored).
 * \return True if the key was handled.
 **********************************************************************************************************************/
bool LayoutView::handleViewKey(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const bool plain = (mods == Qt::NoModifier);
    const bool shift = (mods == Qt::ShiftModifier);
    const bool iso = (m_viewMode == ViewMode::Iso3D);
    const QPoint center = viewport()->rect().center();
    const int panStepX = qMax(1, viewport()->width() / 10);
    const int panStepY = qMax(1, viewport()->height() / 10);

    switch (event->key()) {
    case Qt::Key_C:
        if (mods != Qt::ControlModifier)
            return false;
        if (QClipboard *cb = QGuiApplication::clipboard())
            cb->setPixmap(viewport()->grab());
        return true;
    case Qt::Key_F:
        if (shift) {
            if (isSignalConnected(QMetaMethod::fromSignal(&LayoutView::fieldPageRequested)))
                emit fieldPageRequested(!m_fieldOn);
            else
                setFieldMode(!m_fieldOn);
            return true;
        }
        if (!plain)
            return false;
        m_zoomLocked = false;
        resetTransform();
        fitPreferredContent();
        return true;
    case Qt::Key_Home:
        if (!plain)
            return false;
        m_zoomLocked = false;
        resetTransform();
        fitFullContent();
        return true;
    case Qt::Key_2:
        if (!plain)
            return false;
        setViewMode(ViewMode::Top2D);
        return true;
    case Qt::Key_3:
        if (!plain)
            return false;
        if (m_fieldOn)
            emit fieldExternalVolumeRequested();
        else
            setViewMode(ViewMode::Iso3D);
        return true;
    case Qt::Key_R:
        if (!plain || !iso)
            return false;
        resetOrbitAngles();
        m_zoomLocked = false;
        if (isFieldVolume())
            emitFieldSliceRequest();
        else if (!m_polys.isEmpty())
            rebuildScene(true);
        return true;
    case Qt::Key_I:
        if (!plain || m_fieldOn)
            return false;
        if (!iso) {
            setViewMode(ViewMode::Iso3D); // resets to the isometric angles
        } else {
            resetOrbitAngles();
            if (!m_polys.isEmpty())
                rebuildScene(false);
        }
        return true;
    case Qt::Key_X:
        if ((!plain && !shift) || m_fieldOn)
            return false;
        setSideView(shift ? 90.0 : -90.0);
        return true;
    case Qt::Key_Y:
        if ((!plain && !shift) || m_fieldOn)
            return false;
        setSideView(shift ? 0.0 : 180.0);
        return true;
    case Qt::Key_Z:
        if (shift) {
            if (m_fieldOn)
                return false;
            setSideView(0.0, -85.0); // from below (pitch limit)
            return true;
        }
        if (!plain || !iso)
            return false;
        setViewMode(ViewMode::Top2D);
        return true;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (!plain && !shift)
            return false;
        zoomAt(1.15, center);
        return true;
    case Qt::Key_Minus:
        if (!plain)
            return false;
        zoomAt(1.0 / 1.15, center);
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        if (!plain)
            return false;
        // The view moves in the arrow direction, the content the other way.
        if (event->key() == Qt::Key_Left)
            panBy(panStepX, 0);
        else if (event->key() == Qt::Key_Right)
            panBy(-panStepX, 0);
        else if (event->key() == Qt::Key_Up)
            panBy(0, panStepY);
        else
            panBy(0, -panStepY);
        return true;
    case Qt::Key_M:
        if (!plain)
            return false;
        m_measureArmed = !m_measureArmed;
        viewport()->setCursor(m_measureArmed ? Qt::CrossCursor : Qt::ArrowCursor);
        return true;
    case Qt::Key_L:
        if (!plain || !m_fieldOn || !m_fieldLogChk)
            return false;
        m_fieldLogChk->toggle();
        return true;
    case Qt::Key_P:
        if (!plain || !m_fieldOn || !m_fieldTempChk)
            return false;
        m_fieldTempChk->toggle();
        return true;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if (!plain || !m_fieldOn || !m_fieldZSlider)
            return false;
        m_fieldZSlider->setValue(m_fieldZSlider->value()
                                 + (event->key() == Qt::Key_PageUp ? 50 : -50));
        m_fieldZKeyTimer->start();
        return true;
    default:
        return false;
    }
}

/*!*******************************************************************************************************************
 * \brief Zooms by a factor, keeping the scene point under a viewport position in place.
 *
 * \param factor  Scale factor (> 1 zooms in).
 * \param viewPos Viewport position that stays fixed.
 **********************************************************************************************************************/
void LayoutView::zoomAt(qreal factor, const QPoint &viewPos)
{
    if (factor <= 0.0)
        return;
    m_zoomLocked = true;
    const QPointF scenePt = mapToScene(viewPos);
    const QGraphicsView::ViewportAnchor oldAnchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    scale(factor, factor);
    setTransformationAnchor(oldAnchor);
    const QPoint drift = mapFromScene(scenePt) - viewPos;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + drift.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + drift.y());
}

/*!*******************************************************************************************************************
 * \brief Pans the view; the content moves by the given viewport pixel delta.
 *
 * \param dx Horizontal content shift [px].
 * \param dy Vertical content shift [px].
 **********************************************************************************************************************/
void LayoutView::panBy(int dx, int dy)
{
    m_zoomLocked = true;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
}

/*!*******************************************************************************************************************
 * \brief Iso3D view from a fixed direction; switches from 2D when needed.
 *
 * Yaw −90° looks from +X, 90° from −X, 180° from +Y, 0° from −Y; pitch 0 is a side view,
 * negative pitch looks from below.
 *
 * \param yawDeg   Orbit yaw [deg].
 * \param pitchDeg Orbit pitch [deg].
 **********************************************************************************************************************/
void LayoutView::setSideView(qreal yawDeg, qreal pitchDeg)
{
    if (m_fieldOn)
        return;
    const bool switching = (m_viewMode != ViewMode::Iso3D);
    m_viewMode = ViewMode::Iso3D;
    if (switching)
        emit viewModeChanged(true);
    m_yawDeg = yawDeg;
    m_pitchDeg = qBound(-85.0, pitchDeg, 85.0);
    m_volumeCamZoom = 1.0;
    m_zoomLocked = false;
    if (switching)
        saveViewModeToSettings();
    syncFloatingControls();
    if (!m_polys.isEmpty())
        rebuildScene(true);
}

/*!*******************************************************************************************************************
 * \brief Adds a measure point: start, then end; a further point starts a new ruler.
 *
 * \param scenePt Scene position of the click.
 **********************************************************************************************************************/
void LayoutView::addMeasurePoint(const QPointF &scenePt)
{
    if (!m_measureHasStart || m_measureHasEnd) {
        m_measureStart = scenePt;
        m_measureHasStart = true;
        m_measureHasEnd = false;
        m_measureEnd = scenePt;
    } else {
        m_measureEnd = scenePt;
        m_measureHasEnd = true;
    }
    emitMeasure();
    viewport()->update();
}

/*!*******************************************************************************************************************
 * \brief Tooltip of the 2D or 3D side of the switch: what it shows plus the mouse bindings of the style.
 *
 * \param iso3d True for the 3D side.
 * \return Tooltip text.
 **********************************************************************************************************************/
QString LayoutView::modeButtonToolTip(bool iso3d) const
{
    // Viewer names are the binding table's (NavigationStyle context).
    const QString viewer = QCoreApplication::translate("NavigationStyle", iso3d ? "Layout 3D" : "Layout 2D");
    const QString head = iso3d ? tr("3D view of the layout (key 3).") : tr("Top view of the layout (key 2).");
    return tr("%1\nNavigation: %2\n%3\nAll keys: Setup → Key Bindings")
            .arg(head, NavigationStyle::displayName(m_navStyle),
                 NavigationStyle::tooltipFor(m_navStyle, viewer));
}

/*!*******************************************************************************************************************
 * \brief Tooltips of the 2D / 3D switch and the "3D viewer" button (they follow the navigation style).
 **********************************************************************************************************************/
void LayoutView::updateModeToolTips()
{
    if (!m_modeSwitch)
        return;
    m_mode2dBtn->setToolTip(modeButtonToolTip(false));
    m_mode3dBtn->setToolTip(modeButtonToolTip(true));
    m_field3dBtn->setToolTip(
        tr("Open the interactive 3D field viewer in a separate window (key 3).\n"
           "The field view here stays 2D.\nNavigation in the 3D viewer: %1\nAll keys: Setup → Key Bindings")
            .arg(NavigationStyle::displayName(m_navStyle)));
}

/*!*******************************************************************************************************************
 * \brief Selects the mouse navigation preset and refreshes the tooltips.
 *
 * \param style EMStudio or setupEM.
 **********************************************************************************************************************/
void LayoutView::setNavigationStyle(NavStyle style)
{
    m_navStyle = style;
    updateModeToolTips();
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
 * \brief Starts a drag (orbit / pan / zoom per \c NavStyle), a measure point, or a pending click.
 *
 * A plain left press becomes a drag after 6 px, otherwise a click on release, which selects
 * the named layers under the cursor in top-to-bottom order (repeated clicks cycle down).
 *
 * \param event Mouse event.
 **********************************************************************************************************************/
void LayoutView::mousePressEvent(QMouseEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool left = (event->button() == Qt::LeftButton);
    const bool iso = (m_viewMode == ViewMode::Iso3D);
    const bool vtk = (m_navStyle == NavStyle::SetupEM);

    // Measure: Ctrl+Shift+click any time, or a plain click while M is armed.
    if (left && (mods == (Qt::ControlModifier | Qt::ShiftModifier)
                 || (m_measureArmed && mods == Qt::NoModifier))) {
        m_leftPressPending = false;
        setFocus(Qt::MouseFocusReason);
        addMeasurePoint(mapToScene(event->pos()));
        event->accept();
        return;
    }

    auto beginDrag = [&](bool *flag, Qt::CursorShape cursor) {
        *flag = true;
        m_leftPressPending = false;
        m_pressPos = event->pos();
        m_panLast = event->pos();
        viewport()->setCursor(cursor);
        event->accept();
    };

    // Right drag: zoom (setupEM, 2D and 3D) or orbit (EMStudio, 3D).
    if (event->button() == Qt::RightButton && (vtk || iso)) {
        if (vtk)
            beginDrag(&m_dollying, Qt::SizeVerCursor);
        else
            beginDrag(&m_orbiting, Qt::ClosedHandCursor);
        return;
    }

    // Ctrl+left: orbit in 3D (both styles; the layout has no roll).
    if (iso && left && mods == Qt::ControlModifier) {
        beginDrag(&m_orbiting, Qt::ClosedHandCursor);
        return;
    }

    // Middle, Shift+left or Alt+left: pan.
    if (event->button() == Qt::MiddleButton
        || (left && (mods == Qt::ShiftModifier || mods == Qt::AltModifier))) {
        beginDrag(&m_panning, Qt::ClosedHandCursor);
        return;
    }

    // Plain left: defer select vs drag (orbit in 3D / pan in 2D).
    if (left && mods == Qt::NoModifier) {
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

    if (m_dollying) {
        // Drag up zooms in (VTK dolly), about the press point.
        const int dy = event->pos().y() - m_panLast.y();
        m_panLast = event->pos();
        if (dy != 0)
            zoomAt(std::pow(1.01, -dy), m_pressPos);
        event->accept();
        return;
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
            scheduleOrbitRebuild();
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
        const QPointF scenePt = mapToScene(m_pressPos);
        // Field probe when Temp is enabled; keep shape highlight too.
        if (m_fieldOn && fieldShowTemp() && m_field.hasSamples() && !m_field.volume)
            tryPlaceFieldProbe(scenePt);

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
            // If probe failed earlier (e.g. NaN), retry at the same click after highlight.
            if (m_fieldOn && fieldShowTemp() && m_field.hasSamples() && !m_field.volume
                && !m_fieldProbeActive)
                tryPlaceFieldProbe(scenePt);
        }
        event->accept();
        return;
    }

    const Qt::CursorShape idleCursor = m_measureArmed ? Qt::CrossCursor : Qt::ArrowCursor;
    if (m_dollying && event->button() == Qt::RightButton) {
        m_dollying = false;
        viewport()->setCursor(idleCursor);
        event->accept();
        return;
    }
    if (m_orbiting
        && (event->button() == Qt::RightButton || event->button() == Qt::LeftButton)) {
        m_orbiting = false;
        viewport()->setCursor(idleCursor);
        if (isFieldVolume())
            emitFieldSliceRequest();
        else
            flushOrbitRebuild();
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        viewport()->setCursor(idleCursor);
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

    if (m_fieldProbeActive && fieldShowTemp()) {
        QPen pen(QColor(20, 20, 20));
        pen.setWidth(0);
        painter->setPen(pen);
        painter->setBrush(QColor(255, 220, 40));
        const QPointF a = mapToScene(QPoint(0, 0));
        const QPointF b = mapToScene(QPoint(6, 0));
        const qreal r = qMax<qreal>(0.15, QLineF(a, b).length());
        painter->drawEllipse(m_fieldProbeScene, r, r);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(m_fieldProbeScene, r * 1.6, r * 1.6);
    }

    if (!m_cursorValid && !m_measureHasStart && !(m_fieldProbeActive && fieldShowTemp()))
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
    if (m_fieldProbeActive && fieldShowTemp()) {
        const QPointF g = sceneToGdsUm(m_fieldProbeScene);
        const QString q = m_field.quantity.isEmpty()
                ? QStringLiteral("value")
                : m_field.quantity;
        const bool thermal = q.contains(QStringLiteral("temp"), Qt::CaseInsensitive)
                || q.compare(QStringLiteral("t"), Qt::CaseInsensitive) == 0;
        if (thermal) {
            const qreal celsius = m_fieldProbeValue - 273.15;
            lines << QStringLiteral("T=%1 °C  (%2 K)   @ Z=%3 µm")
                         .arg(celsius, 0, 'f', 2)
                         .arg(m_fieldProbeValue, 0, 'f', 2)
                         .arg(m_field.zUm, 0, 'f', 2);
        } else {
            lines << QStringLiteral("%1=%2   @ Z=%3 µm")
                         .arg(q)
                         .arg(m_fieldProbeValue, 0, 'g', 5)
                         .arg(m_field.zUm, 0, 'f', 2);
        }
        lines << QStringLiteral("X=%1  Y=%2 µm")
                     .arg(g.x(), 0, 'f', 3)
                     .arg(g.y(), 0, 'f', 3);
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
        QPoint anchor = m_cursorView;
        if (m_fieldProbeActive && fieldShowTemp() && !m_cursorValid)
            anchor = mapFromScene(m_fieldProbeScene);
        int x = anchor.x() + 14;
        int y = anchor.y() + 16;
        if (x + boxW > width() - 4)
            x = anchor.x() - boxW - 10;
        if (y + boxH > height() - 4)
            y = anchor.y() - boxH - 10;
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

void LayoutView::clearFieldProbe()
{
    if (!m_fieldProbeActive)
        return;
    m_fieldProbeActive = false;
    viewport()->update();
}

bool LayoutView::sampleFieldAtGdsUm(qreal xUm, qreal yUm, qreal *valueOut) const
{
    if (!valueOut || !m_field.hasSamples() || !m_field.valid())
        return false;
    const qreal w = m_field.xmaxUm - m_field.xminUm;
    const qreal h = m_field.ymaxUm - m_field.yminUm;
    if (w <= 0.0 || h <= 0.0)
        return false;
    // PNG / sampleGrid: row 0 = ymax, col 0 = xmin.
    const qreal fx = (xUm - m_field.xminUm) / w * (m_field.sampleNx - 1);
    const qreal fy = (m_field.ymaxUm - yUm) / h * (m_field.sampleNy - 1);
    if (fx < 0.0 || fy < 0.0 || fx > m_field.sampleNx - 1 || fy > m_field.sampleNy - 1)
        return false;

    const int x0 = int(std::floor(fx));
    const int y0 = int(std::floor(fy));
    const int x1 = qMin(x0 + 1, m_field.sampleNx - 1);
    const int y1 = qMin(y0 + 1, m_field.sampleNy - 1);
    const qreal tx = fx - x0;
    const qreal ty = fy - y0;

    auto at = [this](int ix, int iy) -> float {
        return m_field.sampleGrid.at(iy * m_field.sampleNx + ix);
    };
    const float v00 = at(x0, y0);
    const float v10 = at(x1, y0);
    const float v01 = at(x0, y1);
    const float v11 = at(x1, y1);
    // Prefer bilinear when all corners are finite; otherwise nearest finite neighbor.
    if (std::isfinite(v00) && std::isfinite(v10) && std::isfinite(v01) && std::isfinite(v11)) {
        const qreal v0 = v00 * (1.0 - tx) + v10 * tx;
        const qreal v1 = v01 * (1.0 - tx) + v11 * tx;
        *valueOut = v0 * (1.0 - ty) + v1 * ty;
        return std::isfinite(*valueOut);
    }
    const int ix = qBound(0, int(std::round(fx)), m_field.sampleNx - 1);
    const int iy = qBound(0, int(std::round(fy)), m_field.sampleNy - 1);
    // Search a small window for a finite sample (outside-mesh NaNs).
    for (int rad = 0; rad <= 3; ++rad) {
        for (int dy = -rad; dy <= rad; ++dy) {
            for (int dx = -rad; dx <= rad; ++dx) {
                const int jx = ix + dx;
                const int jy = iy + dy;
                if (jx < 0 || jy < 0 || jx >= m_field.sampleNx || jy >= m_field.sampleNy)
                    continue;
                const float v = at(jx, jy);
                if (std::isfinite(v)) {
                    *valueOut = v;
                    return true;
                }
            }
        }
    }
    return false;
}

bool LayoutView::tryPlaceFieldProbe(const QPointF &scenePt)
{
    if (!m_fieldOn || !m_field.hasSamples() || m_field.volume)
        return false;
    const QPointF g = sceneToGdsUm(scenePt);
    qreal v = 0.0;
    if (!sampleFieldAtGdsUm(g.x(), g.y(), &v))
        return false;
    m_fieldProbeActive = true;
    m_fieldProbeScene = scenePt;
    m_fieldProbeValue = v;
    viewport()->update();
    return true;
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
    return m_layerOpacity.value(gdsLayer, defaultFillOpacity()) / defaultFillOpacity();
}

/*!*******************************************************************************************************************
 * \brief Tooltip of an Elmer Thermal marker (heat source / constant temperature).
 *
 * \param gdsLayer GDS marker layer.
 * \param name     Marker name shown in the view (e.g. "Heat 0.4 W").
 * \return Tooltip text, or an empty string for EM ports and other layers.
 **********************************************************************************************************************/
QString LayoutView::thermalMarkerToolTip(int gdsLayer, const QString &name) const
{
    const PortInfo pi = m_ports.value(gdsLayer);
    if (pi.thermalKind.isEmpty())
        return QString();
    const QString target = pi.toLayer.isEmpty() ? tr("no target layer") : pi.toLayer;
    const QString where = pi.hasToRange ? target : tr("%1, not in the stackup").arg(target);
    if (pi.thermalKind == QLatin1String("heatsource"))
        return tr("%1: heat source on GDS %2, volume in %3").arg(name).arg(gdsLayer).arg(where);
    return tr("%1: constant temperature on GDS %2, faces of %3").arg(name).arg(gdsLayer).arg(where);
}

/*!*******************************************************************************************************************
 * \brief Whether a GDS layer is a port marker layer rather than a stackup layer.
 *
 * A layer with a style is a port only when its kind is "port"; a stackup layer in 201–299
 * (e.g. a ground sheet on 250) stays a layer. Layers without a style count as ports in 201–299.
 *
 * \param gdsLayer GDS layer number.
 * \return True for port marker layers.
 **********************************************************************************************************************/
bool LayoutView::isPortLayerNumber(int gdsLayer) const
{
    const auto it = m_styles.constFind(gdsLayer);
    if (it != m_styles.cend())
        return it.value().kind.compare(QLatin1String("port"), Qt::CaseInsensitive) == 0;
    return gdsLayer >= 201 && gdsLayer <= 299;
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
    return m_layerOpacity.value(gdsLayer, defaultFillOpacity());
}

void LayoutView::setLayerVisible(int gdsLayer, bool visible)
{
    m_layerVisible.insert(gdsLayer, visible);
    if (!rebuildIso3dForStyleChange())
        applyLayerVisual(gdsLayer);
}

void LayoutView::setLayerOpacity(int gdsLayer, qreal opacity)
{
    m_layerOpacity.insert(gdsLayer, qBound(0.0, opacity, 1.0));
    if (!rebuildIso3dForStyleChange())
        applyLayerVisual(gdsLayer);
}

/*!*******************************************************************************************************************
 * \brief Iso3D: redraws the scene after a visibility / opacity change; no-op in 2D.
 *
 * Iso3D faces cannot be restyled in place: dense scenes are one pre-rendered pixmap, and
 * the per-face shading (darker walls, via boost) is only computed in \c rebuildScene3D.
 * Zoom, orbit and the measure ruler are kept.
 *
 * \return True if the scene was rebuilt (the caller skips the 2D item restyle).
 **********************************************************************************************************************/
bool LayoutView::rebuildIso3dForStyleChange()
{
    if (m_viewMode != ViewMode::Iso3D || m_polys.isEmpty())
        return false;
    // Same projection, so the ruler's scene points stay valid.
    const bool hasStart = m_measureHasStart;
    const bool hasEnd = m_measureHasEnd;
    const QPointF start = m_measureStart;
    const QPointF end = m_measureEnd;
    rebuildScene(false);
    if (hasStart) {
        m_measureHasStart = hasStart;
        m_measureHasEnd = hasEnd;
        m_measureStart = start;
        m_measureEnd = end;
        emitMeasure();
    }
    viewport()->update();
    return true;
}

/*!*******************************************************************************************************************
 * \brief Sets every layer's fill opacity: restyles 2D items in one pass, or redraws Iso3D.
 *
 * Layers drawn later (not yet in the scene) also get \a opacity. Items without a GDS layer
 * (Field image, probe) are left alone.
 *
 * \param opacity Fill opacity 0..1.
 **********************************************************************************************************************/
void LayoutView::setAllLayerOpacity(qreal opacity)
{
    const qreal op = qBound(0.0, opacity, 1.0);
    for (auto it = m_styles.constBegin(); it != m_styles.constEnd(); ++it)
        m_layerOpacity.insert(it.key(), op);
    for (const GdsFlatPolygon &p : m_polys)
        m_layerOpacity.insert(p.layer, op);
    for (auto it = m_layerOpacity.begin(); it != m_layerOpacity.end(); ++it)
        it.value() = op;
    if (!m_scene || rebuildIso3dForStyleChange())
        return;
    for (QGraphicsItem *item : m_scene->items()) {
        if (item->data(kRoleGds).isValid())
            applyItemVisual(item);
    }
    viewport()->update();
}

void LayoutView::applyLayerVisual(int gdsLayer)
{
    if (!m_scene)
        return;
    for (QGraphicsItem *item : m_scene->items()) {
        const QVariant gds = item->data(kRoleGds);
        if (gds.isValid() && gds.toInt() == gdsLayer)
            applyItemVisual(item);
    }
    viewport()->update();
}

/*!*******************************************************************************************************************
 * \brief Applies its layer's visibility and opacity (and highlight state) to one layout item.
 **********************************************************************************************************************/
void LayoutView::applyItemVisual(QGraphicsItem *item)
{
    const int gdsLayer = item->data(kRoleGds).toInt();
    const bool vis = visibleFor(gdsLayer);
    const qreal op = opacityFor(gdsLayer);
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
        return;
    }

    if (auto *text = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(item)) {
        QColor c = isHi ? QColor(255, 180, 0)
                        : (item->data(kRolePen).isValid()
                           ? item->data(kRolePen).value<QColor>()
                           : QColor(220, 40, 180));
        c.setAlpha(qBound(40, int(255 * op + 0.5), 255));
        text->setBrush(c);
        return;
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
