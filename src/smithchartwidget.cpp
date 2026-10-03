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

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
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
const QColor kLabelColor(105, 105, 105);  // dimgrey
constexpr qreal kPickRadiusPx = 25.0;     // a click this close to a trace sample places the marker

} // namespace

SmithChartWidget::SmithChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(180, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(tr("Click a trace for a marker · ←/→ step (Shift: 10 points) · Esc clears"));
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
    m_markerTrace = m_markerIndex = -1;
    update();
}

void SmithChartWidget::addTrace(const QVector<std::complex<double>> &gamma,
                                const QColor &color,
                                Qt::PenStyle style,
                                const QString &label,
                                const QVector<double> &freqHz,
                                double z0)
{
    Trace t;
    t.gamma = gamma;
    t.color = color;
    t.style = style;
    t.label = label;
    if (freqHz.size() == gamma.size())
        t.freqHz = freqHz;
    t.z0 = (z0 > 0.0) ? z0 : 50.0;
    m_traces.append(t);
    update();
}

void SmithChartWidget::setMarker(int trace, int index)
{
    if (trace < 0 || trace >= m_traces.size() || m_traces.at(trace).gamma.isEmpty()) {
        m_markerTrace = m_markerIndex = -1;
    } else {
        m_markerTrace = trace;
        m_markerIndex = qBound(0, index, m_traces.at(trace).gamma.size() - 1);
    }
    update();
}

QStringList SmithChartWidget::markerReadout() const
{
    if (m_markerTrace < 0 || m_markerTrace >= m_traces.size())
        return {};
    const Trace &t = m_traces.at(m_markerTrace);
    if (m_markerIndex < 0 || m_markerIndex >= t.gamma.size())
        return {};
    const std::complex<double> g = t.gamma.at(m_markerIndex);

    QStringList lines;
    if (!t.label.isEmpty())
        lines << t.label;
    lines << (t.freqHz.isEmpty()
                  ? tr("Point %1 of %2").arg(m_markerIndex + 1).arg(t.gamma.size())
                  : tr("f = %1 GHz").arg(QString::number(t.freqHz.at(m_markerIndex) / 1e9, 'g', 6)));
    lines << tr("|Γ| = %1   ∠ %2°")
                 .arg(QString::number(std::abs(g), 'f', 4))
                 .arg(QString::number(qRadiansToDegrees(std::arg(g)), 'f', 1));
    const std::complex<double> den = 1.0 - g;
    if (std::abs(den) < 1e-12) {
        lines << tr("Z = ∞ (open)");
    } else {
        const std::complex<double> z = t.z0 * (1.0 + g) / den;
        lines << tr("Z = %1 %2 j%3 Ω")
                     .arg(QString::number(z.real(), 'f', 2))
                     .arg(z.imag() < 0 ? QStringLiteral("−") : QStringLiteral("+"))
                     .arg(QString::number(std::abs(z.imag()), 'f', 2));
    }
    return lines;
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

        if (t.gamma.size() == 1) {
            // Marker dot: fill the small ellipse only.
            p.setBrush(t.color);
            const QPointF pt = toPixel(QPointF(t.gamma.first().real(), t.gamma.first().imag()),
                                       plotRect);
            p.drawEllipse(pt, 4.5, 4.5);
            continue;
        }

        // Stroke only — a brush would fill the open path as a closed polygon
        // (GitHub #24: filled wedge instead of an Snn trajectory line).
        p.setBrush(Qt::NoBrush);
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
    const QRectF plotRect = plotRectForSize();

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
    // White chart area (the circle, or the square when zoomed) on the window background.
    p.fillRect(plotRect, Qt::white);
    drawGrid(p, plotRect);
    drawGridLabels(p, plotRect);
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

    drawMarker(p, plotRect);
}

/*! Square chart area below the title (the Results viewer shows the legend for all charts). */
QRectF SmithChartWidget::plotRectForSize() const
{
    const int titleH = m_title.isEmpty() ? 0 : 22;
    const QRectF area = QRectF(rect()).adjusted(8, 8 + titleH, -8, -8);
    const qreal side = qMax<qreal>(10.0, qMin(area.width(), area.height()));
    return QRectF(area.center().x() - side / 2.0, area.center().y() - side / 2.0, side, side);
}

