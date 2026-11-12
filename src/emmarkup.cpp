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

#include "emmarkup.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace {

const QString kPortPrefix = QStringLiteral("port:");

QString numberText(double v)
{
    return QString::number(v, 'g', 6);
}

QJsonArray polygonToJson(const QPolygonF &poly)
{
    QJsonArray pts;
    for (const QPointF &p : poly)
        pts.append(QJsonArray{p.x(), p.y()});
    return pts;
}

QPolygonF polygonFromJson(const QJsonArray &pts)
{
    QPolygonF poly;
    for (const QJsonValue &v : pts) {
        const QJsonArray xy = v.toArray();
        if (xy.size() >= 2)
            poly << QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
    }
    return poly;
}

bool sameSettings(const EmMarkupPort &a, const EmMarkupPort &b)
{
    return a.encode() == b.encode();
}

} // namespace

const QStringList &EmMarkupPort::directions()
{
    static const QStringList dirs = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
                                     QStringLiteral("-X"), QStringLiteral("-Y"), QStringLiteral("-Z")};
    return dirs;
}

QString EmMarkupPort::encode() const
{
    const QString layers = isVia() ? QStringLiteral("from=%1;to=%2").arg(fromLayer, toLayer)
                                   : QStringLiteral("target=%1").arg(target);
    return QStringLiteral("%1dir=%2;%3;z0=%4;v=%5")
            .arg(kPortPrefix, direction, layers, numberText(z0), numberText(voltage));
}

bool EmMarkupPort::decode(const QString &text, EmMarkupPort *port)
{
    if (!port || !text.startsWith(kPortPrefix))
        return false;
    QHash<QString, QString> values;
    for (const QString &item : text.mid(kPortPrefix.size()).split(QLatin1Char(';'))) {
        const int eq = item.indexOf(QLatin1Char('='));
        if (eq > 0)
            values.insert(item.left(eq).trimmed(), item.mid(eq + 1).trimmed());
    }
    bool okZ0 = true, okV = true;
    const double z0 = values.contains(QStringLiteral("z0")) ? values.value(QStringLiteral("z0")).toDouble(&okZ0) : 50.0;
    const double v = values.contains(QStringLiteral("v")) ? values.value(QStringLiteral("v")).toDouble(&okV) : 1.0;
    if (!okZ0 || !okV)
        return false;
    port->direction = values.value(QStringLiteral("dir"));
    port->fromLayer = values.value(QStringLiteral("from"));
    port->toLayer = values.value(QStringLiteral("to"));
    port->target = values.value(QStringLiteral("target"));
    port->z0 = z0;
    port->voltage = v;
    return true;
}

QStringList EmMarkupPort::problems(const QStringList &metals) const
{
    QStringList out;
    if (!directions().contains(direction))
        out << QStringLiteral("direction '%1' is not one of %2").arg(direction, directions().join(QStringLiteral(", ")));
    const QStringList names = isVia() ? QStringList{fromLayer, toLayer} : QStringList{target};
    for (const QString &name : names) {
        if (name.isEmpty())
            out << QStringLiteral("a layer is not set");
        else if (!metals.isEmpty() && !metals.contains(name))
            out << QStringLiteral("layer '%1' is not a metal of the stackup").arg(name);
    }
    if (isVia() && !fromLayer.isEmpty() && fromLayer == toLayer)
        out << QStringLiteral("from and to are the same layer");
    if (z0 <= 0.0)
        out << QStringLiteral("Z0 must be positive");
    if (shapesUm.isEmpty())
        out << QStringLiteral("no shape in the layout");
    return out;
}

QString EmMarkupPort::summary() const
{
    const QString layers = isVia() ? QStringLiteral("%1 \u2192 %2").arg(fromLayer, toLayer)
                                   : QStringLiteral("on %1").arg(target);
    QString s = QStringLiteral("P%1  %2  %3, %4 \u03a9").arg(number).arg(direction, layers, numberText(z0));
    if (voltage == 0.0)
        s += QStringLiteral(", not excited");
    return s;
}

bool EmMarkup::operator==(const EmMarkup &o) const
{
    if (hasRegion != o.hasRegion || (hasRegion && regionUm != o.regionUm) || ports.size() != o.ports.size())
        return false;
    for (int i = 0; i < ports.size(); ++i) {
        const EmMarkupPort &a = ports.at(i);
        const EmMarkupPort &b = o.ports.at(i);
        if (a.number != b.number || !sameSettings(a, b) || a.shapesUm != b.shapesUm)
            return false;
    }
    return layoutFile == o.layoutFile && cell == o.cell;
}

const EmMarkupPort *EmMarkup::port(int number) const
{
    for (const EmMarkupPort &p : ports)
        if (p.number == number)
            return &p;
    return nullptr;
}

int EmMarkup::nextFreePortNumber() const
{
    for (int n = 1; n <= kLastPortLayer - 200; ++n)
        if (!port(n))
            return n;
    return 0;
}

QStringList EmMarkup::problems(const QStringList &metals) const
{
    QStringList out;
    QSet<int> seen;
    for (const EmMarkupPort &p : ports) {
        if (p.number < 1 || p.number > kLastPortLayer - 200)
            out << QStringLiteral("P%1: port number must be 1..%2").arg(p.number).arg(kLastPortLayer - 200);
        if (seen.contains(p.number))
            out << QStringLiteral("P%1: number used twice").arg(p.number);
        seen.insert(p.number);
        for (const QString &problem : p.problems(metals))
            out << QStringLiteral("P%1: %2").arg(p.number).arg(problem);
        if (hasRegion) {
            const QRectF grown = regionUm.adjusted(-1e-6, -1e-6, 1e-6, 1e-6);
            for (const QPolygonF &shape : p.shapesUm) {
                if (!grown.contains(shape.boundingRect().center())) {
                    out << QStringLiteral("P%1: outside the EM region").arg(p.number);
                    break;
                }
            }
        }
    }
    if (hasRegion && (regionUm.width() <= 0.0 || regionUm.height() <= 0.0))
        out << QStringLiteral("the EM region is empty");
    return out;
}

