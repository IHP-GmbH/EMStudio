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

#ifndef SUBSTRATEVIEW_H
#define SUBSTRATEVIEW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPainter>

#include "substrate.h"

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \class SubstrateView
 * \brief A QGraphicsView-based widget for 3D-style visualization of substrate stackups.
 *
 * This class displays the dielectric, metal, and via layers of a semiconductor substrate in a pseudo-3D format.
 * It supports zooming (mouse wheel and pinch gestures), keyboard interaction, and automatic scaling on resize.
 *
 * The visualization is based on parsed data from a `Substrate` object, and uses a layered color-coded view
 * with labels and optional real-to-visual thickness mapping.
 *
 * Key Features:
 * - Scroll-zoom and pinch gesture support with zoom limits
 * - Smart fitting of the entire stack to the view
 * - Visual distinction between dielectrics, conductors, and vias
 * - Optional labels for dielectric names, heights, and boundaries
 *
 * \see Substrate, QGraphicsView
 **********************************************************************************************************************/
class SubstrateView : public QGraphicsView
{
    Q_OBJECT

private:
    struct VisualThicknessPolicy {
        double minFactor = 0.9;
        double maxFactor = 1.6;
        double gamma     = 0.55;
    };

    struct VisualLayer {
        QString name;
        QString type;
        double realZMin;
        double realZMax;
        double zminPx;
        double zmaxPx;
        QColor color;
    };

public:
    explicit SubstrateView(QWidget* parent = nullptr);

    void                        setSubstrate(const Substrate& substrate);

protected:
    void                        drawBackground(QPainter* painter, const QRectF& rect) override;
    bool                        viewportEvent(QEvent* event) override;
    void                        wheelEvent(QWheelEvent* event) override;
    void                        keyPressEvent(QKeyEvent* event) override;
    void                        resizeEvent(QResizeEvent* event) override;

private:
    static double               median(QVector<double> v);
    static double               trimmedMedian(QVector<double> v, double trim = 0.10);
    double                      mapThicknessUmToVisualUm(double t, double tMed) const;

    void                        resetZoom();
    void                        drawSubstrate();
    double                      standardThickness() const;
    int                         countLayersInDielectric(const QString& dielectricName,
                                                        const QMap<QString, QList<VisualLayer>>& dielectricMap) const;
    int                         getLayerPositionInDielectric(const QString& layerName,
                                                             const QMap<QString, QList<VisualLayer>>& dielectricMap) const;
    VisualLayer                 findEnclosingDielectric(const QString& layerName,
                                                        const QMap<QString, QList<VisualLayer>>& dielectricMap,
                                                        const QList<VisualLayer>& allLayers) const;

private:
    bool                        m_zoomLocked;
    double                      m_currentZoom;
    QGraphicsScene*             m_scene;
    Substrate                   m_substrate;
    VisualThicknessPolicy       m_visPolicy;
};

#endif // QT_VERSION >= 5.0.0

#endif // SUBSTRATEVIEW_H
