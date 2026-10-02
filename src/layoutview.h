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
#include "navigationstyle.h"

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \class LayoutView
 * \brief A QGraphicsView-based top-view preview of flattened GDS polygons.
 *
 * LayoutView draws layout geometry next to the Substrate stackup on the Substrate tab.
 * Polygons are colored from the stackup materials when a GDS layer maps to a named layer;
 * unmapped layers (e.g. port markers 201/202) are shown with a distinct port style.
 * Floating 2D/3D and Field controls (top-right): Iso3D extrudes from stack
 * \c zmin/\c zmax; Field shows a Z-clip heatmap (pane stays 2D; 3D opens PyVista).
 * Choices are stored under QSettings LayoutPreview/view3d and viewField.
 *
 * Interaction:
 * - Left-click a polygon to highlight it and emit \c layerClicked.
 *   Repeated clicks at the same spot cycle through overlapping layers under the cursor
 *   (top → next below → … → top).
 * - Mouse navigation (wheel, drags) follows the \c NavStyle preset set with
 *   \c setNavigationStyle (Setup → Key Bindings); the full list of mouse and key
 *   bindings is \c NavigationStyle::bindingTable.
 * - M arms the measure ruler (clicks set start / end); Ctrl+Shift+click measures any time.
 * - Escape clears highlight, ruler and probe and emits \c highlightCleared.
 *
 * Per-GDS-layer visibility and fill opacity can be driven from LayoutLayerPanel.
 * The opacity value is the true 2D fill opacity; ports, highlights and Iso3D scale
 * their own styling by the same ratio to the default (\c opacityFor).
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
        double  fromZminUm = 0.0; //!< Bottom / top of the From layer [µm] (port surface in Iso3D)
        double  fromZmaxUm = 0.0;
        double  toZminUm = 0.0;   //!< Bottom / top of the To (in-plane: target) layer [µm]
        double  toZmaxUm = 0.0;
        bool    hasFromRange = false;
        bool    hasToRange = false;
        QString thermalKind;   //!< Elmer Thermal marker: "heatsource" / "consttemp"; empty for EM ports
    };

    /*! Top-down (2D) vs isometric extrusion (3D) preview. */
    enum class ViewMode { Top2D, Iso3D };

    /*! Heatmap / volume frame from \c field_slice_export.py cache. */
    struct FieldOverlay
    {
        QImage  image; //!< RGBA heatmap (2D) or volume screenshot (3D)
        qreal   xminUm = 0.0;
        qreal   xmaxUm = 0.0;
        qreal   yminUm = 0.0; //!< GDS Y-up (2D) or image Y (volume)
        qreal   ymaxUm = 0.0;
        qreal   zUm = 0.0;
        qreal   zMinUm = 0.0;
        qreal   zMaxUm = 1.0;
        QString quantity;
        QString status; //!< Empty when overlay is valid; else user-facing message
        /*! Scalar grid matching \c image (row 0 = ymax); enables click-to-probe. */
        QVector<float> sampleGrid;
        int     sampleNx = 0;
        int     sampleNy = 0;
        bool    logScale = false;
        bool    volume = false; //!< True for Field+3D offscreen volume PNG
        bool    valid() const { return !image.isNull() && xmaxUm > xminUm && ymaxUm > yminUm; }
        bool    hasSamples() const
        {
            return sampleNx > 1 && sampleNy > 1
                    && sampleGrid.size() == sampleNx * sampleNy;
        }
    };

    explicit LayoutView(QWidget *parent = nullptr);

    void                        clear();
    void                        setPolygons(const QVector<GdsFlatPolygon> &polys,
                                            const QHash<int, LayerStyle> &styles,
                                            const QHash<int, PortInfo> &ports = {});
    /*! Via merge distance of the model (merge_polygon_size); < 0 = not set. See setViaMergeSize. */
    void                        setViaMergeSize(qreal um);
    qreal                       viaMergeSize() const { return m_viaMergeUm; }
    /*! Most polygons a via layer may have (after merging) before the layout is replaced by a
     *  message; preference LAYOUT_MAX_VIA_POLYGONS. */
    void                        setMaxViaPolygonsPerLayer(int maxPolygons);
    /*! Via layers over the limit, "<name>: <count>" each; empty when the layout is drawn. */
    QStringList                 denseViaLayers() const;
    /*! Polygons as drawn (via layers merged per setViaMergeSize). */
    const QVector<GdsFlatPolygon> &drawnPolygons() const { return m_polys; }
    void                        setHighlightedLayer(const QString &name);
    void                        clearHighlight();

    void                        setLayerVisible(int gdsLayer, bool visible);
    /*! Sets the 2D fill opacity (0..1) of one layer's shapes, as drawn. */
    void                        setLayerOpacity(int gdsLayer, qreal opacity);
    /*! Sets the 2D fill opacity (0..1) of all layers in one pass; the Field image is unaffected. */
    void                        setAllLayerOpacity(qreal opacity);
    bool                        isLayerVisible(int gdsLayer) const;
    /*! 2D fill opacity (0..1) actually used for the layer's shapes. */
    qreal                       layerOpacity(int gdsLayer) const;
    /*! Fill opacity of layers that were never changed (\c kBaseFillAlpha / 255). */
    static qreal                defaultFillOpacity() { return kBaseFillAlpha / 255.0; }

    void                        clearMeasure();
    /*! Clears Field click-probe marker and readout. */
    void                        clearFieldProbe();
    void                        setShowCoordinates(bool on);
    bool                        showCoordinates() const;

    /*! Selects the mouse navigation preset (wheel / drag mapping); updates tooltips. */
    void                        setNavigationStyle(NavStyle style);
    NavStyle                    navigationStyle() const { return m_navStyle; }
    /*! True while M measure mode is armed (left clicks place ruler points). */
    bool                        isMeasureArmed() const { return m_measureArmed; }
    /*! True when a measure start point is set. */
    bool                        hasMeasure() const { return m_measureHasStart; }

    void                        setViewMode(ViewMode mode);
    ViewMode                    viewMode() const { return m_viewMode; }
    bool                        isView3d() const { return m_viewMode == ViewMode::Iso3D; }

    /*!*******************************************************************************************************************
     * \brief Enables or disables Field mode (Z-clip heatmap or volume).
     *
     * Field = Z-slice overlay on layout (always Top2D in this pane). 3D while
     * Field is on emits \c fieldExternalVolumeRequested. Emits \c fieldModeChanged
     * (not persisted: the Fields page owns the mode).
     *
     * \param on True to show the Field panel and request an export.
     **********************************************************************************************************************/
    void                        setFieldMode(bool on);
    /*! True while Field mode is active. */
    bool                        isFieldMode() const { return m_fieldOn; }
    /*! Shows or hides the Field toggle button (hidden when a page owns the Field mode). */
    void                        setFieldToggleVisible(bool visible);

    /*! Zoom / pan of one page that shows this view (Substrate, Fields). */
    struct ViewState
    {
        QTransform              transform;
        QPointF                 center;          //!< Scene point at the viewport centre
        bool                    userZoomed = false; //!< Zoomed or panned by hand (else auto-fit)
    };
    /*! Current zoom / pan. */
    ViewState                   viewState() const;
    /*! Restores a zoom / pan saved with \c viewState; an auto-fit state just refits. */
    void                        restoreViewState(const ViewState &state);
    /*! Field+3D no longer uses an in-pane volume; always false (2D pane). */
    bool                        isFieldVolume() const { return false; }
    /*! Current orbit yaw [deg] (shared by Iso3D layout and Field volume camera). */
    qreal                       orbitYawDeg() const { return m_yawDeg; }
    /*! Current orbit pitch [deg]. */
    qreal                       orbitPitchDeg() const { return m_pitchDeg; }
    /*! Field-volume camera zoom (>1 = closer). Pixel zoom of the PNG is disabled. */
    qreal                       volumeCameraZoom() const { return m_volumeCamZoom; }
    /*! Preferred volume render size [px] from the current viewport (HiDPI-aware). */
    int                         volumeRenderResolution() const;
    /*!*******************************************************************************************************************
     * \brief Replaces the current Field overlay and refreshes the floating panel.
     *
     * When Field mode is on, rebuilds the scene so the heatmap redraws.
     *
     * \param overlay Heatmap image, GDS µm bounds, Z range, quantity.
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
    /*! True when the Field panel Probe/Temp checkbox is checked (click-probe). */
    bool                        fieldShowTemp() const;
    /*!*******************************************************************************************************************
     * \brief Relabels the probe checkbox: \c Temp for Elmer Thermal, \c Probe for EM Field.
     **********************************************************************************************************************/
    void                        setFieldProbeThermal(bool thermal);
    /*!
     * \brief Fills the Field panel's result file / cycle picker (hidden if < 2 entries).
     */
    void                        setFieldChoices(const QStringList &labels, int current);
    /*! Selected index of the result file / cycle picker, or -1. */
    int                         fieldChoiceIndex() const;
    /*! GDS µm Y-up bounding box of non-port layout polygons (for field crop). */
    QRectF                      layoutContentBoundsUm() const;

    /*! Timing / counts from the last Iso3D \c rebuildScene3D (for tests / profiling). */
    struct Iso3dRebuildStats {
        qint64 ms = 0;
        int    inputPolyCount = 0;
        int    viaPolyCount = 0;
        int    viaEnvelopeCount = 0; //!< After ADS-style growEnvelope merge
        int    faceCount = 0;
        int    sceneItemCount = 0;
        bool   usedPixmap = false;
        bool   mergedVias = false;
    };
    Iso3dRebuildStats           lastIso3dRebuildStats() const { return m_lastIso3dStats; }

