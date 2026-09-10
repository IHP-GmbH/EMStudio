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

#include "gdslayout.h"

#include <QtMath>
#include <QFile>
#include <QHash>
#include <QDataStream>
#include <QByteArray>
#include <cmath>

namespace {

/*!*******************************************************************************************************************
 * \struct Aff2
 * \brief 2D affine transform (linear 2×2 + translation) used while flattening hierarchy.
 *
 * Maps a point (x, y) as: (a00·x + a01·y + tx, a10·x + a11·y + ty).
 **********************************************************************************************************************/
struct Aff2
{
    double a00 = 1, a01 = 0, a10 = 0, a11 = 1;
    double tx = 0, ty = 0;

    /*! \brief Returns the identity transform. */
    static Aff2 identity() { return {}; }

    /*! \brief Applies this transform to a point in database units. */
    QPointF map(double x, double y) const
    {
        return QPointF(a00 * x + a01 * y + tx, a10 * x + a11 * y + ty);
    }

    /*! \brief Returns this ∘ \a inner (apply \a inner first, then this). */
    Aff2 composedWith(const Aff2 &inner) const
    {
        // this ∘ inner
        Aff2 o;
        o.a00 = a00 * inner.a00 + a01 * inner.a10;
        o.a01 = a00 * inner.a01 + a01 * inner.a11;
        o.a10 = a10 * inner.a00 + a11 * inner.a10;
        o.a11 = a10 * inner.a01 + a11 * inner.a11;
        o.tx  = a00 * inner.tx + a01 * inner.ty + tx;
        o.ty  = a10 * inner.tx + a11 * inner.ty + ty;
        return o;
    }
};

/*!*******************************************************************************************************************
 * \brief Builds an Aff2 from GDS STRANS / MAG / ANGLE plus translation.
 *
 * Transform order per GDSII: magnify → reflect about X → rotate CCW → translate.
 *
 * \param reflectX  STRANS reflection flag.
 * \param mag       Magnification (≤0 treated as 1).
 * \param angleDeg  Rotation in degrees (counter-clockwise).
 * \param tx,ty     Translation in database units.
 **********************************************************************************************************************/
Aff2 fromStrans(bool reflectX, double mag, double angleDeg, double tx, double ty)
{
    // GDS order: magnify → reflect about X → rotate CCW → translate
    const double m = (mag > 0.0) ? mag : 1.0;
    const double rad = qDegreesToRadians(angleDeg);
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double r00 = reflectX ? -1.0 : 1.0;

    // After reflect+scale: [r00*m, 0; 0, m], then rotate
    Aff2 t;
    t.a00 =  c * r00 * m;
    t.a01 = -s * m;
    t.a10 =  s * r00 * m;
    t.a11 =  c * m;
    t.tx = tx;
    t.ty = ty;
    return t;
}

/*!*******************************************************************************************************************
 * \brief Decodes an 8-byte GDSII REAL8 floating-point value.
 *
 * \param b Pointer to eight big-endian bytes (excess-64 base-16 exponent + mantissa).
 * \return  Decoded double, or 0 if all bytes are zero.
 **********************************************************************************************************************/
double decodeGdsReal8(const quint8 *b)
{
    bool zero = true;
    for (int i = 0; i < 8; ++i) {
        if (b[i]) { zero = false; break; }
    }
    if (zero)
        return 0.0;

    const int sign = (b[0] & 0x80) ? -1 : 1;
    const int exp16 = int(b[0] & 0x7F) - 64;
    double mant = 0.0;
    for (int i = 1; i < 8; ++i)
        mant = mant * 256.0 + double(b[i]);
    mant /= 72057594037927936.0; // 2^56
    return double(sign) * mant * std::pow(16.0, exp16);
}

/*!*******************************************************************************************************************
 * \struct RawBoundary
 * \brief BOUNDARY/BOX/PATH outline stored in database units before flattening.
 **********************************************************************************************************************/
struct RawBoundary
{
    int layer = 0;
    int datatype = 0;
    QVector<QPointF> xy; // DBU
};

/*!*******************************************************************************************************************
 * \struct RawSref
 * \brief Structure reference (SREF) with optional STRANS placement.
 **********************************************************************************************************************/
struct RawSref
{
    QString name;
    bool reflect = false;
    double mag = 1.0;
    double angle = 0.0;
    double tx = 0, ty = 0;
};

/*!*******************************************************************************************************************
 * \struct RawAref
 * \brief Array reference (AREF): COLROW grid plus three XY corner points.
 **********************************************************************************************************************/
struct RawAref
{
    QString name;
    bool reflect = false;
    double mag = 1.0;
    double angle = 0.0;
    int cols = 1, rows = 1;
    double x0 = 0, y0 = 0, xCol = 0, yCol = 0, xRow = 0, yRow = 0;
};

/*!*******************************************************************************************************************
 * \struct RawCell
 * \brief One GDS structure: local geometry plus child SREF/AREF instances.
 **********************************************************************************************************************/
struct RawCell
{
    QString name;
    QVector<RawBoundary> boundaries;
    QVector<RawSref> srefs;
    QVector<RawAref> arefs;
};

/*! Element currently being accumulated between BOUNDARY/PATH/… and ENDEL. */
enum class ElemKind { None, Boundary, Path, Sref, Aref, Box, Other };

/*!*******************************************************************************************************************
 * \brief Reads an ASCII string record payload into \a out (NUL-trimmed).
 *
 * \param stream    Big-endian data stream positioned at the record data.
 * \param dataSize  Number of data bytes in the record.
 * \param out       Destination string.
 * \return          False if the raw read fails.
 **********************************************************************************************************************/
bool readAscii(QDataStream &stream, qint64 dataSize, QString *out)
{
    QByteArray buf;
    buf.resize(int(dataSize));
    if (dataSize > 0) {
        if (stream.readRawData(buf.data(), int(dataSize)) != dataSize)
            return false;
    }
    *out = QString::fromLatin1(buf).trimmed();
    // strip trailing NULs
    while (out->endsWith(QLatin1Char('\0')))
        out->chop(1);
    return true;
}

/*!*******************************************************************************************************************
 * \brief Parses a GDSII library from an open binary file into an in-memory cell map.
 *
 * Reads UNITS, structures (STRNAME…ENDSTR), and element records. PATH centre-lines are
 * converted to thin closed outlines. TEXT/NODE are skipped. On success \a cells holds
 * every structure; \a dbuMeters is the database unit in metres (default 1 nm).
 *
 * \param file       Open QFile in ReadOnly mode.
 * \param cells      Output map: structure name → RawCell.
 * \param dbuMeters  Output database unit length in metres.
 * \param errorMsg   Optional error text if no structures are found.
 * \return           True if at least one structure was parsed.
 **********************************************************************************************************************/
bool parseLibrary(QFile &file, QHash<QString, RawCell> *cells, double *dbuMeters, QString *errorMsg)
{
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::BigEndian);

