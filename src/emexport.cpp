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

#include "emexport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const QStringList kSides = {QStringLiteral("xmin"), QStringLiteral("xmax"), QStringLiteral("ymin"),
                            QStringLiteral("ymax"), QStringLiteral("zmin"), QStringLiteral("zmax")};

double grid(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}

/*! Contour on the 1 nm grid without repeated, closing or collinear points; empty if degenerate. */
QPolygonF cleaned(const QPolygonF &in)
{
    QPolygonF pts;
    for (const QPointF &p : in) {
        const QPointF q(grid(p.x()), grid(p.y()));
        if (pts.isEmpty() || pts.last() != q)
            pts << q;
    }
    if (pts.size() > 1 && pts.first() == pts.last())
        pts.removeLast();
    bool changed = true;
    while (changed && pts.size() >= 3) {
        changed = false;
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF &a = pts.at((i + pts.size() - 1) % pts.size());
            const QPointF &b = pts.at(i);
            const QPointF &c = pts.at((i + 1) % pts.size());
            const double cross = (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
            const double dot = (b.x() - a.x()) * (c.x() - b.x()) + (b.y() - a.y()) * (c.y() - b.y());
            if ((std::abs(cross) < 1e-9 && dot > 0.0) || b == c) {   // straight on; a spike stays
                pts.removeAt(i);
                changed = true;
                break;
            }
        }
    }
    if (pts.size() < 3)
        return QPolygonF();
    double area2 = 0.0;
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF &a = pts.at(i);
        const QPointF &b = pts.at((i + 1) % pts.size());
        area2 += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area2) < 1e-9 ? QPolygonF() : pts;
}

double signedArea(const QPolygonF &p)
{
    double a = 0.0;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF &u = p.at(i);
        const QPointF &v = p.at((i + 1) % p.size());
        a += u.x() * v.y() - v.x() * u.y();
    }
    return a / 2.0;
}

/*! Joins \a hole into \a outer with a cut line from the hole's leftmost vertex to the left (a keyhole,
 *  as KLayout writes polygons with holes to GDS). The hole runs the other way round than \a outer. */