signals:
    /*! Emitted when the user clicks a polygon; \a gdsLayer is the GDS number (-1 if unknown). */
    void                        layerClicked(const QString &name, const QString &kind, int gdsLayer);
    /*! Emitted when Esc (or equivalent) clears the layout highlight. */
    void                        highlightCleared();
    /*! The preview switched between top view and Iso3D. */
    void                        viewModeChanged(bool iso3d);
    /*! Field mode toggled (MainWindow should load / clear field dumps). */
    void                        fieldModeChanged(bool on);
    /*! Shift+F: switch to (on) or away from (off) the Fields page. Without a receiver,
     *  Shift+F toggles Field mode directly. */
    void                        fieldPageRequested(bool on);
    /*! Field is on and user pressed 3D — open external PyVista volume window. */
    void                        fieldExternalVolumeRequested();
    /*! Z-clip or display options changed; MainWindow should re-export the slice. */
    void                        fieldSliceRequest(qreal zUm, bool logScale);
    /*! User asked to jump to the hottest Z (max |E| / temperature in layout ROI). */
    void                        fieldHotZRequest();
    /*! User picked another result file / cycle in the Field panel. */
    void                        fieldChoiceChanged(int index);
    /*! Cursor position in GDS micrometres (Y-up). */
    void                        cursorUmChanged(qreal xUm, qreal yUm);
    /*! Measure segment in GDS micrometres; both ends valid when \a active. */
    void                        measureChanged(bool active, qreal dxUm, qreal dyUm, qreal lenUm);