/*! r values on the real axis and ±xj where the reactance arcs cross the imaginary axis (as setupEM). */
void SmithChartWidget::drawGridLabels(QPainter &p, const QRectF &plotRect) const
{
    const double lim = m_zoomed ? ZOOM_GAMMA : 1.0;
    const QVector<double> &rValues = m_zoomed ? kZoomGridValues : kFullRValues;
    const QVector<double> &xValues = m_zoomed ? kZoomGridValues : kFullXValues;

    QFont f = font();
    f.setPointSizeF(7.5);
    p.setFont(f);
    p.setPen(kLabelColor);
    const QFontMetricsF fm(f);

    for (double r : rValues) {
        const double pos = (r - 1.0) / (r + 1.0);   // left edge of the r circle on the real axis
        if (std::abs(pos) >= lim)
            continue;
        const QPointF at = toPixel(QPointF(pos, 0.0), plotRect);
        const QString text = QString::number(r, 'g', 3);
        p.drawText(QPointF(at.x() - fm.horizontalAdvance(text) / 2.0, at.y() - 3.0), text);
    }
    for (double x : xValues) {
        for (double sign : {1.0, -1.0}) {
            const double xv = sign * x;
            const double radius = std::abs(1.0 / xv);
            if (radius < 1.0)
                continue;   // the arc doesn't reach the imaginary axis
            const double y0 = 1.0 / xv - std::copysign(std::sqrt(radius * radius - 1.0), 1.0 / xv);
            if (std::abs(y0) >= lim)
                continue;
            const QPointF at = toPixel(QPointF(0.0, y0), plotRect);
            const QString text = QString::number(xv, 'g', 3) + QLatin1Char('j');
            p.drawText(QPointF(at.x() + 3.0, at.y() + fm.ascent() / 2.0 - 1.0), text);
        }
    }
}

/*! Marker dot on the chosen sample, plus the readout box: left of the chart when there is room
 *  (wide charts), else in its top-left corner. */
void SmithChartWidget::drawMarker(QPainter &p, const QRectF &plotRect) const
{
    const QStringList lines = markerReadout();
    if (lines.isEmpty())
        return;
    const Trace &t = m_traces.at(m_markerTrace);
    const std::complex<double> g = t.gamma.at(m_markerIndex);
    const QPointF at = toPixel(QPointF(g.real(), g.imag()), plotRect);
    p.setPen(QPen(Qt::black, 1.5));
    p.setBrush(t.color);
    p.drawEllipse(at, 5.5, 5.5);
    p.setBrush(Qt::NoBrush);
    p.drawLine(at + QPointF(-9, 0), at + QPointF(-6, 0));
    p.drawLine(at + QPointF(6, 0), at + QPointF(9, 0));

    p.setFont(font());
    const QFontMetricsF fm(font());
    qreal w = 0.0;
    for (const QString &l : lines)
        w = qMax(w, fm.horizontalAdvance(l));
    const qreal lineH = fm.height();
    QRectF box(4, plotRect.top(), w + 12, lines.size() * lineH + 8);
    if (plotRect.left() - 8 - box.width() >= 4)
        box.moveRight(plotRect.left() - 8);
    p.setPen(QPen(QColor(120, 120, 120), 1.0));
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(palette().windowText().color());
    for (int i = 0; i < lines.size(); ++i)
        p.drawText(QPointF(box.left() + 6, box.top() + 4 + fm.ascent() + i * lineH), lines.at(i));
    p.setBrush(Qt::NoBrush);
}

void SmithChartWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRectF plotRect = plotRectForSize();
    int bestTrace = -1;
    int bestIndex = -1;
    qreal best = kPickRadiusPx;
    for (int ti = 0; ti < m_traces.size(); ++ti) {
        const QVector<std::complex<double>> &gamma = m_traces.at(ti).gamma;
        for (int i = 0; i < gamma.size(); ++i) {
            const QPointF pt = toPixel(QPointF(gamma.at(i).real(), gamma.at(i).imag()), plotRect);
            const qreal d = std::hypot(pt.x() - event->pos().x(), pt.y() - event->pos().y());
            if (d < best) {
                best = d;
                bestTrace = ti;
                bestIndex = i;
            }
        }
    }
    setMarker(bestTrace, bestIndex);   // a click away from every trace clears the marker
    event->accept();
}

void SmithChartWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_markerTrace >= 0 && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        setMarker(m_markerTrace, m_markerIndex + (event->key() == Qt::Key_Right ? step : -step));
        event->accept();
        return;
    }
    if (m_markerTrace >= 0 && event->key() == Qt::Key_Escape) {
        setMarker(-1, -1);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