void bridgeHole(QPolygonF *outer, QPolygonF hole)
{
    if ((signedArea(*outer) > 0) == (signedArea(hole) > 0))
        std::reverse(hole.begin(), hole.end());
    int hi = 0;
    for (int i = 1; i < hole.size(); ++i)
        if (hole.at(i).x() < hole.at(hi).x() || (hole.at(i).x() == hole.at(hi).x() && hole.at(i).y() < hole.at(hi).y()))
            hi = i;
    const QPointF h = hole.at(hi);

    int edge = -1;
    double bestX = -std::numeric_limits<double>::max();
    for (int i = 0; i < outer->size(); ++i) {
        const QPointF &a = outer->at(i);
        const QPointF &b = outer->at((i + 1) % outer->size());
        if (a.y() == b.y())
            continue;
        const bool spans = (a.y() <= h.y() && h.y() < b.y()) || (b.y() <= h.y() && h.y() < a.y());
        if (!spans)
            continue;
        const double x = a.x() + (h.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
        if (x <= h.x() && x > bestX) {
            bestX = x;
            edge = i;
        }
    }
    if (edge < 0)
        return;   // not inside: leave it out rather than draw a wrong polygon
    const QPointF q(bestX, h.y());
    QPolygonF joined;
    joined.reserve(outer->size() + hole.size() + 3);
    for (int i = 0; i <= edge; ++i)
        joined << outer->at(i);
    joined << q;
    for (int k = 0; k <= hole.size(); ++k)
        joined << hole.at((hi + k) % hole.size());
    joined << q;
    for (int i = edge + 1; i < outer->size(); ++i)
        joined << outer->at(i);
    *outer = joined;
}

/*! Adds the contours of \a path (odd-even) to \a out as polygons without holes (holes keyholed). */
void emitWithoutHoles(const QPainterPath &path, QVector<QPolygonF> *out)
{
    QVector<QPolygonF> contours;
    for (const QPolygonF &c : path.toSubpathPolygons()) {
        const QPolygonF clean = cleaned(c);
        if (!clean.isEmpty())
            contours << clean;
    }
    QVector<int> depth(contours.size(), 0);
    for (int i = 0; i < contours.size(); ++i)
        for (int j = 0; j < contours.size(); ++j)
            if (j != i && contours.at(j).containsPoint(contours.at(i).first(), Qt::OddEvenFill))
                ++depth[i];

    QVector<QVector<int>> holesOf(contours.size());
    for (int i = 0; i < contours.size(); ++i) {
        if (depth.at(i) % 2 == 0)
            continue;
        int parent = -1;
        for (int j = 0; j < contours.size(); ++j) {
            if (depth.at(j) != depth.at(i) - 1 || !contours.at(j).containsPoint(contours.at(i).first(), Qt::OddEvenFill))
                continue;
            if (parent < 0 || std::abs(signedArea(contours.at(j))) < std::abs(signedArea(contours.at(parent))))
                parent = j;
        }
        if (parent >= 0)
            holesOf[parent] << i;
    }
    for (int i = 0; i < contours.size(); ++i) {
        if (depth.at(i) % 2)
            continue;
        QPolygonF poly = contours.at(i);
        QVector<int> holes = holesOf.at(i);
        // Leftmost holes first: a cut to the left never crosses a hole that is not joined yet.
        std::sort(holes.begin(), holes.end(), [&contours](int a, int b) {
            return contours.at(a).boundingRect().left() < contours.at(b).boundingRect().left();
        });
        for (int h : holes)
            bridgeHole(&poly, contours.at(h));
        out->push_back(poly);
    }
}

QString directionLetter(const QString &d)
{
    return d.trimmed().toUpper();
}

/*! Side of \a region a zero-width port shape lies on, or "". */
QString wallSide(const QPolygonF &shape, const QRectF &region)
{
    const QRectF b = shape.boundingRect();
    const double eps = 1e-6;
    if (b.width() > eps && b.height() > eps)
        return QString();
    if (b.width() <= eps) {
        if (std::abs(b.left() - region.left()) < eps)
            return QStringLiteral("xmin");
        if (std::abs(b.left() - region.right()) < eps)
            return QStringLiteral("xmax");
    } else {
        if (std::abs(b.top() - region.top()) < eps)
            return QStringLiteral("ymin");
        if (std::abs(b.top() - region.bottom()) < eps)
            return QStringLiteral("ymax");
    }
    return QString();
}

bool writeJson(const QString &path, const QJsonObject &obj, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        *error = QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    return true;
}

QString forwardSlashes(const QString &path)
{
    return QFileInfo(path).absoluteFilePath().replace(QLatin1Char('\\'), QLatin1Char('/'));
}

} // namespace

QVector<QPolygonF> EmExport::clipAndMerge(const QVector<QPolygonF> &polys, const QRectF &clip, bool merge)
{
    QVector<QPolygonF> out;
    QPainterPath clipPath;
    if (!clip.isNull())
        clipPath.addRect(clip.normalized());

    auto closedPath = [](const QPolygonF &poly) {
        QPainterPath p;
        p.addPolygon(poly);
        p.closeSubpath();
        return p;
    };

    if (!merge) {
        for (const QPolygonF &poly : polys) {
            if (!clip.isNull() && !clip.intersects(poly.boundingRect()))
                continue;
            if (clip.isNull() || clip.contains(poly.boundingRect())) {
                const QPolygonF clean = cleaned(poly);
                if (!clean.isEmpty())
                    out << clean;
                continue;
            }
            emitWithoutHoles(closedPath(poly).intersected(clipPath), &out);
        }
        return out;
    }

    QPainterPath all;
    all.setFillRule(Qt::WindingFill);
    for (const QPolygonF &poly : polys) {
        if (!clip.isNull() && !clip.intersects(poly.boundingRect()))
            continue;
        all.addPolygon(poly);
        all.closeSubpath();
    }
    if (all.isEmpty())
        return out;
    QPainterPath merged = all.simplified();
    if (!clip.isNull())
        merged = merged.intersected(clipPath);
    emitWithoutHoles(merged, &out);
    return out;
}