protected:
    void                        drawBackground(QPainter *painter, const QRectF &rect) override;
    void                        drawForeground(QPainter *painter, const QRectF &rect) override;
    bool                        event(QEvent *event) override;
    void                        wheelEvent(QWheelEvent *event) override;
    bool                        viewportEvent(QEvent *event) override;
    void                        keyPressEvent(QKeyEvent *event) override;
    void                        resizeEvent(QResizeEvent *event) override;
    void                        mousePressEvent(QMouseEvent *event) override;
    void                        mouseMoveEvent(QMouseEvent *event) override;
    void                        mouseReleaseEvent(QMouseEvent *event) override;
    void                        leaveEvent(QEvent *event) override;

private slots:
    /*! Field toolbutton toggled → \c setFieldMode. */
    void                        onFieldButtonToggled(bool on);
    /*! Log changed; re-exports the slice. */
    void                        onFieldControlsChanged();
    void                        onFieldTempToggled(bool on);
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
    void                        applyItemVisual(QGraphicsItem *item);
    bool                        rebuildIso3dForStyleChange();
    /*! Styling multiplier: layer fill opacity relative to \c defaultFillOpacity(). */
    qreal                       opacityFor(int gdsLayer) const;
    bool                        visibleFor(int gdsLayer) const;
    bool                        isPortLayerNumber(int gdsLayer) const;
    QString                     thermalMarkerToolTip(int gdsLayer, const QString &name) const;
    /*!*******************************************************************************************************************
     * \brief Adds the Field heatmap pixmap under layout polygons (scene Y-down).
     **********************************************************************************************************************/
    void                        addFieldOverlayItems();
    /*! Full-pane volume screenshot (Field+3D); scene = image pixel rectangle. */
    void                        addFieldVolumeItems();
    /*! Syncs Z slider / Log widgets from \c m_field without re-export. */
    void                        updateFieldControlsFromOverlay();
    /*! Shows/hides Field panel, updates status line, repositions floating controls. */
    void                        syncFloatingControls();
    /*! Emits \c fieldSliceRequest from current slider / checkbox state. */
    void                        emitFieldSliceRequest();
    /*! Volume camera zoom changed — re-render only after settle (wheel stop / pinch end). */
    void                        scheduleVolumeCameraRefresh();
    void                        wipeFieldSceneForModeSwitch(bool toVolume);
    /*! Apply a multiplicative camera zoom step (Field volume); schedules settle refresh. */
    void                        applyVolumeCamZoomFactor(qreal factor);
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
    /*! Zooms by \a factor keeping the scene point under \a viewPos fixed. */
    void                        zoomAt(qreal factor, const QPoint &viewPos);
    /*! Pans by a viewport pixel delta (content follows the mouse). */
    void                        panBy(int dx, int dy);
    /*! Iso3D view from \a yawDeg / \a pitchDeg (default: side view; switches to 3D if needed). */
    void                        setSideView(qreal yawDeg, qreal pitchDeg = 0.0);
    /*! Places a measure point (start, then end; a third point starts a new ruler). */
    void                        addMeasurePoint(const QPointF &scenePt);
    /*! Tooltip of one side of the 2D / 3D switch, built from \c NavigationStyle::bindingTable. */
    QString                     modeButtonToolTip(bool iso3d) const;
    void                        updateModeToolTips();
    /*! Handles the 2/3, Field, measure, Field-panel and copy keys; true if consumed. */
    bool                        handleViewKey(QKeyEvent *event);
    static QPointF              sceneToGdsUm(const QPointF &scenePt);
    void                        emitMeasure();
    /*! Bilinear sample of \c m_field.sampleGrid at GDS µm (Y-up). */
    bool                        sampleFieldAtGdsUm(qreal xUm, qreal yUm, qreal *valueOut) const;
    /*! Place / replace Field probe at scene point (Field 2D with samples only). */
    bool                        tryPlaceFieldProbe(const QPointF &scenePt);
    static void                 setPixelOffset(QGraphicsItem *item, qreal dxPx, qreal dyPx);

    void                        rebuildScene(bool refit = true);
    void                        applyViaMerge();
    void                        rebuildScene2D(bool refit);
    void                        rebuildScene3D(bool refit);
    /*! Coalesce Iso3D rebuilds while orbiting (mouse / wheel). */
    void                        scheduleOrbitRebuild();
    void                        flushOrbitRebuild();
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
    QWidget                    *m_modeSwitch = nullptr;  //!< [2D | 3D] segmented switch (Substrate page)
    class QToolButton          *m_mode2dBtn = nullptr;
    class QToolButton          *m_mode3dBtn = nullptr;
    class QToolButton          *m_field3dBtn = nullptr;  //!< "3D viewer ↗" (Fields page)
    class QToolButton          *m_fieldBtn = nullptr;
    class QWidget              *m_fieldPanel = nullptr;
    class QSlider              *m_fieldZSlider = nullptr;
    class QToolButton          *m_fieldHotZBtn = nullptr;
    class QCheckBox            *m_fieldLogChk = nullptr;
    class QCheckBox            *m_fieldTempChk = nullptr;
    class QLabel               *m_fieldStatusLbl = nullptr;
    class QComboBox            *m_fieldChoiceCombo = nullptr;
    class QTimer               *m_volumeCamSettle = nullptr; //!< Debounce volume zoom re-render
    class QTimer               *m_orbitRebuildTimer = nullptr; //!< Coalesce Iso3D orbit rebuilds
    Iso3dRebuildStats           m_lastIso3dStats;
    ViewMode                    m_viewMode = ViewMode::Top2D;
    bool                        m_fieldOn = false;
    FieldOverlay                m_field;
    bool                        m_blockFieldControls = false;

    QVector<GdsFlatPolygon>     m_polys;        //!< Drawn polygons: m_rawPolys with vias merged (applyViaMerge)
    QVector<GdsFlatPolygon>     m_rawPolys;     //!< Polygons as flattened from the GDS
    qreal                       m_viaMergeUm = -1.0; //!< merge_polygon_size of the model, < 0 = not set
    int                         m_maxViaPolygons = 100; //!< Per via layer, see setMaxViaPolygonsPerLayer
    class QLabel               *m_denseViaLabel = nullptr; //!< Shown instead of the layout over the limit
    QHash<int, LayerStyle>      m_styles;
    QHash<int, PortInfo>        m_ports;

    bool                        m_zoomLocked = false;
    QString                     m_highlightedName;
    QHash<int, bool>            m_layerVisible;  // missing => true
    QHash<int, qreal>           m_layerOpacity;  // 2D fill opacity; missing => defaultFillOpacity()

    bool                        m_cursorValid = false;
    bool                        m_showCoordinates = true;
    bool                        m_panning = false;
    bool                        m_orbiting = false;
    bool                        m_leftPressPending = false; //!< Left down; select on release if not dragged
    bool                        m_dollying = false;         //!< Right drag zoom (setupEM style)
    bool                        m_measureArmed = false;     //!< M pressed: left clicks measure
    NavStyle                    m_navStyle = NavStyle::EMStudio;
    class QTimer               *m_fieldZKeyTimer = nullptr; //!< Debounce PgUp/PgDn slice export
    QPoint                      m_panLast;
    QPoint                      m_pressPos;
    qreal                       m_yawDeg = 45.0;   //!< Orbit around stack Z [deg]
    qreal                       m_pitchDeg = 30.0; //!< Orbit pitch [deg], clamped
    qreal                       m_volumeCamZoom = 1.0; //!< Field+3D camera zoom (re-render, not pixmap scale)
    qreal                       m_orbitCx = 0.0;
    qreal                       m_orbitCy = 0.0;
    qreal                       m_orbitCz = 0.0;
    qreal                       m_orbitRadius = 1.0;   //!< Half 3D diagonal of the layout box [µm]
    QRectF                      m_iso3dContentRect;    //!< Projected Iso3D geometry (+ margin)
    qreal                       m_iso3dSceneHalf = 0.0; //!< Half size of the square Iso3D sceneRect
    QPointF                     m_cursorScene;
    QPoint                      m_cursorView;
    bool                        m_measureHasStart = false;
    bool                        m_measureHasEnd = false;
    QPointF                     m_measureStart;
    QPointF                     m_measureEnd;

    bool                        m_fieldProbeActive = false;
    QPointF                     m_fieldProbeScene;
    qreal                       m_fieldProbeValue = 0.0;

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
