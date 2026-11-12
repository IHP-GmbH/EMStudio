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

#ifndef EMEXPORT_H
#define EMEXPORT_H

#include <QMap>
#include <QPair>
#include <QSet>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QPolygonF>
#include <QJsonObject>
#include <QStringList>

#include "emmarkup.h"
#include "gdslayout.h"

namespace EmExport {

/*! What goes into the EM layout. */
struct Options
{
    QString         outGds;           //!< <name>.gds; <name>.simcfg and <name>_portmap.json go next to it
    QString         cell;             //!< Cell name in the exported GDS
    QString         substrateFile;    //!< Stackup XML for the .simcfg
    QSet<int>       stackLayers;      //!< GDS layers of the stackup (others are dropped)
    QSet<int>       purposes;         //!< Datatypes kept (empty = all)
    QStringList     metals;           //!< Stackup metal names, for the port checks
    bool            merge = true;     //!< Merge the shapes per layer and datatype
};

/*! One exported port: its .simcfg number and the markup port it comes from. */
struct ExportedPort
{
    int             number = 0;       //!< Number in the .simcfg / .sNp (layer 200 + number in the GDS)
    EmMarkupPort    port;             //!< Settings and layout number (port.number)
    bool            inSimcfg = false; //!< Settings complete; else exported as shapes only
};

struct Result
{
    bool                        ok = false;
    QString                     error;
    QString                     gds;
    QString                     simcfg;
    QString                     portmap;
    QString                     cell;
    QVector<ExportedPort>       ports;
    QMap<QPair<int, int>, int>  kept;           //!< (layer, datatype) → polygons written
    QMap<QPair<int, int>, int>  dropped;        //!< Not in the stackup / purposes
    QStringList                 portWalls;      //!< Region sides with a port wall (xmin, xmax, ymin, ymax)
    bool                        updatedSimcfg = false; //!< An existing setupEM file was updated
    QStringList                 warnings;
};

/*!
 * \brief Polygons of one layer clipped to \a clip (null rect: none), merged when \a merge.
 *
 * The output has no holes (each hole is joined to its outline by a cut line, as KLayout writes them
 * to GDS), vertices on the 1 nm grid.
 */
QVector<QPolygonF>  clipAndMerge(const QVector<QPolygonF> &polys, const QRectF &clip, bool merge);

/*! The .simcfg port entry of setupEM for \a port exported as \a number. */
QJsonObject         simcfgPort(int number, const EmMarkupPort &port);

/*!
 * \brief Writes the EM layout of \a markup: flat GDS, .simcfg and _portmap.json.
 *
 * Only stackup layers (and the model's purposes) are written, clipped to the EM region (when set)
 * and merged per layer; the ports in the region are numbered 1..N in layout order onto layers
 * 201.. as in klayout_em_export. The source polygons are not changed.
 */
Result              run(const QVector<GdsFlatPolygon> &polys, const EmMarkup &markup, const Options &options);

} // namespace EmExport

#endif // EMEXPORT_H
