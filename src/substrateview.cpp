/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
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

#include <cmath>
#include <algorithm>

#include <QDebug>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QGraphicsRectItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QAbstractGraphicsShapeItem>
#include <QHash>
#include <QMouseEvent>

#include "substrateview.h"

/*!*******************************************************************************************************************
 * \brief Constructor for SubstrateView.
 *
 * Initializes the view with a QGraphicsScene and default interaction settings such as anti-aliasing and
 * drag mode. Sets the scene to the view.
 * \param parent Pointer to the parent QWidget.
 **********************************************************************************************************************/
SubstrateView::SubstrateView(QWidget* parent)
    : QGraphicsView(parent),
    m_zoomLocked(false),
    m_currentZoom(1.0),
    m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    grabGesture(Qt::PinchGesture);

    scale(1, -1);
}

/*!*******************************************************************************************************************
 * \brief Sets the substrate data to be visualized.
 *
 * Stores the substrate reference and triggers the drawing of the layers.
 * \param substrate The Substrate object containing layer information.
 **********************************************************************************************************************/
void SubstrateView::setSubstrate(const Substrate& substrate)
{
    m_substrate = substrate;
    m_highlightedName.clear();
    drawSubstrate();
}

void SubstrateView::tagStackItem(QGraphicsItem* item, const QString& name, const QString& kind,
                                 const QString& toolTip)
{
    if (!item)
        return;
    item->setData(kRoleName, name);
    item->setData(kRoleKind, kind);
    item->setCursor(Qt::PointingHandCursor);
    // Avoid Qt item-selection styling side effects on the whole scene.
    item->setFlag(QGraphicsItem::ItemIsSelectable, false);
    item->setAcceptedMouseButtons(Qt::LeftButton);
    if (!toolTip.isEmpty())
        item->setToolTip(toolTip);
}

void SubstrateView::setHighlightedLayer(const QString& name)
{
    if (m_highlightedName == name)
        return;
    const QString previous = m_highlightedName;
    m_highlightedName = name;
    // Only touch the previous + new layer — never re-set fonts/pens on the whole stack
    // (that was re-rasterizing every label and making them look bold).
    setLayerHighlightVisual(previous, false);
    setLayerHighlightVisual(m_highlightedName, true);
}

void SubstrateView::clearHighlight()
{
    if (m_highlightedName.isEmpty())
        return;
    const QString previous = m_highlightedName;
    m_highlightedName.clear();
    setLayerHighlightVisual(previous, false);
    emit highlightCleared();
}

void SubstrateView::setLayerHighlightVisual(const QString& name, bool on)
{
    if (!m_scene || name.isEmpty())
        return;

    const QPen normal(Qt::black);
    const QPen hi(QColor(255, 180, 0), 2.5);

    for (QGraphicsItem *item : m_scene->items()) {
        if (item->data(kRoleName).toString() != name)
            continue;
        if (auto *shape = qgraphicsitem_cast<QAbstractGraphicsShapeItem *>(item))
            shape->setPen(on ? hi : normal);
    }
}

void SubstrateView::applyHighlight()
{
    // Kept for redraw-after-setSubstrate: re-apply current highlight without
    // touching unrelated layers' fonts/pens.
    if (!m_highlightedName.isEmpty())
        setLayerHighlightVisual(m_highlightedName, true);
}

void SubstrateView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        if (QGraphicsItem *hit = itemAt(event->pos())) {
            for (QGraphicsItem *it = hit; it; it = it->parentItem()) {
                const QString name = it->data(kRoleName).toString();
                if (name.isEmpty())
                    continue;
                const QString kind = it->data(kRoleKind).toString();
                setHighlightedLayer(name);
                emit layerClicked(name, kind);
                break;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

/*!*******************************************************************************************************************
 * \brief Draws the background of the view.
 *
 * Overrides the default background rendering with a solid white background.
 * \param painter Pointer to the QPainter used for drawing.
 * \param rect The area of the background to be redrawn.
 **********************************************************************************************************************/
void SubstrateView::drawBackground(QPainter* painter, const QRectF& rect)
{
    Q_UNUSED(rect);
    painter->fillRect(viewport()->rect(), Qt::white);
}

/*!*******************************************************************************************************************
 * \brief Handles resize events for the view.
 *
 * Automatically fits the entire scene into the view when the window is resized.
 * \param event The QResizeEvent carrying the resize information.
 **********************************************************************************************************************/
void SubstrateView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    if (!m_zoomLocked) {
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }
}

/*!*******************************************************************************************************************
 * \brief Handles keyboard input events.
 *
 * Pressing the 'F' key resets the view transformation to fit the full substrate into view.
 * Other keys are passed to the base class handler.
 * \param event The QKeyEvent containing information about the key press.
 **********************************************************************************************************************/
void SubstrateView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        clearHighlight();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F) {
        resetZoom();
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event); // default handling
}