QJsonObject EmMarkup::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("format"), QStringLiteral("emstudio-markup"));
    obj.insert(QStringLiteral("version"), 1);
    if (!layoutFile.isEmpty())
        obj.insert(QStringLiteral("layout"), layoutFile);
    if (!cell.isEmpty())
        obj.insert(QStringLiteral("cell"), cell);
    if (hasRegion) {
        obj.insert(QStringLiteral("region"), QJsonArray{regionUm.left(), regionUm.top(),
                                                        regionUm.right(), regionUm.bottom()});
    }
    QJsonArray portArr;
    for (const EmMarkupPort &p : ports) {
        QJsonObject po;
        po.insert(QStringLiteral("number"), p.number);
        po.insert(QStringLiteral("settings"), p.encode());
        QJsonArray shapes;
        for (const QPolygonF &s : p.shapesUm)
            shapes.append(polygonToJson(s));
        po.insert(QStringLiteral("shapes"), shapes);
        portArr.append(po);
    }
    obj.insert(QStringLiteral("ports"), portArr);
    return obj;
}

EmMarkup EmMarkup::fromJson(const QJsonObject &obj)
{
    EmMarkup m;
    m.layoutFile = obj.value(QStringLiteral("layout")).toString();
    m.cell = obj.value(QStringLiteral("cell")).toString();
    const QJsonArray region = obj.value(QStringLiteral("region")).toArray();
    if (region.size() == 4) {
        m.hasRegion = true;
        m.regionUm = QRectF(QPointF(region.at(0).toDouble(), region.at(1).toDouble()),
                            QPointF(region.at(2).toDouble(), region.at(3).toDouble())).normalized();
    }
    for (const QJsonValue &v : obj.value(QStringLiteral("ports")).toArray()) {
        const QJsonObject po = v.toObject();
        EmMarkupPort p;
        p.number = po.value(QStringLiteral("number")).toInt();
        EmMarkupPort::decode(po.value(QStringLiteral("settings")).toString(), &p);
        for (const QJsonValue &s : po.value(QStringLiteral("shapes")).toArray()) {
            const QPolygonF poly = polygonFromJson(s.toArray());
            if (poly.size() >= 2)
                p.shapesUm << poly;
        }
        m.ports << p;
    }
    return m;
}

bool EmMarkup::save(const QString &path, QString *errorMsg) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (errorMsg)
            *errorMsg = QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (errorMsg)
            *errorMsg = QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    return true;
}

bool EmMarkup::load(const QString &path, EmMarkup *out, QString *errorMsg)
{
    if (!out)
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorMsg)
            *errorMsg = QStringLiteral("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull() || !doc.isObject()) {
        if (errorMsg)
            *errorMsg = QStringLiteral("%1 is not a markup file: %2").arg(QDir::toNativeSeparators(path), err.errorString());
        return false;
    }
    *out = fromJson(doc.object());
    return true;
}

QString EmMarkup::sidecarPath(const QString &modelPath)
{
    if (modelPath.trimmed().isEmpty())
        return QString();
    const QFileInfo fi(modelPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".markup.json"));
}

bool EmMarkup::layoutHasMarkup(const QVector<GdsFlatPolygon> &polys)
{
    for (const GdsFlatPolygon &p : polys) {
        if (p.layer == kRegionLayer && p.datatype == kRegionDatatype)
            return true;
        if (p.layer >= kFirstPortLayer && p.layer <= kLastPortLayer && p.datatype == 0
                && p.properties.value(kPortPropertyKey).startsWith(kPortPrefix))
            return true;
    }
    return false;
}

EmMarkup EmMarkup::fromLayout(const QVector<GdsFlatPolygon> &polys, QStringList *warnings)
{
    EmMarkup m;
    QMap<int, EmMarkupPort> byNumber;
    QMap<int, bool> conflicts;
    int regionBoxes = 0;
    for (const GdsFlatPolygon &p : polys) {
        if (p.layer == kRegionLayer && p.datatype == kRegionDatatype && p.pointsUm.size() >= 3) {
            const QRectF box = p.pointsUm.boundingRect();
            m.regionUm = m.hasRegion ? m.regionUm.united(box) : box;
            m.hasRegion = true;
            ++regionBoxes;
            continue;
        }
        if (p.layer < kFirstPortLayer || p.layer > kLastPortLayer || p.datatype != 0)
            continue;
        EmMarkupPort settings;
        if (!EmMarkupPort::decode(p.properties.value(kPortPropertyKey), &settings))
            continue;
        const int number = p.layer - 200;
        auto it = byNumber.find(number);
        if (it == byNumber.end()) {
            settings.number = number;
            it = byNumber.insert(number, settings);
        } else if (!sameSettings(it.value(), settings)) {
            conflicts.insert(number, true);
        }
        it->shapesUm << p.pointsUm;
    }
    m.ports = byNumber.values().toVector();
    if (warnings) {
        for (int n : conflicts.keys())
            *warnings << QStringLiteral("P%1: shapes carry different settings, the first one is used").arg(n);
        if (regionBoxes > 1)
            *warnings << QStringLiteral("%1 EM region boxes on %2/%3: their bounding box is used (split regions come later)")
                         .arg(regionBoxes).arg(kRegionLayer).arg(kRegionDatatype);
    }
    return m;
}
