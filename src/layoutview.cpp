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
#include <QPainter>
#include <QAbstractGraphicsShapeItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>

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
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(Qt::white);
    setFocusPolicy(Qt::StrongFocus);
}

/*!*******************************************************************************************************************
 * \brief Clears all polygons, highlight state, and the scene rectangle.
 **********************************************************************************************************************/
void LayoutView::clear()
{
    m_highlightedName.clear();
    m_zoomLocked = false;
    m_scene->clear();
    m_scene->setSceneRect(QRectF());
}

/*!*******************************************************************************************************************
 * \brief Replaces the scene contents with flattened GDS polygons.
 *
 * Polygons are sorted by \c LayerStyle::order (lower first). Layers missing from
 * \a styles are drawn as port-style placeholders named \c L\<n\>. GDS Y is flipped
 * for Qt display. After drawing, the view fits the content unless the user has zoomed.
 *
 * \param polys   Flattened polygons in micrometres (from GdsLayout::flattenTopCell).
 * \param styles  Map from GDS layer number to display style / stack name.
 **********************************************************************************************************************/
void LayoutView::setPolygons(const QVector<GdsFlatPolygon> &polys,
                             const QHash<int, LayerStyle> &styles)
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
            // Unmapped GDS layer (e.g. port markers): still show.
            st.name = QStringLiteral("L%1").arg(p.layer);
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
        if (it.poly.pointsUm.size() < 3)
            continue;

        // Flip Y so layout matches the usual GDS/KLayout top-view (Y up).
        QPolygonF drawn;
        drawn.reserve(it.poly.pointsUm.size());
        for (const QPointF &p : it.poly.pointsUm)
            drawn << QPointF(p.x(), -p.y());

        QColor fill = it.style.color;
        fill.setAlpha(150);
        auto *item = m_scene->addPolygon(drawn,
                                         QPen(QColor(20, 20, 20), 0),
                                         QBrush(fill));
        item->setData(kRoleName, it.style.name);
        item->setData(kRoleKind, it.style.kind);
        item->setToolTip(QStringLiteral("%1  (GDS %2/%3)")
                             .arg(it.style.name)
                             .arg(it.poly.layer)
                             .arg(it.poly.datatype));
        item->setZValue(double(it.style.order));
        bounds |= drawn.boundingRect();
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

    const QPen normal(QColor(20, 20, 20), 0);
    // Bright yellow-orange, thick cosmetic stroke — readable on any metal color.
    QPen hiPen(QColor(255, 220, 0));
    hiPen.setCosmetic(true);
    hiPen.setWidth(4);

    for (QGraphicsItem *item : m_scene->items()) {
        if (item->data(kRoleName).toString() != name)
            continue;
        if (auto *shape = qgraphicsitem_cast<QAbstractGraphicsShapeItem *>(item)) {
            if (on) {
                if (!item->data(kRoleBrush).isValid())
                    item->setData(kRoleBrush, shape->brush().color());
                const QColor base = item->data(kRoleBrush).value<QColor>();
                // Mix layer color with hot yellow so fill pops without hiding the layer hue.
                QColor fill(qMin(255, int(0.35 * base.red()   + 0.65 * 255)),
                            qMin(255, int(0.35 * base.green() + 0.65 * 210)),
                            qMin(255, int(0.35 * base.blue()  + 0.65 * 0)));
                fill.setAlpha(230);
                shape->setPen(hiPen);
                shape->setBrush(fill);
                shape->setZValue(shape->zValue() + 0.5);
            } else {
                shape->setPen(normal);
                if (item->data(kRoleBrush).isValid()) {
                    QColor orig = item->data(kRoleBrush).value<QColor>();
                    orig.setAlpha(150);
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

#endif // QT_VERSION
