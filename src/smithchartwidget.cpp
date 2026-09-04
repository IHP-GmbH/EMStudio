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

#include "smithchartwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QtMath>

namespace {

constexpr qreal kGridLw = 0.8;
const QColor kGridColor(211, 211, 211); // lightgrey
const QVector<double> kFullRValues = {0.2, 0.5, 1.0, 2.0, 5.0};
const QVector<double> kFullXValues = {0.2, 0.5, 1.0, 2.0, 5.0};
const QVector<double> kZoomGridValues = {0.2, 0.5, 1.0, 1.5, 2.0, 3.0};

} // namespace

SmithChartWidget::SmithChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(180, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void SmithChartWidget::setZoomed(bool zoomed)
{
    if (m_zoomed == zoomed)
        return;
    m_zoomed = zoomed;
    update();
}

void SmithChartWidget::setChartTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    update();
}

void SmithChartWidget::clearTraces()
{
    m_traces.clear();
    update();
}

void SmithChartWidget::addTrace(const QVector<std::complex<double>> &gamma,
                                const QColor &color,
                                Qt::PenStyle style,
                                const QString &label)
{
    Trace t;
    t.gamma = gamma;
    t.color = color;
    t.style = style;
    t.label = label;
    m_traces.append(t);
    update();
}

QSize SmithChartWidget::sizeHint() const
{
    return {280, 280};
}

QSize SmithChartWidget::minimumSizeHint() const
{
    return {140, 140};
}

QPointF SmithChartWidget::toPixel(const QPointF &gamma, const QRectF &plotRect) const
{
    const double lim = m_zoomed ? ZOOM_GAMMA : 1.0;
    const double xNorm = (gamma.x() + lim) / (2.0 * lim);
    const double yNorm = (lim - gamma.y()) / (2.0 * lim); // imag up
    return QPointF(plotRect.left() + xNorm * plotRect.width(),
                   plotRect.top() + yNorm * plotRect.height());
}

void SmithChartWidget::drawGrid(QPainter &p, const QRectF &plotRect) const
{
    const double lim = m_zoomed ? ZOOM_GAMMA : 1.0;
    auto circle = [&](double cx, double cy, double r) {
        const QPointF c = toPixel(QPointF(cx, cy), plotRect);
        const QPointF edge = toPixel(QPointF(cx + r, cy), plotRect);
        const qreal radiusPx = std::abs(edge.x() - c.x());
        p.drawEllipse(c, radiusPx, radiusPx);
    };

    p.setBrush(Qt::NoBrush);

    // Outer / clip boundary
    if (!m_zoomed) {
        p.setPen(QPen(QColor(80, 80, 80), 1.2));
        circle(0.0, 0.0, 1.0);
    }

    p.setPen(QPen(kGridColor, kGridLw));

    // Horizontal real axis
    p.drawLine(toPixel(QPointF(-lim, 0), plotRect), toPixel(QPointF(lim, 0), plotRect));
    if (m_zoomed) {
        p.setPen(QPen(QColor(128, 128, 128), 0.5));
        p.drawLine(toPixel(QPointF(-lim, 0), plotRect), toPixel(QPointF(lim, 0), plotRect));
        p.setPen(QPen(kGridColor, kGridLw));
    }

    const QVector<double> &rValues = m_zoomed ? kZoomGridValues : kFullRValues;
    const QVector<double> &xValues = m_zoomed ? kZoomGridValues : kFullXValues;

    // Constant-r circles: center (r/(1+r), 0), radius 1/(1+r)
    for (double r : rValues) {
        const double center = r / (1.0 + r);
        const double radius = 1.0 / (1.0 + r);
        circle(center, 0.0, radius);
    }

    // Constant-x circles: center (1, 1/x), radius |1/x|
    for (double x : xValues) {
        for (double sign : {1.0, -1.0}) {
            const double xv = sign * x;
            const double cy = 1.0 / xv;
            const double radius = std::abs(1.0 / xv);
            circle(1.0, cy, radius);
        }
    }

    if (!m_zoomed) {
        // Unit circle again on top of grid
        p.setPen(QPen(QColor(80, 80, 80), 1.2));
        circle(0.0, 0.0, 1.0);
    }

    // Clip frame for zoomed view
    if (m_zoomed) {
        p.setPen(QPen(QColor(80, 80, 80), 1.0));
        p.drawRect(plotRect);
    }
}

void SmithChartWidget::drawTraces(QPainter &p, const QRectF &plotRect) const
{
    for (const Trace &t : m_traces) {
        if (t.gamma.isEmpty())
            continue;

        p.setPen(QPen(t.color, 1.6, t.style));
        p.setBrush(t.color);

        if (t.gamma.size() == 1) {
            const QPointF pt = toPixel(QPointF(t.gamma.first().real(), t.gamma.first().imag()),
                                       plotRect);
            p.drawEllipse(pt, 4.5, 4.5);
            continue;
        }

        QPainterPath path;
        bool started = false;
        for (const auto &g : t.gamma) {
            const QPointF pt = toPixel(QPointF(g.real(), g.imag()), plotRect);
            if (!started) {
                path.moveTo(pt);
                started = true;
            } else {
                path.lineTo(pt);
            }
        }
        p.drawPath(path);
    }
}

void SmithChartWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().window());

    const int titleH = m_title.isEmpty() ? 0 : 22;
    QRectF area = rect().adjusted(8, 8 + titleH, -8, -8);

    // Equal aspect square plot region
    const qreal side = qMin(area.width(), area.height());
    QRectF plotRect(area.center().x() - side / 2.0,
                    area.center().y() - side / 2.0,
                    side, side);

    if (!m_title.isEmpty()) {
        p.setPen(palette().windowText().color());
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(rect().left(), 4, rect().width(), titleH),
                   Qt::AlignHCenter | Qt::AlignVCenter, m_title);
    }

    // Clip traces to unit/zoom disc (and square for zoom)
    p.save();
    if (m_zoomed) {
        p.setClipRect(plotRect);
    } else {
        QPainterPath clip;
        clip.addEllipse(plotRect);
        p.setClipPath(clip);
    }
    drawGrid(p, plotRect);
    drawTraces(p, plotRect);
    p.restore();

    // Redraw border without clip so edge is crisp
    if (!m_zoomed) {
        p.setPen(QPen(QColor(80, 80, 80), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(plotRect);
    } else {
        p.setPen(QPen(QColor(80, 80, 80), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(plotRect);
    }
}