QJsonObject EmExport::simcfgPort(int number, const EmMarkupPort &port)
{
    const bool via = port.isVia();
    QJsonObject o;
    o.insert(QStringLiteral("portnumber"), number);
    o.insert(QStringLiteral("source_layernum"), 200 + number);
    o.insert(QStringLiteral("target_layername"), via ? QJsonValue() : QJsonValue(port.target));
    o.insert(QStringLiteral("from_layername"), via ? QJsonValue(port.fromLayer) : QJsonValue());
    o.insert(QStringLiteral("to_layername"), via ? QJsonValue(port.toLayer) : QJsonValue());
    o.insert(QStringLiteral("direction"), directionLetter(port.direction));
    o.insert(QStringLiteral("port_Z0"), port.z0);
    o.insert(QStringLiteral("voltage"), port.voltage);
    return o;
}

EmExport::Result EmExport::run(const QVector<GdsFlatPolygon> &polys, const EmMarkup &markup, const Options &options)
{
    Result r;
    if (!options.outGds.endsWith(QStringLiteral(".gds"), Qt::CaseInsensitive)) {
        r.error = QStringLiteral("The EM layout must be a .gds file: %1").arg(options.outGds);
        return r;
    }
    const QString base = options.outGds.left(options.outGds.size() - 4);
    r.gds = options.outGds;
    r.simcfg = base + QStringLiteral(".simcfg");
    r.portmap = base + QStringLiteral("_portmap.json");
    r.cell = options.cell.trimmed().isEmpty() ? QStringLiteral("TOP") : options.cell.trimmed();
    const QRectF region = markup.hasRegion ? markup.regionUm.normalized() : QRectF();

    // Layout: stackup layers of the model's purposes, per (layer, datatype).
    QMap<QPair<int, int>, QVector<QPolygonF>> byKey;
    for (const GdsFlatPolygon &p : polys) {
        if (p.layer >= EmMarkup::kFirstPortLayer && p.layer <= EmMarkup::kLastPortLayer
                && !options.stackLayers.contains(p.layer))
            continue;   // the markup ports replace the layout's port shapes
        const QPair<int, int> key(p.layer, p.datatype);
        if (!options.stackLayers.contains(p.layer)
                || (!options.purposes.isEmpty() && !options.purposes.contains(p.datatype))) {
            ++r.dropped[key];
            continue;
        }
        byKey[key] << p.pointsUm;
    }

    QVector<GdsFlatPolygon> outPolys;
    for (auto it = byKey.cbegin(); it != byKey.cend(); ++it) {
        const QVector<QPolygonF> done = clipAndMerge(it.value(), region, options.merge);
        for (const QPolygonF &poly : done) {
            GdsFlatPolygon gp;
            gp.layer = it.key().first;
            gp.datatype = it.key().second;
            gp.pointsUm = poly;
            outPolys << gp;
        }
        if (!done.isEmpty())
            r.kept[it.key()] = done.size();
    }

    // Ports in the region, numbered 1..N in layout order.
    QVector<EmMarkupPort> ports = markup.ports;
    std::sort(ports.begin(), ports.end(), [](const EmMarkupPort &a, const EmMarkupPort &b) {
        return a.number < b.number;
    });
    QSet<QString> walls;
    for (const EmMarkupPort &port : qAsConst(ports)) {
        if (port.shapesUm.isEmpty()) {
            r.warnings << QStringLiteral("P%1 has no shape and is left out").arg(port.number);
            continue;
        }
        if (!region.isNull()) {
            const QRectF grown = region.adjusted(-1e-6, -1e-6, 1e-6, 1e-6);
            bool inside = true;
            for (const QPolygonF &s : port.shapesUm)
                inside = inside && grown.contains(s.boundingRect().center());
            if (!inside) {
                r.warnings << QStringLiteral("P%1 is outside the EM region and is left out").arg(port.number);
                continue;
            }
        }
        ExportedPort ep;
        ep.number = r.ports.size() + 1;
        ep.port = port;
        QStringList problems = port.problems(options.metals);
        ep.inSimcfg = problems.isEmpty();
        if (!ep.inSimcfg)
            r.warnings << QStringLiteral("P%1 is exported as shapes only: %2").arg(port.number).arg(problems.join(QStringLiteral("; ")));
        for (const QPolygonF &s : port.shapesUm) {
            GdsFlatPolygon gp;
            gp.layer = 200 + ep.number;
            gp.pointsUm = s;
            outPolys << gp;
            if (!region.isNull()) {
                const QString side = wallSide(s, region);
                if (!side.isEmpty())
                    walls.insert(side);
            }
        }
        r.ports << ep;
        if (200 + ep.number > EmMarkup::kLastPortLayer) {
            r.error = QStringLiteral("More than %1 ports").arg(EmMarkup::kLastPortLayer - 200);
            return r;
        }
    }
    for (const QString &side : kSides)
        if (walls.contains(side))
            r.portWalls << side;

    if (!GdsLayout::writeFlat(r.gds, r.cell, outPolys, &r.error))
        return r;

    // .simcfg: setupEM's own settings in an existing file are kept.
    QJsonObject simcfg;
    {
        QFile f(r.simcfg);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject() && doc.object().value(QStringLiteral("application")).toString() == QLatin1String("setupEM")
                    && doc.object().value(QStringLiteral("saved_values")).isObject()) {
                simcfg = doc.object();
                r.updatedSimcfg = true;
            }
        }
    }
    if (simcfg.isEmpty()) {
        simcfg.insert(QStringLiteral("application"), QStringLiteral("setupEM"));
        simcfg.insert(QStringLiteral("data_format"), QStringLiteral("1.0"));
        simcfg.insert(QStringLiteral("saved_values"), QJsonObject{{QStringLiteral("unit"), 1e-06}});
    }
    QJsonObject saved = simcfg.value(QStringLiteral("saved_values")).toObject();
    QJsonArray purpose;
    QList<int> purposes = options.purposes.values();
    std::sort(purposes.begin(), purposes.end());
    if (purposes.isEmpty())
        purposes << 0;
    for (int p : purposes)
        purpose.append(p);
    saved.insert(QStringLiteral("purpose"), purpose);
    saved.insert(QStringLiteral("GdsFile"), forwardSlashes(r.gds));
    if (!options.substrateFile.isEmpty())
        saved.insert(QStringLiteral("SubstrateFile"), forwardSlashes(options.substrateFile));
    saved.insert(QStringLiteral("cellname"), r.cell);
    if (!r.portWalls.isEmpty()) {
        QJsonArray boundary;
        const QJsonValue old = saved.value(QStringLiteral("boundary"));
        const QJsonArray oldArr = old.toArray();
        for (int i = 0; i < kSides.size(); ++i) {
            QString v = oldArr.size() == kSides.size() ? oldArr.at(i).toString()
                                                       : (old.isString() && !old.toString().isEmpty() ? old.toString()
                                                                                                      : QStringLiteral("ABC"));
            if (r.portWalls.contains(kSides.at(i)))
                v = QStringLiteral("PORTWALL");
            boundary.append(v);
        }
        saved.insert(QStringLiteral("boundary"), boundary);
    }
    simcfg.insert(QStringLiteral("saved_values"), saved);
    QJsonArray simPorts;
    for (const ExportedPort &ep : qAsConst(r.ports))
        if (ep.inSimcfg)
            simPorts.append(simcfgPort(ep.number, ep.port));
    simcfg.insert(QStringLiteral("ports"), simPorts);
    if (!writeJson(r.simcfg, simcfg, &r.error))
        return r;

    // Port map: exported number ↔ layout number, as klayout_em_export writes it.
    QJsonArray rows;
    for (const ExportedPort &ep : qAsConst(r.ports)) {
        QJsonObject row;
        row.insert(QStringLiteral("port"), ep.number);
        row.insert(QStringLiteral("layer"), 200 + ep.number);
        row.insert(QStringLiteral("layout_port"), ep.port.number);
        row.insert(QStringLiteral("layout_layer"), 200 + ep.port.number);
        row.insert(QStringLiteral("in_simcfg"), ep.inSimcfg);
        row.insert(QStringLiteral("settings"), ep.port.encode());
        row.insert(QStringLiteral("direction"), directionLetter(ep.port.direction));
        rows.append(row);
    }
    QJsonObject portmap;
    portmap.insert(QStringLiteral("note"), QStringLiteral(
        "port = number in the .simcfg and .snp, layer = its layer in the exported GDS; layout_port = number in "
        "the markup (layer - 200 in klayout_em_export). Ports not in the .simcfg are exported as shapes only."));
    portmap.insert(QStringLiteral("ports"), rows);
    portmap.insert(QStringLiteral("devices"), QJsonObject());
    portmap.insert(QStringLiteral("exported_by"), QStringLiteral("EMStudio"));
    if (!writeJson(r.portmap, portmap, &r.error))
        return r;

    r.ok = true;
    return r;
}
