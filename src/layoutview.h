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

#ifndef LAYOUTVIEW_H
#define LAYOUTVIEW_H

#include <QHash>
#include <QColor>
#include <QString>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include <QGraphicsView>
#include <QGraphicsScene>

#include "gdslayout.h"

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \class LayoutView
 * \brief A QGraphicsView-based top-view preview of flattened GDS polygons.
 *
 * LayoutView draws layout geometry next to the Substrate stackup on the Substrate tab.
 * Polygons are colored from the stackup materials when a GDS layer maps to a named layer;
 * unmapped layers (e.g. port markers 201/202) are shown with a distinct port style.
 *
 * Interaction mirrors SubstrateView:
 * - Left-click a polygon to highlight it and emit \c layerClicked.
 *   Repeated clicks at the same spot cycle through overlapping layers under the cursor
 *   (top → next below → … → top).
 * - Shift+Left-click: measure ruler (start / end); Esc clears measure + highlight.
 * - Escape clears the highlight and emits \c highlightCleared.
 * - Mouse wheel zooms; F / Home fits the view; pan via scroll-hand drag.
 *
 * Per-GDS-layer visibility and fill opacity can be driven from LayoutLayerPanel.
 *
 * Y coordinates from GDS (Y-up) are flipped for Qt display (Y-down) when polygons are added.
 * Cursor readout (µm) follows the mouse in the view.
 *
 * \see GdsLayout::flattenTopCell, SubstrateView, MainWindow::refreshLayoutPreview
 **********************************************************************************************************************/
class LayoutView : public QGraphicsView
{
    Q_OBJECT

public:
    /*!*******************************************************************************************************************
     * \struct LayerStyle
     * \brief Visual style for one GDS layer number in the preview.
     *
     * \var name   Stackup / display name used for highlight sync with SubstrateView.
     * \var kind   Layer kind string (conductor, via, port, …) forwarded in \c layerClicked.
     * \var color  Fill color (alpha applied when drawing).
     * \var order  Draw order; lower values are painted underneath.
     **********************************************************************************************************************/
    struct LayerStyle
    {
        QString name;
        QString kind;
        QColor  color;
        int     order = 0;
    };

    explicit LayoutView(QWidget *parent = nullptr);

    void                        clear();
    void                        setPolygons(const QVector<GdsFlatPolygon> &polys,
                                            const QHash<int, LayerStyle> &styles,
                                            const QHash<int, QString> &portDirections = {});
    void                        setHighlightedLayer(const QString &name);
    void                        clearHighlight();

    void                        setLayerVisible(int gdsLayer, bool visible);
    void                        setLayerOpacity(int gdsLayer, qreal opacity);
    bool                        isLayerVisible(int gdsLayer) const;
    qreal                       layerOpacity(int gdsLayer) const;

    void                        clearMeasure();
    void                        setShowCoordinates(bool on);
    bool                        showCoordinates() const;

signals:
    /*! Emitted when the user clicks a polygon; \a name / \a kind match stack item tagging. */
    void                        layerClicked(const QString &name, const QString &kind);
    /*! Emitted when Esc (or equivalent) clears the layout highlight. */
    void                        highlightCleared();
    /*! Cursor position in GDS micrometres (Y-up). */
    void                        cursorUmChanged(qreal xUm, qreal yUm);
    /*! Measure segment in GDS micrometres; both ends valid when \a active. */
    void                        measureChanged(bool active, qreal dxUm, qreal dyUm, qreal lenUm);

protected:
    void                        drawBackground(QPainter *painter, const QRectF &rect) override;
    void                        drawForeground(QPainter *painter, const QRectF &rect) override;
    void                        wheelEvent(QWheelEvent *event) override;
    void                        keyPressEvent(QKeyEvent *event) override;
    void                        resizeEvent(QResizeEvent *event) override;
    void                        mousePressEvent(QMouseEvent *event) override;
    void                        mouseMoveEvent(QMouseEvent *event) override;
    void                        mouseReleaseEvent(QMouseEvent *event) override;
    void                        leaveEvent(QEvent *event) override;

private:
    void                        applyHighlight();
    void                        setLayerHighlightVisual(const QString &name, bool on);
    void                        fitContent();
    void                        applyLayerVisual(int gdsLayer);
    qreal                       opacityFor(int gdsLayer) const;
    bool                        visibleFor(int gdsLayer) const;
    void                        addPortArrow(const QPointF &origin,
                                             const QPointF &dirScene,
                                             const QColor &color,
                                             const QString &name,
                                             const QString &kind,
                                             int gdsLayer,
                                             bool visible,
                                             qreal z,
                                             const QString &dirLabel);
    /*! ⊙ for +z / ⊗ for -z (out-of-plane via ports). */
    void                        addPortOutOfPlaneMarker(const QPointF &origin,
                                                        const QColor &color,
                                                        const QString &name,
                                                        const QString &kind,
                                                        int gdsLayer,
                                                        bool visible,
                                                        qreal z,
                                                        const QString &dirLabel);
    static QPointF              sceneToGdsUm(const QPointF &scenePt);
    void                        emitMeasure();
    static void                 setPixelOffset(QGraphicsItem *item, qreal dxPx, qreal dyPx);

    QGraphicsScene             *m_scene = nullptr;
    bool                        m_zoomLocked = false;
    QString                     m_highlightedName;
    QHash<int, bool>            m_layerVisible;  // missing => true
    QHash<int, qreal>           m_layerOpacity;  // missing => 1.0

    bool                        m_cursorValid = false;
    bool                        m_showCoordinates = true;
    bool                        m_panning = false;
    QPoint                      m_panLast;
    QPointF                     m_cursorScene;
    QPoint                      m_cursorView;
    bool                        m_measureHasStart = false;
    bool                        m_measureHasEnd = false;
    QPointF                     m_measureStart;
    QPointF                     m_measureEnd;

    static constexpr int        kRoleName = 0;
    static constexpr int        kRoleKind = 1;
    static constexpr int        kRoleBrush = 2; // original fill QColor before highlight
    static constexpr int        kRoleGds = 3;
    static constexpr int        kRoleIsPort = 4;
    static constexpr int        kRolePen = 5;   // original QColor for port line pen
    static constexpr int        kBaseFillAlpha = 150;
    static constexpr int        kPortPenWidth = 4;
};

#endif // QT_VERSION

#endif // LAYOUTVIEW_H