/*!*******************************************************************************************************************
 * \brief Handles mouse wheel events for zooming in and out.
 *
 * Scales the view based on the direction of the wheel scroll. Zoom level is constrained within set limits.
 * \param event The QWheelEvent containing the wheel movement delta.
 **********************************************************************************************************************/
void SubstrateView::wheelEvent(QWheelEvent* event)
{
    const double scaleFactor = 1.15;
    const double minZoom = 0.2;
    const double maxZoom = 20.0;

    double newZoom = m_currentZoom;

    if (event->angleDelta().y() > 0)
        newZoom *= scaleFactor;
    else if (event->angleDelta().y() < 0)
        newZoom /= scaleFactor;

    // Clamp zoom
    newZoom = std::clamp(newZoom, minZoom, maxZoom);

    // Set absolute zoom (not incremental)
    QTransform t;
    t.scale(newZoom, -newZoom); // still flipped Y
    setTransform(t);

    m_currentZoom = newZoom;
    m_zoomLocked = true;
}




/*!*******************************************************************************************************************
 * \brief Handles gesture events, including pinch zooming.
 *
 * Overrides the default viewport event handler to support pinch gestures. If a QPinchGesture is detected,
 * the view is scaled according to the pinch factor, with zoom constrained between minimum and maximum limits.
 *
 * \param event Pointer to the QEvent object containing gesture information.
 * \return True if the gesture was handled; otherwise, passes the event to the base class and returns its result.
 **********************************************************************************************************************/
bool SubstrateView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::Gesture) {
        QGestureEvent* gesture = static_cast<QGestureEvent*>(event);
        if (QPinchGesture* pinch = static_cast<QPinchGesture*>(gesture->gesture(Qt::PinchGesture))) {
            const double factor = pinch->scaleFactor();
            const double minZoom = 0.2;
            const double maxZoom = 20.0;

            double newZoom = m_currentZoom * factor;

            if (newZoom < minZoom)
                return true; // block zoom out
            if (newZoom > maxZoom)
                return true; // block zoom in

            scale(factor, factor);
            m_currentZoom = newZoom;
            m_zoomLocked = true;
            return true;
        }
    }

    return QGraphicsView::viewportEvent(event);
}



/*!*******************************************************************************************************************
 * \brief Resets the zoom and transformation to the default state.
 *
 * Clears any manual zoom applied via mouse or programmatically, restores vertical flip, and fits the full scene in view.
 **********************************************************************************************************************/
void SubstrateView::resetZoom()
{
    resetTransform(); // clear all scales
    scale(1, -1);      // restore flipped Y
    m_currentZoom = 1.0;
    m_zoomLocked = false;
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}


/*!*******************************************************************************************************************
 * \brief Returns the second highest thickness among dielectric layers, excluding the topmost and bottommost ones.
 *
 * The method skips the first and last dielectric layers in the stack and finds the second largest thickness value
 * among the remaining layers.
 *
 * \return The second highest thickness as a double. Returns 0.0 if less than 3 layers exist.
 **********************************************************************************************************************/
double SubstrateView::standardThickness() const
{
    auto dielectrics = m_substrate.dielectrics();

    if (dielectrics.size() < 3)
        return 0.0;

    QVector<double> thicknesses;
    for (int i = 1; i < dielectrics.size() - 1; ++i) {
        thicknesses.append(dielectrics[i].thickness());
    }

    std::sort(thicknesses.begin(), thicknesses.end(), std::greater<double>());

    if (thicknesses.size() >= 2)
        return thicknesses[1]; // second highest
    else
        return thicknesses[0]; // only one element
}

