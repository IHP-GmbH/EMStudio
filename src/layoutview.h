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
#include <QImage>
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
 * Floating 2D/3D and Field controls (top-right): Iso3D extrudes from stack
 * \c zmin/\c zmax; Field shows a Z-clip heatmap overlay (mutually exclusive with 3D).
 * Choices are stored under QSettings LayoutPreview/view3d and viewField.
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

    /*! In-plane vector sample on a Z clip (GDS µm, Y-up). */
    struct FieldArrow
    {
        qreal xUm = 0.0;
        qreal yUm = 0.0;
        qreal dx = 0.0; //!< In-plane X component (GDS)
        qreal dy = 0.0; //!< In-plane Y component (GDS, Y-up)
        qreal mag = 1.0;
    };

    /*! Heatmap + optional arrows from \c field_slice_export.py cache. */
    struct FieldOverlay
    {
        QImage  image; //!< RGBA heatmap
        qreal   xminUm = 0.0;
        qreal   xmaxUm = 0.0;
        qreal   yminUm = 0.0; //!< GDS Y-up
        qreal   ymaxUm = 0.0;
        qreal   zUm = 0.0;
        qreal   zMinUm = 0.0;
        qreal   zMaxUm = 1.0;
        QString quantity;
        QString status; //!< Empty when overlay is valid; else user-facing message
        QVector<FieldArrow> arrows;
        bool    logScale = false;
        bool    showArrows = true;
        bool    valid() const { return !image.isNull() && xmaxUm > xminUm && ymaxUm > yminUm; }
    };

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

    /*!*******************************************************************************************************************
     * \brief Enables or disables Field mode (Z-clip heatmap overlay).
     *
     * Mutually exclusive with Iso3D: turning Field on forces Top2D. Persists
     * under QSettings LayoutPreview/viewField and emits \c fieldModeChanged.
     *
     * \param on True to show the Field panel and request a slice export.
     **********************************************************************************************************************/
    void                        setFieldMode(bool on);
    /*! True while Field mode is active (Iso3D button disabled). */
    bool                        isFieldMode() const { return m_fieldOn; }
    /*!*******************************************************************************************************************
     * \brief Replaces the current Field overlay and refreshes the floating panel.
     *
     * When Field mode is on, rebuilds the scene so the heatmap / arrows redraw.
     *
     * \param overlay Heatmap image, GDS µm bounds, Z range, quantity, arrows.
     **********************************************************************************************************************/
    void                        setFieldOverlay(const FieldOverlay &overlay);
    /*! Clears heatmap / status and rebuilds the scene if Field mode is on. */
    void                        clearFieldOverlay();
    /*! Last overlay applied via \c setFieldOverlay (may be invalid / status-only). */
    const FieldOverlay         &fieldOverlay() const { return m_field; }
    /*!*******************************************************************************************************************
     * \brief Z clip [µm] implied by the Field Z slider within overlay zMin..zMax.
     **********************************************************************************************************************/
    qreal                       fieldClipZUm() const;
    /*! True when the Field panel Log checkbox is checked. */
    bool                        fieldLogScale() const;
    /*! True when the Field panel Arrows checkbox is checked. */
    bool                        fieldShowArrows() const;
    /*!*******************************************************************************************************************
     * \brief GDS µm Y-up bounding box of non-port layout polygons (for field crop).
     *
     * Skips port GDS layers (201–299) and styles tagged kind=port so the exporter
     * frames the DUT rather than port markers.
     **********************************************************************************************************************/
    QRectF                      layoutContentBoundsUm() const;

signals:
    /*! Emitted when the user clicks a polygon; \a name / \a kind match stack item tagging. */
    void                        layerClicked(const QString &name, const QString &kind);
    /*! Emitted when Esc (or equivalent) clears the layout highlight. */
    void                        highlightCleared();
    /*! Field mode toggled (MainWindow should load / clear field dumps). */
    void                        fieldModeChanged(bool on);
    /*! Z-clip or display options changed; MainWindow should re-export the slice. */
    void                        fieldSliceRequest(qreal zUm, bool logScale, bool showArrows);
    /*! User asked to jump to the hottest Z (max |E| / temperature in layout ROI). */
    void                        fieldHotZRequest();
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
    /*! Field toolbutton toggled → \c setFieldMode. */
    void                        onFieldButtonToggled(bool on);
    /*! Log / Arrows changed; may re-export or only redraw arrows. */
    void                        onFieldControlsChanged();
    /*! Live Z readout while dragging; export deferred until release. */
    void                        onFieldZSliderPreview(int value);
    /*! Slider released → emit \c fieldSliceRequest for a new export. */
    void                        onFieldZSliderCommitted();
    /*! Max / hot-Z button → emit \c fieldHotZRequest. */
    void                        onFieldHotZClicked();

private:
    void                        applyHighlight();
    void                        setLayerHighlightVisual(const QString &name, bool on);
    void                        applyLayerVisual(int gdsLayer);
    qreal                       opacityFor(int gdsLayer) const;
    bool                        visibleFor(int gdsLayer) const;
    /*!*******************************************************************************************************************
     * \brief Adds the Field heatmap pixmap under layout polygons (scene Y-down).
     **********************************************************************************************************************/
    void                        addFieldOverlayItems();
    /*! Syncs Z slider / Log / Arrows widgets from \c m_field without re-export. */
    void                        updateFieldControlsFromOverlay();
    /*! Shows/hides Field panel, updates status line, repositions floating controls. */
    void                        syncFloatingControls();
    /*! Emits \c fieldSliceRequest from current slider / checkbox state. */
    void                        emitFieldSliceRequest();
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
    void                        rebuildScene2D(bool refit);
    void                        rebuildScene3D(bool refit);
    /*! Fit viewport to layout (Field: zoomed-in); scene still holds full field for zoom-out. */
    void                        fitPreferredContent();
    /*! Fit viewport to the full sceneRect (layout ∪ field domain). */
    void                        fitFullContent();
    void                        fitContent();

    void                        repositionFloatingControls();
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
    class QToolButton          *m_fieldBtn = nullptr;
    class QWidget              *m_fieldPanel = nullptr;
    class QSlider              *m_fieldZSlider = nullptr;
    class QToolButton          *m_fieldHotZBtn = nullptr;
    class QCheckBox            *m_fieldLogChk = nullptr;
    class QCheckBox            *m_fieldArrowsChk = nullptr;
    class QLabel               *m_fieldStatusLbl = nullptr;
    ViewMode                    m_viewMode = ViewMode::Top2D;
    bool                        m_fieldOn = false;
    FieldOverlay                m_field;
    bool                        m_blockFieldControls = false;

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
