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
 * - Escape clears the highlight and emits \c highlightCleared.
 * - Mouse wheel zooms; F / Home fits the view; pan via scroll-hand drag.
 *
 * Y coordinates from GDS (Y-up) are flipped for Qt display (Y-down) when polygons are added.
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
                                            const QHash<int, LayerStyle> &styles);
    void                        setHighlightedLayer(const QString &name);
    void                        clearHighlight();

signals:
    /*! Emitted when the user clicks a polygon; \a name / \a kind match stack item tagging. */
    void                        layerClicked(const QString &name, const QString &kind);
    /*! Emitted when Esc (or equivalent) clears the layout highlight. */
    void                        highlightCleared();

protected:
    void                        drawBackground(QPainter *painter, const QRectF &rect) override;
    void                        wheelEvent(QWheelEvent *event) override;
    void                        keyPressEvent(QKeyEvent *event) override;
    void                        resizeEvent(QResizeEvent *event) override;
    void                        mousePressEvent(QMouseEvent *event) override;

private:
    void                        applyHighlight();
    void                        setLayerHighlightVisual(const QString &name, bool on);
    void                        fitContent();

    QGraphicsScene             *m_scene = nullptr;
    bool                        m_zoomLocked = false;
    QString                     m_highlightedName;

    static constexpr int        kRoleName = 0;
    static constexpr int        kRoleKind = 1;
};

#endif // QT_VERSION

#endif // LAYOUTVIEW_H