/*!*******************************************************************************************************************
 * \brief Finds the enclosing dielectric layer for a given metal or via layer.
 *
 * Searches the dielectric map to locate the dielectric that contains the specified layer name,
 * and then returns the corresponding dielectric VisualLayer object from the full list.
 *
 * \param layerName The name of the layer (typically a metal or via).
 * \param dielectricMap A map from dielectric names to their enclosed VisualLayer items.
 * \param allLayers The full list of VisualLayer objects (including dielectrics).
 * \return The enclosing dielectric as a VisualLayer object. Returns a default-constructed object if not found.
 **********************************************************************************************************************/
SubstrateView::VisualLayer SubstrateView::findEnclosingDielectric(const QString& layerName,
                                    const QMap<QString, QList<VisualLayer>>& dielectricMap,
                                    const QList<VisualLayer>& allLayers) const
{
    for (auto it = dielectricMap.begin(); it != dielectricMap.end(); ++it) {
        const QList<VisualLayer>& sublayers = it.value();
        for (const VisualLayer& vis : sublayers) {
            if (vis.name == layerName) {
                // Find the dielectric object in allLayers
                for (const VisualLayer& diel : allLayers) {
                    if (diel.name == it.key() && diel.type == "dielectric") {
                        return diel;
                    }
                }
            }
        }
    }

    return VisualLayer{};
}

/*!*******************************************************************************************************************
 * \brief Counts the number of metal or via layers enclosed by a given dielectric.
 *
 * Retrieves the list of VisualLayer objects associated with the dielectric name and returns the count.
 *
 * \param dielectricName The name of the dielectric layer.
 * \param dielectricMap A map from dielectric names to lists of VisualLayer items (conductors or vias).
 * \return The number of enclosed layers. Returns 0 if the dielectric is not found in the map.
 **********************************************************************************************************************/
int SubstrateView::countLayersInDielectric(const QString& dielectricName,
                                           const QMap<QString, QList<VisualLayer>>& dielectricMap) const
{
    if (!dielectricMap.contains(dielectricName))
        return 0;

    return dielectricMap.value(dielectricName).size();
}

/*!*******************************************************************************************************************
 * \brief Determines the index position of a layer within its enclosing dielectric.
 *
 * Searches all dielectric sublayer lists in the dielectric map to find the index of the given layer name.
 *
 * \param layerName The name of the metal or via layer.
 * \param dielectricMap A map from dielectric names to their enclosed VisualLayer lists.
 * \return The index position within the dielectric's list, or -1 if the layer is not found.
 **********************************************************************************************************************/
int SubstrateView::getLayerPositionInDielectric(const QString& layerName,
                                                const QMap<QString, QList<VisualLayer>>& dielectricMap) const
{
    for (auto it = dielectricMap.begin(); it != dielectricMap.end(); ++it) {
        const QList<VisualLayer>& layers = it.value();
        for (int i = 0; i < layers.size(); ++i) {
            if (layers[i].name == layerName) {
                return i;
            }
        }
    }

    return -1;
}


/*!*******************************************************************************************************************
 * \brief Draws a pseudo-3D visualization of the substrate layers.
 *
 * Clears the scene and draws each non-via layer with front, top, bottom, and side polygons,
 * creating a 3D appearance. Also adds text labels and tooltips for each layer.
 **********************************************************************************************************************/
