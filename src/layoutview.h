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
 * A floating 2D/3D control (top-right) toggles top view vs isometric extrusion from
 * stack \c zmin/\c zmax; the choice is stored in QSettings under LayoutPreview/view3d.
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
        double  zminUm = 0.0; //!< Stack Z bottom [µm]; used in 3D mode
        double  zmaxUm = 0.0; //!< Stack Z top [µm]; used in 3D mode
        bool    hasZ = false; //!< True when zmin/zmax came from substrate
    };

    /*! Port marker: layout XY + stack Z from Ports From/To (XML). */
    struct PortInfo
    {
        QString direction;     //!< x / -x / y / -y / z / -z
        QString fromLayer;     //!< Stack name or GDS number
        QString toLayer;       //!< Stack name or GDS number
        double  zFromUm = 0.0; //!< Mid-Z of From layer [µm]
        double  zToUm = 0.0;   //!< Mid-Z of To layer [µm]
        bool    hasFromZ = false;
        bool    hasToZ = false;
    };

    /*! Top-down (2D) vs isometric extrusion (3D) preview. */
    enum class ViewMode { Top2D, Iso3D };

    explicit LayoutView(QWidget *parent = nullptr);

    void                        clear();
    void                        setPolygons(const QVector<GdsFlatPolygon> &polys,
                                            const QHash<int, LayerStyle> &styles,
                                            const QHash<int, PortInfo> &ports = {});
    void                        setHighlightedLayer(const QString &name);
    void                        clearHighlight();

    void                        setLayerVisible(int gdsLayer, bool visible);
    void                        setLayerOpacity(int gdsLayer, qreal opacity);
    bool                        isLayerVisible(int gdsLayer) const;
    qreal                       layerOpacity(int gdsLayer) const;

    void                        clearMeasure();
    void                        setShowCoordinates(bool on);
    bool                        showCoordinates() const;

    void                        setViewMode(ViewMode mode);
    ViewMode                    viewMode() const { return m_viewMode; }
    bool                        isView3d() const { return m_viewMode == ViewMode::Iso3D; }

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
    bool                        viewportEvent(QEvent *event) override;
    void                        keyPressEvent(QKeyEvent *event) override;
    void                        resizeEvent(QResizeEvent *event) override;
    void                        mousePressEvent(QMouseEvent *event) override;
    void                        mouseMoveEvent(QMouseEvent *event) override;
    void                        mouseReleaseEvent(QMouseEvent *event) override;
    void                        leaveEvent(QEvent *event) override;

private slots:
    void                        onModeButtonToggled(bool on);

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
    /*! Scene-space shaft + tip arrowhead (3D Z ports / injection face). */
    void                        addPortArrowAlong(const QPointF &tailScene,
                                                  const QPointF &tipScene,
                                                  const QColor &color,
                                                  const QString &name,
                                                  const QString &kind,
                                                  int gdsLayer,
                                                  bool visible,
                                                  qreal z,
                                                  const QString &dirLabel);
    /*! Inward in-plane arrow for top-view Z ports (tip on injection edge). */
    void                        addPortInwardArrow(const QPointF &tipScene,
                                                   const QPointF &towardScene,
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

    void                        rebuildScene(bool refit = true);
    void                        rebuildScene2D();
    void                        rebuildScene3D(bool refit);
    void                        repositionModeButton();
    void                        loadViewModeFromSettings();
    void                        saveViewModeToSettings() const;
    void                        resetOrbitAngles();
    void                        updateOrbitCenter();
    /*! Resolve stack name or GDS number to layer mid-Z from \c m_styles (XML stack). */
    bool                        layerMidZ(const QString &nameOrGds, qreal *zMid) const;
    /*! Orthographic projection after yaw/pitch orbit; Qt Y-down, stack Z up. */
    QPointF                     project3D(qreal xUm, qreal yUm, qreal zUm) const;
    qreal                       depth3D(qreal xUm, qreal yUm, qreal zUm) const;

    QGraphicsScene             *m_scene = nullptr;
    class QToolButton          *m_modeBtn = nullptr;
    ViewMode                    m_viewMode = ViewMode::Top2D;

    QVector<GdsFlatPolygon>     m_polys;
    QHash<int, LayerStyle>      m_styles;
    QHash<int, PortInfo>        m_ports;

    bool                        m_zoomLocked = false;
    QString                     m_highlightedName;
    QHash<int, bool>            m_layerVisible;  // missing => true
    QHash<int, qreal>           m_layerOpacity;  // missing => 1.0

    bool                        m_cursorValid = false;
    bool                        m_showCoordinates = true;
    bool                        m_panning = false;
    bool                        m_orbiting = false;
    bool                        m_leftPressPending = false; //!< Left down; select on release if not dragged
    QPoint                      m_panLast;
    QPoint                      m_pressPos;
    qreal                       m_yawDeg = 45.0;   //!< Orbit around stack Z [deg]
    qreal                       m_pitchDeg = 30.0; //!< Orbit pitch [deg], clamped
    qreal                       m_orbitCx = 0.0;
    qreal                       m_orbitCy = 0.0;
    qreal                       m_orbitCz = 0.0;
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
