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

#ifndef GDSLAYOUT_H
#define GDSLAYOUT_H

#include <QString>
#include <QVector>
#include <QPolygonF>

/*!*******************************************************************************************************************
 * \struct GdsFlatPolygon
 * \brief One flattened polygon from a GDSII stream, ready for LayoutView.
 *
 * Coordinates in \c pointsUm are in micrometers after applying structure
 * references (SREF/AREF) and the library UNITS scale.
 *
 * \var layer      GDS layer number.
 * \var datatype   GDS datatype.
 * \var pointsUm   Closed polygon vertices in µm (caller may close/open; ≥3 points required to draw).
 **********************************************************************************************************************/
struct GdsFlatPolygon
{
    int         layer = -1;
    int         datatype = 0;
    QPolygonF   pointsUm;
};

namespace GdsLayout {

/*!*******************************************************************************************************************
 * \brief Flattens the named top cell of a GDSII file into micrometre polygons.
 *
 * Parses BOUNDARY and BOX elements (PATH approximated as a thin outline) and
 * recursively resolves SREF/AREF with STRANS/MAG/ANGLE. Output polygons use
 * the library database unit converted to µm.
 *
 * \param filePath  Path to the binary GDSII file.
 * \param topCell   Structure name to flatten (must exist in the file).
 * \param out       Destination vector; cleared on entry when non-null.
 * \param errorMsg  Optional human-readable error on failure.
 * \return          True on success; false on I/O, parse, or missing-cell errors.
 **********************************************************************************************************************/
bool flattenTopCell(const QString &filePath,
                    const QString &topCell,
                    QVector<GdsFlatPolygon> *out,
                    QString *errorMsg = nullptr);

} // namespace GdsLayout

#endif // GDSLAYOUT_H