void SubstrateView::drawSubstrate()
{
    m_scene->clear();

    const double dielWidth   = 300.0;
    const double depthOffset = 10.0;
    const double pixelScale  = 20.0;
    const double rightGutterX= dielWidth + 24.0;
    const double leftMargin  = 10.0;
    const double inset       = 2.0;

    QList<VisualLayer> allLayers;

    auto dielectrics = m_substrate.dielectrics();
    const bool useResolved =
        std::any_of(dielectrics.begin(), dielectrics.end(), [](const Dielectric &d) {
            return d.resolvedZmax() != d.resolvedZmin() || d.thickness() > 0;
        });

    if (!useResolved)
        std::reverse(dielectrics.begin(), dielectrics.end());

    const auto& layers    = m_substrate.layers();
    const auto& materials = m_substrate.materials();

    QHash<QString,int> contentCount;
    struct Span { QString name; double zmin, zmax; };
    QVector<Span> dielSpans; dielSpans.reserve(dielectrics.size());
    {
        if (useResolved) {
            for (const auto& d : dielectrics)
                dielSpans.push_back({ d.name(), d.resolvedZmin(), d.resolvedZmax() });
        } else {
            double z = -m_substrate.substrateOffset();
            for (const auto& d : dielectrics) {
                dielSpans.push_back({ d.name(), z, z + d.thickness() });
                z += d.thickness();
            }
        }
    }
    for (const Layer& L : layers) {
        if (L.type() == "dielectric" || L.name() == "LBE") continue;
        for (const auto& sp : dielSpans) {
            if (L.zmin() >= sp.zmin && L.zmax() <= sp.zmax) { contentCount[sp.name] += 1; break; }
        }
    }

    QVector<double> thicks; thicks.reserve(dielectrics.size());
    for (const auto& d : dielectrics) thicks.push_back(d.thickness());
    double tMed = trimmedMedian(thicks, 0.10);
    if (tMed <= 0.0) tMed = 1.0;

    const double emptyScale   = 0.30;
    const double busyBoostK   = 0.10;
    const int    busyBoostCap = 4;

    // Draw dielectrics bottom-up for visual stacking
    QList<Dielectric> drawOrder = dielectrics;
    if (useResolved) {
        std::sort(drawOrder.begin(), drawOrder.end(), [](const Dielectric &a, const Dielectric &b) {
            return a.resolvedZmin() < b.resolvedZmin();
        });
    }

    double zDraw = 0.0;
    for (const Dielectric& diel : drawOrder) {
        const double t = diel.thickness();
        double vUm = mapThicknessUmToVisualUm(t, tMed);
        const int n = contentCount.value(diel.name(), 0);
        double contentFactor = (n == 0) ? emptyScale : (1.0 + busyBoostK * std::min(n, busyBoostCap));
        vUm *= contentFactor;

        VisualLayer vis;
        vis.name     = diel.name();
        vis.type     = "dielectric";
        if (useResolved) {
            vis.realZMin = diel.resolvedZmin();
            vis.realZMax = diel.resolvedZmax();
        } else {
            // legacy spans already computed in dielSpans
            for (const auto &sp : dielSpans) {
                if (sp.name == diel.name()) {
                    vis.realZMin = sp.zmin;
                    vis.realZMax = sp.zmax;
                    break;
                }
            }
        }
        vis.zminPx   = zDraw * pixelScale;
        vis.zmaxPx   = (zDraw + vUm) * pixelScale;
        vis.color    = QColor(0, 0, 255, 77);
        allLayers.append(vis);

        zDraw += vUm;
    }

    for (const Layer& layer : layers) {
        if (layer.name() == "LBE") continue;
        VisualLayer vis;
        vis.name     = layer.name();
        vis.type     = layer.type();
        vis.realZMin = layer.zmin();
        vis.realZMax = layer.zmax();
        for (const Material& mat : materials) {
            if (mat.name() == layer.material()) { vis.color = mat.color(); break; }
        }
        if (!vis.color.isValid()) {
            vis.color = (vis.type == "via") ? QColor(150, 100, 0, 160)
                                            : QColor(200, 0, 0, 160);
        }
        allLayers.append(vis);
    }

    std::sort(allLayers.begin(), allLayers.end(),
              [](const VisualLayer& a, const VisualLayer& b){ return a.realZMin < b.realZMin; });

    QMap<QString, QList<VisualLayer>> dielectricMap;
    for (const VisualLayer& diel : allLayers) {
        if (diel.type != "dielectric") continue;
        QList<VisualLayer> enclosed;
        for (const VisualLayer& layer : allLayers) {
            if (layer.type == "dielectric") continue;
            if (layer.realZMin >= diel.realZMin && layer.realZMax <= diel.realZMax) enclosed.append(layer);
        }
        dielectricMap[diel.name] = enclosed;
    }

    auto physToPx = [&](double z) -> double {
        for (const auto& d : allLayers) {
            if (d.type != "dielectric") continue;
            if (z >= d.realZMin && z <= d.realZMax) {
                const double t = d.realZMax - d.realZMin;
                if (t <= 0) return d.zminPx;
                const double k = (d.zmaxPx - d.zminPx) / t;
                return d.zminPx + (z - d.realZMin) * k;
            }
        }

        const VisualLayer* first = nullptr;
        const VisualLayer* last  = nullptr;
        for (const auto& d : allLayers) {
            if (d.type != "dielectric") continue;
            if (!first || d.realZMin < first->realZMin) first = &d;
            if (!last  || d.realZMax > last->realZMax)  last  = &d;
        }
        if (!first || !last) return 0.0;

        if (z < first->realZMin) {
            const double t = first->realZMax - first->realZMin;
            const double k = (t > 0) ? (first->zmaxPx - first->zminPx) / t : 1.0;
            return first->zminPx + (z - first->realZMin) * k;
        } else {
            const double t = last->realZMax - last->realZMin;
            const double k = (t > 0) ? (last->zmaxPx - last->zminPx) / t : 1.0;
            return last->zmaxPx + (z - last->realZMax) * k;
        }
    };

    for (auto& layer : allLayers) {
        if (layer.name == "LBE" || layer.type == "dielectric") continue;

        VisualLayer refDielectric = findEnclosingDielectric(layer.name, dielectricMap, allLayers);

        if (!refDielectric.name.isEmpty()) {
            const int num = countLayersInDielectric(refDielectric.name, dielectricMap);
            if (num > 0) {
                const double bandPx = (refDielectric.zmaxPx - refDielectric.zminPx) / num;
                const int idx = getLayerPositionInDielectric(layer.name, dielectricMap);
                if (idx >= 0) {
                    layer.zminPx = refDielectric.zminPx + bandPx * idx;
                    layer.zmaxPx = refDielectric.zminPx + bandPx * (idx + 1);
                    continue;
                }
            }
        }

        layer.zminPx = physToPx(layer.realZMin);
        layer.zmaxPx = physToPx(layer.realZMax);
    }

    const double metalWidth = dielWidth / 1.5;
    const double viaWidth   = dielWidth / 6.0;

    // --- compute per-type bounding box and longest label ---
    double minDielH = 1e9, minCondH = 1e9, minViaH = 1e9;
    QString longestCond, longestVia;

    for (const auto& L : allLayers) {
        const double h = std::abs(L.zmaxPx - L.zminPx);
        if (L.type == "dielectric") minDielH = std::min(minDielH, h);
        else if (L.type == "conductor") { minCondH = std::min(minCondH, h); if (L.name.size() > longestCond.size()) longestCond = L.name; }
        else if (L.type == "via")       { minViaH  = std::min(minViaH,  h); if (L.name.size() > longestVia.size())  longestVia  = L.name; }
    }
    if (!std::isfinite(minDielH)) minDielH = 14.0;
    if (!std::isfinite(minCondH)) minCondH = 14.0;
    if (!std::isfinite(minViaH )) minViaH  = 14.0;

    auto makeLabelFont = [](double pt) {
        QFont f;
        f.setPointSizeF(pt);
        f.setBold(false);
        f.setWeight(QFont::Light);
        f.setStyleStrategy(QFont::PreferAntialias);
        return f;
    };

    auto fitFontPtToBox = [&](const QString& text, double targetW, double targetH){
        double lo = 6.0, hi = 22.0;
        for (int i=0;i<16;i++){
            double mid=(lo+hi)*0.5;
            QFont f = makeLabelFont(mid);
            QFontMetricsF fm(f);
            if (fm.height() <= targetH && fm.horizontalAdvance(text) <= targetW) lo = mid;
            else hi = mid;
        }
        return lo;
    };

    const double dielPt = std::max(6.0, std::min(22.0, minDielH - 4.0)); // only vertical fit for outer labels

    const double condBoxH = std::max(8.0, minCondH - 2*inset);
    const double viaBoxH  = std::max(8.0, minViaH  - 2*inset);
    const double condBoxW = std::max(8.0, metalWidth - 2*inset);
    const double viaBoxW  = std::max(8.0, viaWidth   - 2*inset);

    const double condPt = fitFontPtToBox(longestCond.isEmpty() ? "M" : longestCond, condBoxW, condBoxH);
    const double viaPt  = fitFontPtToBox(longestVia .isEmpty() ? "M" : longestVia , viaBoxW,  viaBoxH );

    const QFont dielFont = makeLabelFont(dielPt);
    const QFont condFont = makeLabelFont(condPt);
    const QFont viaFont  = makeLabelFont(viaPt);

    QFontMetricsF dielFm(dielFont);

    double lastLeftY = -1e9;

    for (const auto& layer : allLayers) {
        const double zStart = layer.zminPx;
        const double zStop  = layer.zmaxPx;
        const double thicknessUm = layer.realZMax - layer.realZMin;
        const QString tip = tr("%1\n"
                               "Type: %2\n"
                               "Thickness: %3 µm\n"
                               "Zmin: %4 µm\n"
                               "Zmax: %5 µm")
                                .arg(layer.name,
                                     layer.type,
                                     QString::number(thicknessUm, 'f', 4),
                                     QString::number(layer.realZMin, 'f', 4),
                                     QString::number(layer.realZMax, 'f', 4));

        double layerWidth = dielWidth;
        double xOffset    = 0.0;
        if (layer.type == "conductor") { layerWidth = metalWidth; xOffset = (dielWidth - layerWidth)/2.0; }
        else if (layer.type == "via")  { layerWidth = viaWidth;  xOffset = (dielWidth - layerWidth)/2.0; }

        QRectF frontRect(QPointF(xOffset, zStart), QPointF(xOffset + layerWidth, zStop));
        auto* frontFace = m_scene->addRect(frontRect, QPen(Qt::black), QBrush(layer.color));
        tagStackItem(frontFace, layer.name, layer.type, tip);

        QPolygonF bottomFace;
        bottomFace << QPointF(xOffset, zStop)
                   << QPointF(xOffset + depthOffset, zStop - depthOffset)
                   << QPointF(xOffset + layerWidth + depthOffset, zStop - depthOffset)
                   << QPointF(xOffset + layerWidth, zStop);
        tagStackItem(m_scene->addPolygon(bottomFace, QPen(Qt::black), QBrush(layer.color)),
                     layer.name, layer.type, tip);

        QPolygonF topFace;
        topFace << QPointF(xOffset, zStart)
                << QPointF(xOffset + depthOffset, zStart - depthOffset)
                << QPointF(xOffset + layerWidth + depthOffset, zStart - depthOffset)
                << QPointF(xOffset + layerWidth, zStart);
        tagStackItem(m_scene->addPolygon(topFace, QPen(Qt::black), QBrush(layer.color)),
                     layer.name, layer.type, tip);

        QPolygonF sideFace;
        sideFace << QPointF(xOffset + layerWidth, zStop)
                 << QPointF(xOffset + layerWidth + depthOffset, zStop - depthOffset)
                 << QPointF(xOffset + layerWidth + depthOffset, zStart - depthOffset)
                 << QPointF(xOffset + layerWidth, zStart);
        tagStackItem(m_scene->addPolygon(sideFace, QPen(Qt::black), QBrush(layer.color)),
                     layer.name, layer.type, tip);

        QPolygonF leftFace;
        leftFace << QPointF(xOffset, zStop)
                 << QPointF(xOffset, zStart)
                 << QPointF(xOffset + depthOffset, zStart - depthOffset)
                 << QPointF(xOffset + depthOffset, zStop - depthOffset);
        tagStackItem(m_scene->addPolygon(leftFace, QPen(Qt::black), QBrush(layer.color)),
                     layer.name, layer.type, tip);

        if (layer.type=="conductor" || layer.type=="via") {
            QFont f = (layer.type=="conductor")?condFont:viaFont;
            QGraphicsSimpleTextItem* label = m_scene->addSimpleText(layer.name);
            label->setFont(f);
            label->setBrush(Qt::black);
            tagStackItem(label, layer.name, layer.type, tip);

            QRectF target = frontRect.adjusted(inset, inset, -inset, -inset);
            QRectF tb = label->boundingRect();
            QPointF c = target.center();
            double xLab = c.x() - tb.width()*0.35;
            double yLab = c.y() + tb.height()*0.25;
            label->setPos(QPointF(xLab, yLab));
            label->setTransform(QTransform::fromScale(1, -1), true);
        }

        if (layer.type == "dielectric") {
            const QString heightLabel = QString::number(layer.realZMax, 'f', 3) + " µm";
            const QString nameLabel   = QString("%1 (%2 µm)")
                                          .arg(layer.name.trimmed())
                                          .arg(layer.realZMax - layer.realZMin, 0, 'f', 3);

            QGraphicsSimpleTextItem* leftText = m_scene->addSimpleText(heightLabel);
            leftText->setFont(dielFont);
            leftText->setBrush(Qt::black);
            tagStackItem(leftText, layer.name, layer.type, tip);
            QRectF lb = leftText->boundingRect();

            double xL = -(leftMargin + lb.width());
            double yTop = frontRect.bottom();
            double yL = yTop + lb.height() * 0.5;
            const double minGap = lb.height() + 2.0;
            if (yL < lastLeftY + minGap) yL = lastLeftY + minGap;

            leftText->setPos(QPointF(xL, yL));
            leftText->setTransform(QTransform::fromScale(1, -1), true);
            lastLeftY = yL;

            QGraphicsSimpleTextItem* rightText = m_scene->addSimpleText(nameLabel);
            rightText->setFont(dielFont);
            rightText->setBrush(Qt::black);
            tagStackItem(rightText, layer.name, layer.type, tip);
            QRectF rb = rightText->boundingRect();

            QRectF rTarget = frontRect.adjusted(inset, inset, -inset, -inset);
            QPointF rc = rTarget.center();
            double yC = rc.y() + rb.height() * 0.25; // your preferred centering tweak

            rightText->setPos(QPointF(rightGutterX, yC));
            rightText->setTransform(QTransform::fromScale(1, -1), true);
        }
    }

    m_scene->setSceneRect(m_scene->itemsBoundingRect());
    if (!m_highlightedName.isEmpty())
        applyHighlight();
    resetZoom();
}

