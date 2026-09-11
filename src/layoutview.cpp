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
#include <QFont>
#include <QFontMetrics>
#include <QTransform>
#include <QVariant>
#include <QtGlobal>
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
}

/*!*******************************************************************************************************************
 * \brief Clears all polygons, highlight state, and the scene rectangle.
 **********************************************************************************************************************/
void LayoutView::clear()
{
    m_highlightedName.clear();
    m_zoomLocked = false;
    m_cursorValid = false;
    clearMeasure();
    m_scene->clear();
    m_scene->setSceneRect(QRectF());
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
                             const QHash<int, QString> &portDirections)
{
    clear();

    struct Item {
        GdsFlatPolygon poly;
        LayerStyle style;
    };
    QVector<Item> items;
    items.reserve(polys.size());

    for (const GdsFlatPolygon &p : polys) {
        LayerStyle st;
        if (styles.contains(p.layer)) {
            st = styles.value(p.layer);
        } else {
            // Unmapped GDS layer (e.g. port markers 201/202).
            const int portIdx = p.layer - 200;
            st.name = (portIdx >= 1 && portIdx <= 99)
                    ? QStringLiteral("P%1").arg(portIdx)
                    : QStringLiteral("L%1").arg(p.layer);
            st.kind = QStringLiteral("port");
            st.color = QColor(220, 40, 180);
            st.order = 10000 + p.layer;
        }
        items.push_back({p, st});
    }

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

            // Direction from Ports table: in-plane arrows for x/y; ⊙ (+z) / ⊗ (-z).
            QString dir = portDirections.value(it.poly.layer).trimmed().toLower();
            if (dir.isEmpty())
                dir = QStringLiteral("z");
            if (dir.contains(QLatin1Char('z'))) {
                const QString zl = dir.startsWith(QLatin1Char('-'))
                        ? QStringLiteral("-z") : QStringLiteral("z");
                addPortOutOfPlaneMarker(mid, it.style.color, it.style.name,
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

            QString dir = portDirections.value(it.poly.layer).trimmed().toLower();
            if (dir.isEmpty())
                dir = QStringLiteral("x");
            if (dir.contains(QLatin1Char('z'))) {
                const QString zl = dir.startsWith(QLatin1Char('-'))
                        ? QStringLiteral("-z") : QStringLiteral("z");
                addPortOutOfPlaneMarker(bb.center(), it.style.color, it.style.name,
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
                addPortArrow(bb.center(), dirScene, it.style.color, it.style.name,
                             QStringLiteral("port"), it.poly.layer, vis,
                             double(it.style.order) + 0.4, dirLabel);
            }
        }
    }

    if (!bounds.isNull()) {
        bounds.adjust(-bounds.width() * 0.05, -bounds.height() * 0.05,
                      bounds.width() * 0.05, bounds.height() * 0.05);
        m_scene->setSceneRect(bounds);
        fitContent();
    }

    applyHighlight();
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
 * \brief Fits the scene rectangle into the viewport while keeping aspect ratio.
 **********************************************************************************************************************/
void LayoutView::fitContent()
{
    if (m_scene->sceneRect().isEmpty())
        return;
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
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
    const double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
    event->accept();
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
    if (event->key() == Qt::Key_F || event->key() == Qt::Key_Home) {
        m_zoomLocked = false;
        resetTransform();
        fitContent();
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
    // Middle button or Alt+Left: pan. Default cursor stays an arrow for precise coords.
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_panning = true;
        m_panLast = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) {
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

    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);

        // items(pos) is topmost-first stacking order.
        struct Hit { QString name; QString kind; };
        QVector<Hit> stack;
        QSet<QString> seen;
        for (QGraphicsItem *it : items(event->pos())) {
            const QString name = it->data(kRoleName).toString();
            if (name.isEmpty() || seen.contains(name))
                continue;
            seen.insert(name);
            stack.append({name, it->data(kRoleKind).toString()});
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
            emit layerClicked(pick.name, pick.kind);
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void LayoutView::mouseMoveEvent(QMouseEvent *event)
{
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

    // Pixel-space arrow (ItemIgnoresTransformations): tip at origin.
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

void LayoutView::addPortOutOfPlaneMarker(const QPointF &origin,
                                         const QColor &color,
                                         const QString &name,
                                         const QString &kind,
                                         int gdsLayer,
                                         bool visible,
                                         qreal z,
                                         const QString &dirLabel)
{
    // Top-view convention: ⊙ = +z toward viewer, ⊗ = -z into the page.
    // Ring/dot/cross are black and thick for readability on light fills.
    Q_UNUSED(color);
    const bool intoPage = dirLabel.startsWith(QLatin1Char('-'));
    const qreal r = 11.0;
    const QColor ink(0, 0, 0);

    QPen ring(ink);
    ring.setWidthF(2.8);
    ring.setCosmetic(true);
    ring.setCapStyle(Qt::RoundCap);
    ring.setJoinStyle(Qt::RoundJoin);

    auto tag = [&](QGraphicsItem *item) {
        item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        item->setPos(origin);
        item->setData(kRoleName, name);
        item->setData(kRoleKind, kind);
        item->setData(kRoleGds, gdsLayer);
        item->setData(kRoleIsPort, true);
        item->setData(kRolePen, ink);
        item->setVisible(visible);
        item->setZValue(z);
        item->setToolTip(QStringLiteral("%1 direction %2 (%3)")
                             .arg(name, dirLabel,
                                  intoPage ? QStringLiteral("into page / away")
                                           : QStringLiteral("toward viewer")));
    };

    auto *circle = m_scene->addEllipse(-r, -r, 2 * r, 2 * r, ring, Qt::NoBrush);
    tag(circle);

    if (intoPage) {
        QPen cross(ink);
        cross.setWidthF(3.0);
        cross.setCosmetic(true);
        cross.setCapStyle(Qt::RoundCap);
        const qreal s = r * 0.58;
        auto *l1 = m_scene->addLine(-s, -s, s, s, cross);
        auto *l2 = m_scene->addLine(-s, s, s, -s, cross);
        tag(l1);
        tag(l2);
    } else {
        const qreal dr = 3.6;
        auto *dot = m_scene->addEllipse(-dr, -dr, 2 * dr, 2 * dr, QPen(Qt::NoPen), QBrush(ink));
        tag(dot);
    }

    auto *dl = m_scene->addSimpleText(dirLabel);
    QFont f = dl->font();
    f.setPointSize(8);
    f.setBold(true);
    dl->setFont(f);
    dl->setBrush(ink);
    dl->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    dl->setPos(origin);
    setPixelOffset(dl, r + 4, -5);
    dl->setData(kRoleName, name);
    dl->setData(kRoleKind, kind);
    dl->setData(kRoleGds, gdsLayer);
    dl->setData(kRoleIsPort, true);
    dl->setData(kRolePen, ink);
    dl->setVisible(visible);
    dl->setZValue(z + 0.05);
}

#endif // QT_VERSION