    *dbuMeters = 1e-9; // default 1 nm
    RawCell *cur = nullptr;
    ElemKind elem = ElemKind::None;

    int layer = 0, datatype = 0, width = 0;
    bool reflect = false;
    double mag = 1.0, angle = 0.0;
    int cols = 1, rows = 1;
    QString sname;
    QVector<QPointF> xy;

    auto flushElement = [&]() {
        if (!cur || elem == ElemKind::None || elem == ElemKind::Other)
            return;
        if (elem == ElemKind::Boundary || elem == ElemKind::Box) {
            if (xy.size() >= 3) {
                RawBoundary b;
                b.layer = layer;
                b.datatype = datatype;
                b.xy = xy;
                cur->boundaries.push_back(b);
            }
        } else if (elem == ElemKind::Path) {
            // Approximate path as a thin closed outline (centerline ± half-width).
            if (xy.size() >= 2) {
                const double half = (width > 0) ? (0.5 * double(width)) : 0.5;
                QVector<QPointF> outline;
                outline.reserve(xy.size() * 2);
                for (int i = 0; i < xy.size(); ++i) {
                    QPointF d;
                    if (i + 1 < xy.size())
                        d = xy[i + 1] - xy[i];
                    else
                        d = xy[i] - xy[i - 1];
                    const double len = std::hypot(d.x(), d.y());
                    if (len < 1e-12)
                        continue;
                    const QPointF n(-d.y() / len * half, d.x() / len * half);
                    outline.push_back(xy[i] + n);
                }
                for (int i = xy.size() - 1; i >= 0; --i) {
                    QPointF d;
                    if (i > 0)
                        d = xy[i] - xy[i - 1];
                    else
                        d = xy[i + 1] - xy[i];
                    const double len = std::hypot(d.x(), d.y());
                    if (len < 1e-12)
                        continue;
                    const QPointF n(-d.y() / len * half, d.x() / len * half);
                    outline.push_back(xy[i] - n);
                }
                if (outline.size() >= 3) {
                    RawBoundary b;
                    b.layer = layer;
                    b.datatype = datatype;
                    b.xy = outline;
                    cur->boundaries.push_back(b);
                }
            }
        } else if (elem == ElemKind::Sref && !sname.isEmpty() && !xy.isEmpty()) {
            RawSref r;
            r.name = sname;
            r.reflect = reflect;
            r.mag = mag;
            r.angle = angle;
            r.tx = xy[0].x();
            r.ty = xy[0].y();
            cur->srefs.push_back(r);
        } else if (elem == ElemKind::Aref && !sname.isEmpty() && xy.size() >= 3) {
            RawAref r;
            r.name = sname;
            r.reflect = reflect;
            r.mag = mag;
            r.angle = angle;
            r.cols = cols;
            r.rows = rows;
            r.x0 = xy[0].x(); r.y0 = xy[0].y();
            r.xCol = xy[1].x(); r.yCol = xy[1].y();
            r.xRow = xy[2].x(); r.yRow = xy[2].y();
            cur->arefs.push_back(r);
        }
        elem = ElemKind::None;
        layer = datatype = width = 0;
        reflect = false;
        mag = 1.0;
        angle = 0.0;
        cols = rows = 1;
        sname.clear();
        xy.clear();
    };