/*!*******************************************************************************************************************
 * \brief Computes the median of a numeric vector.
 *
 * This function sorts a local copy of the input (on purpose, to keep the caller's
 * container intact) and returns the middle value. For an even number of elements,
 * it returns the arithmetic mean of the two central values.
 *
 * Complexity: \c O(n log n) due to sorting.
 *
 * \param v Vector of values (passed by value intentionally).
 * \return Median value, or \c 0.0 if the vector is empty.
 **********************************************************************************************************************/
double SubstrateView::median(QVector<double> v) {
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    const int n = v.size();
    return (n & 1) ? v[n/2] : 0.5*(v[n/2-1]+v[n/2]);
}

/*!*******************************************************************************************************************
 * \brief Computes a trimmed median of a numeric vector.
 *
 * Removes a symmetric fraction of the smallest and largest elements and then
 * returns the median of the remaining data. Trimming is specified as a fraction
 * in the range \c [0.0, 0.5). For example, \c trim=0.10 removes 10% from the
 * low end and 10% from the high end (total 20%).
 *
 * The function clamps the effective number of elements trimmed so that at least
 * one element always remains after trimming.
 *
 * Complexity: \c O(n log n) due to sorting.
 *
 * \param v     Vector of values (passed by value intentionally).
 * \param trim  Fraction to remove from each tail, \c 0.0 ≤ trim < \c 0.5.
 * \return Trimmed-median value, or \c 0.0 if the vector is empty.
 **********************************************************************************************************************/
