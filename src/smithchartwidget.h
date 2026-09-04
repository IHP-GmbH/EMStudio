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

#ifndef SMITHCHARTWIDGET_H
#define SMITHCHARTWIDGET_H

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

#include <complex>

/*!*******************************************************************************************************************
 * \class SmithChartWidget
 * \brief Paints a Smith chart (full or zoomed) with reflection-coefficient traces.
 **********************************************************************************************************************/
class SmithChartWidget : public QWidget
{
    Q_OBJECT

public:
    static constexpr double ZOOM_GAMMA = 0.2;

    explicit SmithChartWidget(QWidget *parent = nullptr);

    void setZoomed(bool zoomed);
    bool isZoomed() const { return m_zoomed; }

    void setChartTitle(const QString &title);
    void clearTraces();
    void addTrace(const QVector<std::complex<double>> &gamma,
                  const QColor &color,
                  Qt::PenStyle style,
                  const QString &label);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Trace {
        QVector<std::complex<double>> gamma;
        QColor color;
        Qt::PenStyle style;
        QString label;
    };

    QPointF toPixel(const QPointF &gamma, const QRectF &plotRect) const;
    void drawGrid(QPainter &p, const QRectF &plotRect) const;
    void drawTraces(QPainter &p, const QRectF &plotRect) const;

    bool m_zoomed = false;
    QString m_title;
    QVector<Trace> m_traces;
};

#endif // SMITHCHARTWIDGET_H