    while (file.bytesAvailable() >= 4) {
        const qint64 recStart = file.pos();
        quint16 size = 0;
        quint8 recordType = 0, dataType = 0;
        stream >> size >> recordType >> dataType;
        if (stream.status() != QDataStream::Ok)
            break;
        if (size < 4 || (size & 1) != 0)
            break;
        const qint64 dataSize = qint64(size) - 4;
        if (dataSize > file.bytesAvailable())
            break;

        auto skipRest = [&]() {
            const qint64 next = recStart + size;
            return file.seek(next);
        };

        switch (recordType) {
        case 0x03: { // UNITS: two REAL8
            if (dataSize >= 16 && dataType == 0x05) {
                quint8 buf[16];
                if (stream.readRawData(reinterpret_cast<char *>(buf), 16) == 16)
                    *dbuMeters = decodeGdsReal8(buf + 8);
                else
                    return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        }
        case 0x05: { // BGNSTR
            flushElement();
            cur = nullptr;
            if (!skipRest()) return false;
            break;
        }
        case 0x06: { // STRNAME
            QString name;
            if (dataType == 0x06) {
                if (!readAscii(stream, dataSize, &name))
                    return false;
            } else if (!skipRest()) {
                return false;
            }
            RawCell cell;
            cell.name = name;
            cells->insert(name, cell);
            cur = &(*cells)[name];
            break;
        }
        case 0x07: // ENDSTR
            flushElement();
            cur = nullptr;
            if (!skipRest()) return false;
            break;
        case 0x08: // BOUNDARY
            flushElement();
            elem = ElemKind::Boundary;
            if (!skipRest()) return false;
            break;
        case 0x09: // PATH
            flushElement();
            elem = ElemKind::Path;
            if (!skipRest()) return false;
            break;
        case 0x0A: // SREF
            flushElement();
            elem = ElemKind::Sref;
            if (!skipRest()) return false;
            break;
        case 0x0B: // AREF
            flushElement();
            elem = ElemKind::Aref;
            if (!skipRest()) return false;
            break;
        case 0x2D: // BOX
            flushElement();
            elem = ElemKind::Box;
            if (!skipRest()) return false;
            break;
        case 0x0C: // TEXT
        case 0x15: // NODE
            flushElement();
            elem = ElemKind::Other;
            if (!skipRest()) return false;
            break;
        case 0x0D: // LAYER
            if (dataType == 0x02 && dataSize >= 2) {
                quint16 v = 0;
                stream >> v;
                layer = int(v);
                if (!skipRest()) return false; // consume any padding via seek from start
                // already read 2 of data; skipRest seeks to end of record — OK
            } else if (!skipRest()) {
                return false;
            }
            break;
        case 0x0E: // DATATYPE
        case 0x16: // TEXTTYPE
        case 0x2E: // BOXTYPE
            if (dataType == 0x02 && dataSize >= 2) {
                quint16 v = 0;
                stream >> v;
                datatype = int(v);
                if (!file.seek(recStart + size)) return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        case 0x0F: // WIDTH
            if (dataType == 0x03 && dataSize >= 4) {
                qint32 v = 0;
                stream >> v;
                width = int(v);
                if (!file.seek(recStart + size)) return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        case 0x12: // SNAME
            if (dataType == 0x06) {
                if (!readAscii(stream, dataSize, &sname))
                    return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        case 0x13: { // COLROW
            if (dataType == 0x02 && dataSize >= 4) {
                qint16 c = 0, r = 0;
                stream >> c >> r;
                cols = int(c);
                rows = int(r);
                if (!file.seek(recStart + size)) return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        }
        case 0x1A: { // STRANS
            if (dataType == 0x01 && dataSize >= 2) {
                quint16 bits = 0;
                stream >> bits;
                reflect = (bits & 0x8000) != 0;
                if (!file.seek(recStart + size)) return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        }
        case 0x1B: { // MAG
            if (dataType == 0x05 && dataSize >= 8) {
                quint8 buf[8];
                if (stream.readRawData(reinterpret_cast<char *>(buf), 8) != 8)
                    return false;
                mag = decodeGdsReal8(buf);
                if (!file.seek(recStart + size)) return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        }
        case 0x1C: { // ANGLE
            if (dataType == 0x05 && dataSize >= 8) {
                quint8 buf[8];
                if (stream.readRawData(reinterpret_cast<char *>(buf), 8) != 8)
                    return false;
                angle = decodeGdsReal8(buf);
                if (!file.seek(recStart + size)) return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        }
        case 0x10: { // XY
            if (dataType == 0x03) {
                const int nCoords = int(dataSize / 4);
                xy.clear();
                xy.reserve(nCoords / 2);
                for (int i = 0; i + 1 < nCoords; i += 2) {
                    qint32 x = 0, y = 0;
                    stream >> x >> y;
                    if (stream.status() != QDataStream::Ok)
                        return false;
                    xy.push_back(QPointF(double(x), double(y)));
                }
                if (!file.seek(recStart + size)) return false;
            } else if (!skipRest()) {
                return false;
            }
            break;
        }
        case 0x11: // ENDEL
            flushElement();
            if (!skipRest()) return false;
            break;
        case 0x04: // ENDLIB
            flushElement();
            return true;
        default:
            if (!skipRest())
                return false;
            break;
        }
    }

    flushElement();
    if (cells->isEmpty()) {
        if (errorMsg)
            *errorMsg = QStringLiteral("No structures found in GDS file.");
        return false;
    }
    return true;
}

/*!*******************************************************************************************************************
 * \brief Appends transformed cell boundaries to \a out in micrometres.
 *
 * \param cell     Source structure (database-unit polygons).
 * \param xf       Accumulated parent transform.
 * \param scaleUm  Metres-per-DBU × 1e6 → µm per DBU.
 * \param out      Destination flattened polygon list.
 **********************************************************************************************************************/
void emitBoundaries(const RawCell &cell,
                    const Aff2 &xf,
                    double scaleUm,
                    QVector<GdsFlatPolygon> *out)
{
    for (const RawBoundary &b : cell.boundaries) {
        GdsFlatPolygon poly;
        poly.layer = b.layer;
        poly.datatype = b.datatype;
        poly.pointsUm.reserve(b.xy.size());
        for (const QPointF &p : b.xy) {
            const QPointF m = xf.map(p.x(), p.y());
            poly.pointsUm << QPointF(m.x() * scaleUm, m.y() * scaleUm);
        }
        if (poly.pointsUm.size() >= 3)
            out->push_back(poly);
    }
}

/*!*******************************************************************************************************************
 * \brief Recursively flattens a named cell and its SREF/AREF children.
 *
 * Depth is capped at 64 to avoid runaway recursion on cyclic references.
 *
 * \param name     Structure name in \a cells.
 * \param cells    Full library map from parseLibrary.
 * \param xf       Transform from this cell into the top-cell frame.
 * \param scaleUm  DBU → µm scale factor.
 * \param depth    Current recursion depth.
 * \param out      Accumulated flattened polygons.
 **********************************************************************************************************************/
void flattenCell(const QString &name,
                 const QHash<QString, RawCell> &cells,
                 const Aff2 &xf,
                 double scaleUm,
                 int depth,
                 QVector<GdsFlatPolygon> *out)
{
    if (depth > 64)
        return;
    const auto it = cells.constFind(name);
    if (it == cells.cend())
        return;
    const RawCell &cell = it.value();
    emitBoundaries(cell, xf, scaleUm, out);

    for (const RawSref &r : cell.srefs) {
        const Aff2 local = fromStrans(r.reflect, r.mag, r.angle, r.tx, r.ty);
        flattenCell(r.name, cells, xf.composedWith(local), scaleUm, depth + 1, out);
    }

    for (const RawAref &r : cell.arefs) {
        const Aff2 base = fromStrans(r.reflect, r.mag, r.angle, 0, 0);
        const int nc = qMax(1, r.cols);
        const int nr = qMax(1, r.rows);
        const double dCx = (nc > 1) ? (r.xCol - r.x0) / double(nc - 1) : 0.0;
        const double dCy = (nc > 1) ? (r.yCol - r.y0) / double(nc - 1) : 0.0;
        const double dRx = (nr > 1) ? (r.xRow - r.x0) / double(nr - 1) : 0.0;
        const double dRy = (nr > 1) ? (r.yRow - r.y0) / double(nr - 1) : 0.0;
        for (int row = 0; row < nr; ++row) {
            for (int col = 0; col < nc; ++col) {
                Aff2 placed = base;
                placed.tx = r.x0 + col * dCx + row * dRx;
                placed.ty = r.y0 + col * dCy + row * dRy;
                flattenCell(r.name, cells, xf.composedWith(placed), scaleUm, depth + 1, out);
            }
        }
    }
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Flattens the named top cell of a GDSII file into micrometre polygons.
 *
 * Opens \a filePath, parses the library into an in-memory cell map, then recursively
 * expands SREF/AREF under \a topCell. BOUNDARY/BOX (and PATH outlines) are scaled from
 * database units to µm using the UNITS record (default 1 nm if missing).
 *
 * \param filePath  Path to the binary GDSII file.
 * \param topCell   Structure name to flatten.
 * \param out       Output polygon list; must be non-null (cleared on entry).
 * \param errorMsg  Optional error string on failure.
 * \return          True if \a out was filled; false on open/parse/missing-cell errors.
 **********************************************************************************************************************/
bool GdsLayout::flattenTopCell(const QString &filePath,
                               const QString &topCell,
                               QVector<GdsFlatPolygon> *out,
                               QString *errorMsg)
{
    if (!out)
        return false;
    out->clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg)
            *errorMsg = QStringLiteral("Cannot open GDS: %1").arg(file.errorString());
        return false;
    }

    QHash<QString, RawCell> cells;
    double dbuMeters = 1e-9;
    if (!parseLibrary(file, &cells, &dbuMeters, errorMsg)) {
        if (errorMsg && errorMsg->isEmpty())
            *errorMsg = QStringLiteral("Failed to parse GDS file.");
        return false;
    }

    const QString top = topCell.trimmed();
    if (top.isEmpty() || !cells.contains(top)) {
        if (errorMsg)
            *errorMsg = QStringLiteral("Top cell '%1' not found in GDS.").arg(top);
        return false;
    }

    const double scaleUm = dbuMeters * 1e6; // meters → µm
    flattenCell(top, cells, Aff2::identity(), scaleUm, 0, out);
    return true;
}