double SubstrateView::trimmedMedian(QVector<double> v, double trim) {
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    int n = v.size();
    int cut = int(std::floor(trim*n));
    cut = std::min(cut, std::max(0, n-1));
    v = v.mid(cut, n - 2*cut);
    return median(v);
}

/*!*******************************************************************************************************************
 * \brief Maps a physical thickness (µm) to a visual thickness (µm) with limits and
 *        gentle non-linear compression.
 *
 * The mapping uses three policy parameters stored in \c m_visPolicy:
 *  - \c minFactor : lower visual bound relative to the median thickness (\c tMed)
 *  - \c maxFactor : upper visual bound relative to \c tMed
 *  - \c gamma     : compression exponent (\c 1.0 = linear; \c <1 compresses outliers)
 *
 * More precisely:
 *   - Compute \c r = max(t / tMed, 1e-9).
 *   - Visual thickness \c v = tMed * pow(r, gamma).
 *   - Clamp \c v to [\c tMed*minFactor, \c tMed*maxFactor].
 *
 * If \c tMed ≤ 0, the function falls back to returning the physical thickness \c t.
 *
 * \param t     Physical thickness in micrometers (µm).
 * \param tMed  Median (or robust scale) used as a reference (µm).
 * \return Visual thickness in micrometers (µm) after compression and clamping.
 **********************************************************************************************************************/
double SubstrateView::mapThicknessUmToVisualUm(double t, double tMed) const {
    if (tMed <= 0.0)
        return t;
    const auto &p = m_visPolicy;
    const double minUm = tMed * p.minFactor;
    const double maxUm = tMed * p.maxFactor;

    const double r = std::max(t / tMed, 1e-9);
    double v = tMed * std::pow(r, p.gamma);

    return std::clamp(v, minUm, maxUm);
}

